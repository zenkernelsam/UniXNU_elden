"""Fault-inject the actual MHI firmware wait functions and shim wait macro."""
import argparse,hashlib,json,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'drivers/EldenWifi/peach'))
from MhiReadPatch import patch
def function(source,name):
    start=source.index('static int '+name+'(');end=source.index('\n}',start)+2
    return source[start:end]+'\n'
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('component','output'):p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--compiler',default='cc');a=p.parse_args()
    if a.output.exists():raise ValueError('New output directory required')
    a.output.mkdir(parents=True)
    current=(a.component/'out/mhi-src/boot.c').read_bytes()
    original=a.component/'out/mhi-src/boot.before-status-read-fix'
    data=original.read_bytes() if original.exists() else current
    derived=patch(data)
    if original.exists() and derived!=current:raise ValueError('Integrated MHI source differs from the tested patch')
    wait=(a.component/'shim/include/lx_os.h').read_text();start=wait.index('#define wait_event_timeout(w, cond, tmo_j)');end=wait.index('\n/* The condition',start)
    wait=wait[start:end]
    if hashlib.sha256(wait.encode()).hexdigest()!='5244039fb9f4b447fc30c384e8debe5f950b9e3f539e40cc8927d4eef3aadb38':
        raise ValueError('Pinned shim wait semantics changed')
    template=(ROOT/'drivers/EldenWifi/peach/TestMhiRead.c').read_text();results={}
    for name,body in [('original',data),('repaired',derived)]:
        code=template.replace('// @WAIT_MACRO@',wait).replace('// @MHI_FUNCTIONS@','\n'.join(function(body.decode(),n) for n in ('mhi_fw_load_bhie','mhi_fw_load_bhi')))
        source=a.output/(name+'.c');source.write_text(code);binary=a.output/name
        command=[a.compiler,'-std=gnu11','-O1','-g','-Wall','-Wextra','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-omit-frame-pointer','-DORIGINAL='+str(int(name=='original')),str(source),'-o',str(binary)]
        build=subprocess.run(command,capture_output=True,text=True);(a.output/(name+'.build.log')).write_text(build.stdout+build.stderr)
        if build.returncode:raise RuntimeError(build.stderr)
        run=subprocess.run([str(binary)],capture_output=True,text=True,check=True)
        results[name]={'passed':True,'stdout':run.stdout,'stderr':run.stderr,'source_sha256':hashlib.sha256(body).hexdigest(),'harness_sha256':hashlib.sha256(code.encode()).hexdigest()}
    result={'results':results,'integrated_source_verified':original.exists(),
            'actual_wait_macro_sha256':hashlib.sha256(wait.encode()).hexdigest(),'hardware_operations':False}
    (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
