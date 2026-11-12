"""Prepare a small BlueTool-only runtime, retaining original launchd jobs/cache."""
import argparse,copy,hashlib,json,plistlib,struct,sys
from pathlib import Path
from build_elden_bluetool import verify_target
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/legacy-workbench'))
from icon_cpu_trust import append,cdhash,parts
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
    if sys.flags.optimize:raise ValueError('Run without -O')
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('helper','launchd-cache','bluetool-job','trust-cache','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve()
    if out.exists():raise ValueError('Output must be new')
    expected=json.loads((ROOT/'profiles/v98-bt-bootstrap/userland.json').read_text())
    helper=a.helper.read_bytes();verify_target(a.helper)
    if sha(helper)!=expected['BlueTool_sha256'] or cdhash(helper).hex()!=expected['BlueTool_cdhash']:raise ValueError('Unqualified helper')
    original_cache=a.launchd_cache.read_bytes();job_bytes=a.bluetool_job.read_bytes()
    if sha(original_cache)!='f1b99c4e3e738cb1c2622ad82f5e78cd2fbd1f88c8ced13c423aabf91b2b22eb' or sha(job_bytes)!='e6a3c88d3458b7d6adc5da3b11099769648068baf81beae63283300c80c6606a':raise ValueError('Wrong original jobs/cache')
    job=plistlib.loads(job_bytes);cache=plistlib.loads(original_cache)
    if cache['LaunchDaemons']['/System/Library/LaunchDaemons/com.apple.BlueTool.plist']!=job:raise ValueError('Original cache/job disagree')
    if job['UserName']!='mobile' or job['ProgramArguments']!=['/usr/sbin/BlueTool','-R'] or job['MachServices']!={'com.apple.BlueTool':True}:raise ValueError('Original launch contract changed')
    adt=json.loads((ROOT/'profiles/v91/boot-build/adt.json').read_text());new=copy.deepcopy(adt);found=[]
    def visit(node):
        for prop in node['properties']:
            if prop['name']=='text-fixups':found.append(prop)
        for child in node['children']:visit(child)
    visit(new['tree'])
    if len(found)!=1:raise ValueError('Expected one original fixup table')
    baseline=bytes.fromhex(found[0]['hex'])
    if not baseline.endswith(b'\0') or b'bluetoothd,' in baseline:raise ValueError('Unexpected prior Bluetooth fixups')
    fixups=baseline+b'bluetoothd,ddd20,a0050034,1f2003d5\0';found[0]['hex']=fixups.hex()
    out.mkdir(parents=True)
    (out/'BlueTool').write_bytes(helper)
    # The schema's prior digest must not masquerade as the new encoded ADT hash.
    new.pop('sha256',None);(out/'adt-input.json').write_text(json.dumps(new,indent=2)+'\n')
    header='#include <stdint.h>\nstatic const uint8_t elden_bt_runtime_payload[]={\n'+','.join(str(v) for v in helper)+'\n};\n'
    header+='static const uint8_t elden_bt_runtime_fixups[]={'+','.join(str(v) for v in fixups)+'};\n'
    (out/'EldenBtRuntimePayload.h').write_text(header,newline='\n')
    original=a.trust_cache.read_bytes()
    if sha(original)!='583b2012d14c45f74532bccc0ec6eaf35143ba47b4dfbfabbe7dff5ced7ee67e':raise ValueError('Expected complete v94 trust container')
    trust,digest=append(original,helper);(out/'runtime.trustcache').write_bytes(trust)
    report={'helper_sha256':sha(helper),'helper_cdhash':digest,'payload_header_sha256':sha(header.encode()),
            'trust_sha256':sha(trust),'original_trust_sha256':sha(original),'original_trust_parts_preserved':parts(trust)[:-1]==parts(original),
            'original_cache_sha256':sha(original_cache),'original_job_sha256':sha(job_bytes),'original_jobs':len(cache['LaunchDaemons']),
            'original_job_unchanged':True,'original_cache_unchanged':True,'runtime_files':['/usr/sbin/BlueTool'],
            'text_fixups_before_hex':baseline.hex(),'text_fixups_after_hex':fixups.hex(),'original_bluetoothd_cdhash':'fe7bc182386a2766c7574f8b48412ff9df113564',
            'F2_patch':False,'new_boot_image':False,'installed':False,'hardware_tested':False}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({k:report[k] for k in ('helper_cdhash','original_jobs','original_job_unchanged','original_cache_unchanged')},indent=2))
if __name__=='__main__':main()
