"""Build the original21H461 cache with Sheng's four BlueTool lifecycle overrides.

Only reads the original OS image; writes new local output files. Does not install
the cache or access a tablet. Apple plutil and codesign require macOS.
"""
from pathlib import Path
import argparse,copy,ctypes,hashlib,json,plistlib,re,struct,subprocess,sys
from build_v91_data_seed import attach,detach
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/legacy-workbench'))
from icon_cpu_trust import parts
OVERRIDES={'UserName':'root','EnableTransactions':True,'EnablePressuredExit':False,'ThrottleInterval':3}
KEY='/System/Library/LaunchDaemons/com.apple.BlueTool.plist'
def sha(data):return hashlib.sha256(data).hexdigest()
def host_xpc(data):
    x=ctypes.CDLL('/usr/lib/system/libxpc.dylib')
    for name,args,result in [('xpc_create_from_plist',[ctypes.c_void_p,ctypes.c_size_t],ctypes.c_void_p),
        ('xpc_dictionary_get_value',[ctypes.c_void_p,ctypes.c_char_p],ctypes.c_void_p),
        ('xpc_dictionary_get_string',[ctypes.c_void_p,ctypes.c_char_p],ctypes.c_char_p),
        ('xpc_dictionary_get_bool',[ctypes.c_void_p,ctypes.c_char_p],ctypes.c_bool),
        ('xpc_dictionary_get_int64',[ctypes.c_void_p,ctypes.c_char_p],ctypes.c_int64),
        ('xpc_dictionary_get_count',[ctypes.c_void_p],ctypes.c_size_t),('xpc_release',[ctypes.c_void_p],None)]:
        f=getattr(x,name);f.argtypes=args;f.restype=result
    buffer=ctypes.create_string_buffer(data);parsed=x.xpc_create_from_plist(buffer,len(data))
    if not parsed:raise ValueError('Host private XPC parser rejected cache')
    try:
        jobs=x.xpc_dictionary_get_value(parsed,b'LaunchDaemons');assert jobs and x.xpc_dictionary_get_count(jobs)==561
        blue=x.xpc_dictionary_get_value(jobs,KEY.encode());assert blue
        assert x.xpc_dictionary_get_string(blue,b'UserName')==b'root'
        assert x.xpc_dictionary_get_bool(blue,b'EnableTransactions')
        assert not x.xpc_dictionary_get_bool(blue,b'EnablePressuredExit')
        assert x.xpc_dictionary_get_int64(blue,b'ThrottleInterval')==3
    finally:x.xpc_release(parsed)
def main():
    if sys.platform!='darwin':raise SystemExit('macOS plutil/codesign required')
    if sys.flags.optimize:raise ValueError('Assertions required')
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('os-image','trust-cache','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();image=a.os_image.resolve(strict=True)
    if out.exists() or not image.is_file() or str(image).startswith('/dev/'):raise ValueError('New output and original image file required')
    trust=a.trust_cache.read_bytes()
    if sha(trust)!='a6758681cd8ce0f364411bf65fc1207514b3470b0c16699e2c1a0d12f22a2c65':raise ValueError('Unexpected existing Bluetooth trust container')
    out.mkdir(parents=True);mount=out/'os-mount';mount.mkdir();dev=attach(image,mount)
    try:
        original=(mount/'System/Library/xpc/launchd.plist').read_bytes();job_bytes=(mount/KEY.lstrip('/')).read_bytes()
    finally:detach(dev)
    if sha(original)!='f1b99c4e3e738cb1c2622ad82f5e78cd2fbd1f88c8ced13c423aabf91b2b22eb' or sha(job_bytes)!='e6a3c88d3458b7d6adc5da3b11099769648068baf81beae63283300c80c6606a':raise ValueError('Original cache/job identity differs')
    before=plistlib.loads(original);old_job=plistlib.loads(job_bytes);assert len(before['LaunchDaemons'])==561 and before['LaunchDaemons'][KEY]==old_job
    after=copy.deepcopy(before);after['LaunchDaemons'][KEY].update(OVERRIDES)
    assert all(after[k]==v for k,v in before.items() if k!='LaunchDaemons')
    assert all(after['LaunchDaemons'][k]==v for k,v in before['LaunchDaemons'].items() if k!=KEY)
    changed={k for k in set(old_job)|set(after['LaunchDaemons'][KEY]) if old_job.get(k)!=after['LaunchDaemons'][KEY].get(k)}
    assert changed==set(OVERRIDES)
    cache=out/'launchd.plist';cache.write_bytes(plistlib.dumps(after,fmt=plistlib.FMT_XML,sort_keys=False))
    subprocess.run(['plutil','-convert','binary1',str(cache)],check=True);data=cache.read_bytes();assert plistlib.loads(data)==after;host_xpc(data)
    sig=out/'launchd.plist.sig';sign=['codesign','--force','--sign','-','--pagesize','4096','--digest-algorithm','sha256','--identifier','org.unixnu.elden.bluetooth.launchd-cache','--detached',str(sig),str(cache)]
    subprocess.run(sign,check=True);subprocess.run(['codesign','--verify','--strict','--detached',str(sig),str(cache)],check=True)
    detail=subprocess.run(['codesign','--display','--verbose=4','--detached',str(sig),str(cache)],check=True,capture_output=True,text=True).stderr
    (out/'codesign.txt').write_text(detail);match=re.search(r'^CDHash=([0-9a-f]{40})$',detail,re.M)
    if not match:raise ValueError('No verified cache CDHash')
    digest=bytes.fromhex(match[1]);oldparts=parts(trust);assert not any(p[i:i+20]==digest for p in oldparts for i in range(24,len(p),24))
    module=struct.pack('<I16sI',2,hashlib.sha256(trust+digest).digest()[:16],1)+digest+bytes([2,0,0,0]);allparts=oldparts+[module];at=4*(len(allparts)+1);offsets=[]
    for part in allparts:offsets.append(at);at+=len(part)
    combined=struct.pack('<I',len(allparts))+struct.pack('<'+'I'*len(offsets),*offsets)+b''.join(allparts)
    assert parts(combined)[:-1]==oldparts and parts(combined)[-1]==module;(out/'runtime.trustcache').write_bytes(combined)
    header='#include <stdint.h>\n'
    for name,blob in [('elden_bt_launch_cache',data),('elden_bt_launch_cache_signature',sig.read_bytes())]:header+='static const uint8_t '+name+'[]={\n'+','.join(map(str,blob))+'\n};\n'
    (out/'EldenBtLaunchCache.h').write_bytes(header.encode())
    report={'original_cache_sha256':sha(original),'original_job_sha256':sha(job_bytes),'original_job':old_job,'overrides':OVERRIDES,'jobs':561,'other_jobs_unchanged':560,'other_top_level_entries_unchanged':True,'new_job':after['LaunchDaemons'][KEY],'cache_sha256':sha(data),'cache_bytes':len(data),'signature_sha256':sha(sig.read_bytes()),'cdhash':digest.hex(),'input_trust_sha256':sha(trust),'trust_sha256':sha(combined),'old_trust_modules_unchanged':True,'host_xpc_parser_passed':True,'target17_parser_not_yet_tested':True,'detached_signature_verified':True,'header_sha256':sha(header.encode()),'installed':False,'device_operation':False,'source_sha256':sha(Path(__file__).read_bytes())}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
