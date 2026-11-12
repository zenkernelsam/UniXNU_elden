"""Fault-inject exact frozen and derived touch methods, using host IOKit stubs."""
import argparse, hashlib, json, re, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'drivers/EldenHidDelivery'))
from SourcePatch import original,derive

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--compiler',default='c++')
    p.add_argument('--wsl-distro')
    a=p.parse_args()
    if a.output.exists():raise ValueError('New output directory required')
    a.output.mkdir(parents=True)
    def path(v):
        v=str(Path(v).resolve())
        if not a.wsl_distro:return v
        if v[1]!=':':raise ValueError('Expected Windows path')
        return '/mnt/'+v[0].lower()+v[2:].replace('\\','/')
    def run(command):
        if a.wsl_distro:command=['wsl','-d',a.wsl_distro,'--']+command
        result=subprocess.run(command,capture_output=True,check=True)
        # wsl.exe may prepend UTF-16 diagnostics to a UTF-8 compiler stream.
        # Preserve every byte through escapes instead of losing a reader thread.
        result.stdout=result.stdout.decode('utf-8',errors='backslashreplace')
        result.stderr=result.stderr.decode('utf-8',errors='backslashreplace')
        return result
    template=(ROOT/'drivers/EldenHidDelivery/TestHarness.cpp').read_text()
    reports={}
    for label,source in [('original',original),('repaired',derive)]:
        native=source('NativeHidInput.cpp').decode();glue=source('EldenHidGlue.cpp').decode()
        methods=native[native.index('    uint32_t lastSeq=0;'):native.index('    // The button driver\'s thread')]
        barrier='__asm__ volatile("dmb ish":::"memory");'
        if methods.count(barrier)!=2:raise ValueError('Unexpected barrier scope')
        methods=methods.replace(barrier,'std::atomic_thread_fence(std::memory_order_seq_cst);')
        publisher=glue[glue.index('extern "C" int elden_touch_hid_publish('):glue.index('// One explicitly gated UI diagnostic')]
        text=template.replace('// @NATIVE_METHODS@',methods).replace('// @PUBLISHER@',publisher)
        cpp=a.output/(label+'.cpp');binary=a.output/label
        cpp.write_text(text,encoding='utf-8',newline='\n')
        command=[a.compiler,'-std=c++17','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer','-pthread','-DORIGINAL='+str(int(label=='original')),str(path(cpp)),'-o',path(binary)]
        if a.wsl_distro:command[1:1]=['-fno-pie','-no-pie']
        build=run(command);test=run([path(binary)])
        reports[label]={'native_sha256':hashlib.sha256(native.encode()).hexdigest(),'glue_sha256':hashlib.sha256(glue.encode()).hexdigest(),
            'build_stdout':build.stdout,'build_stderr':build.stderr,'stdout':test.stdout,'stderr':test.stderr,'passed':True}
    traces=[re.search(r'Normal trace: ([0-9a-f]+ reports=\d+)',reports[n]['stdout']).group(1) for n in ('original','repaired')]
    if traces[0]!=traces[1]:raise ValueError('Successful multi-touch report bytes changed')
    report={'tests':reports,'normal_trace_equal':True,'normal_trace':traces[0],'barrier_host_substitution':True,'actual_methods_extracted':True,'hardware_tested':False}
    (a.output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
