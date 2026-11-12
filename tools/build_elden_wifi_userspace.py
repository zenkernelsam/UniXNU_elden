"""Build sheng's unchanged native supplicant for arm64/iOS17, offline only.

Uses a pinned Git snapshot and a verified wpa_supplicant release supplied by
the caller. No reference checkout changes, Linux dependency downloads, device
access, daemon installation or network credentials are involved.
"""
import argparse,hashlib,io,json,os,plistlib,re,shutil,subprocess,sys,tarfile
from pathlib import Path
from build_elden_bluetool import verify_target
REVISION='5f7f5a8a9f02f7ab45ed10a2f226e348c519a838'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('component-repository','supplicant-archive','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve()
    if out.exists():raise ValueError('New output directory required')
    packed=subprocess.check_output(['git','-C',str(a.component_repository),'archive',REVISION])
    out.mkdir(parents=True);component=out/'component';component.mkdir()
    with tarfile.open(fileobj=io.BytesIO(packed)) as t:t.extractall(component,filter='data')
    lock=json.loads((component/'tools/source-lock.json').read_text())['supplicant']
    if sha(a.supplicant_archive)!=lock['sha256']:raise ValueError('Supplicant release hash mismatch')
    (component/'deps').mkdir();(component/'out').mkdir()
    shutil.copyfile(a.supplicant_archive,component/'deps'/lock['archive'])
    changes={}
    def patch(name,replacements):
        path=component/name;text=path.read_text();before=sha(path)
        for old,new in replacements:
            if text.count(old)!=1:raise ValueError('Upstream recipe context changed: '+name)
            text=text.replace(old,new)
        path.write_text(text,newline='\n');changes[name]={'before':before,'after':sha(path)}
    # The userland build consumes only the pinned release and component headers.
    # Verify that release here instead of downloading unrelated Linux/FreeBSD trees.
    patch('tools/prepare-supplicant.py',[(
        'subprocess.run(["python3", str(root / "tools/fetch-sources.py"), "--check"], check=True)',
        'assert hashlib.sha256((root / "deps/wpa_supplicant-2.12.tar.gz").read_bytes()).hexdigest() == "'+lock['sha256']+'"')])
    patch('tools/build-supplicant.py',[
        ('"arm64e-apple-ios27.0"','"arm64-apple-ios17.0"'),
        ('(output / "report.json").unlink(missing_ok=True)',
         'if (output / "report.json").exists(): raise SystemExit("Preserve existing report")'),
        ('(root / "out/wpa-src/wpa_supplicant/wpa_supplicant").unlink(missing_ok=True)',
         'if (root / "out/wpa-src/wpa_supplicant/wpa_supplicant").exists(): raise SystemExit("Preserve existing binary")')])
    clang=subprocess.check_output(['xcrun','--sdk','iphoneos','--find','clang'],text=True).strip()
    env=dict(os.environ);env['PATH']=str(Path(sys.executable).parent)+os.pathsep+str(Path(clang).parent)+os.pathsep+env['PATH']
    with (out/'build.log').open('x') as log:
        subprocess.run([sys.executable,str(component/'tools/build-supplicant.py'),'--target','ios','--jobs','4'],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    adapter=json.loads((component/'out/wpa-src/native-adapter.json').read_text())
    permitted={'src/drivers/drivers.mak','src/drivers/drivers.c','wpa_supplicant/wpa_supplicant.c'}
    if set(adapter['changes'])!=permitted or adapter['authentication_algorithms_modified']:raise ValueError('Unexpected authentication source changes')
    checked=0
    with tarfile.open(a.supplicant_archive) as t:
        for member in t.getmembers():
            if not member.isfile():continue
            relative=Path(member.name).relative_to(lock['directory']).as_posix()
            if relative in permitted:continue
            data=t.extractfile(member).read()
            if (component/'out/wpa-src'/relative).read_bytes()!=data:raise ValueError('Original release file changed: '+relative)
            checked+=1
    binary=out/'unixnu-wpa_supplicant';shutil.copyfile(component/'out/wpa-ios/bin/wpa_supplicant',binary);binary.chmod(0o755)
    verify_target(binary)
    entitlements=out/'entitlements.plist';entitlements.write_bytes(plistlib.dumps({'com.apple.private.security.no-container':True}))
    subprocess.run(['codesign','--force','--sign','-','--pagesize','4096','--identifier','org.unixnu.wifi.supplicant','--entitlements',str(entitlements),str(binary)],check=True)
    subprocess.run(['codesign','--verify','--strict',str(binary)],check=True)
    target=verify_target(binary)
    detail=subprocess.run(['codesign','--display','--verbose=4',str(binary)],capture_output=True,text=True,check=True).stderr
    match=re.search(r'^CDHash=([0-9a-f]{40})$',detail,re.M)
    if not match:raise ValueError('Missing signed CDHash')
    # The same job/control paths as the proven sheng userland adapter. This is
    # a staging template, not a merged/signed system launchd cache.
    job={'Label':'org.unixnu.wifi.supplicant','ProgramArguments':['/usr/libexec/unixnu-wpa_supplicant','-Dsheng','-ien0','-c/private/var/ngpu/etc/wifi/wpa_supplicant.conf','-q','-s'],
         'RunAtLoad':True,'KeepAlive':True,'ThrottleInterval':3,'UserName':'root','Umask':0o77,'ProcessType':'Interactive'}
    (out/'org.unixnu.wifi.supplicant.plist').write_bytes(plistlib.dumps(job))
    (out/'wpa_supplicant.conf').write_text('# Networks arrive from the native Wi-Fi control channel.\nap_scan=1\n')
    recipe=json.loads((component/'out/wpa-ios/report.json').read_text())
    result={'component_revision':REVISION,'release_sha256':lock['sha256'],'target':target,'cdhash':match[1],
            'recipe_changes':changes,'unchanged_release_files':checked,'authentication_algorithms_modified':False,
            'adapter_sha256':recipe['adapter_sha256'],'original_registration_changes':adapter['changes'],
            'control_endpoint':'org.unixnu.wifi.control','files':{n:sha(out/n) for n in ('unixnu-wpa_supplicant','entitlements.plist','org.unixnu.wifi.supplicant.plist','wpa_supplicant.conf')},
            'runtime_installed':False,'trust_cache_updated':False,'launchd_cache_merged':False,'kernel_endpoint_verified':False,
            'hardware_tested':False,'device_operations':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('target','cdhash','unchanged_release_files','hardware_tested')},indent=2))
if __name__=='__main__':main()
