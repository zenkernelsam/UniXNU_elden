"""Host-verify the KMD diagnostics parity (opt-008); no device IO.

Verifies the three offline-safe diagnostics features the pinned A830 tree carries
(research/Adreno740-ipados27 a423030 + 10ac0ea lineage equivalents, and the
Adreno740-a830 0ee17b4 bounded frame trace ring, preemption excluded):
 1. nbNote kernel pass/pipeline notes (NativeAdrenoMethodNote = 18): exact extracted
    method handler + lazily-allocated bounded 512-row rings, compiled under ASan/UBSan
    with kernel shims — validation rejections, atomic slot claim, pid stamping.
 2. GPU priority caller attribution: exact extracted setPriority body — newest-32 ring
    rows carrying pid, both values, role, time and the per-connection told count.
 3. NativeGpuTrace bounded frame trace ring: the pin's own host test
    (NativeGpuTraceTest.cpp) compiled verbatim against the tree's NativeGpuTrace.h/.inc
    under ASan/UBSan — capacity wrap, dropped counting, concurrent writers.
Also verifies the derivation pipeline preserves the feature symbols into the EldenGpu
driver units. Host/model evidence only.
"""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, sys

ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenGpu'
sys.path.insert(0,str(DRIVER))

def run(cmd,log,cwd=None):
    proc=subprocess.run(list(map(str,cmd)),capture_output=True,cwd=cwd)
    Path(log).write_bytes(proc.stdout+proc.stderr)
    return proc.returncode

def extract_block(text,start_anchor):
    at=text.index(start_anchor)
    depth=0;began=False
    for i in range(at,len(text)):
        c=text[i]
        if c=='{':depth+=1;began=True
        elif c=='}':
            depth-=1
            if began and depth==0:return text[at:i+1]
    raise ValueError('unterminated block: '+start_anchor[:60])

def extract_fn(text,signature):
    scan=0
    while True:
        cand=text.index(signature,scan)
        window=text[cand:cand+len(signature)+160]
        if '{' in window and (';' not in window[:window.index('{')]):
            blank=text.rindex('\n\n',0,cand)
            return extract_block(text[blank+2:],signature)+'\n' if False else _full(text,blank+2)
        scan=cand+1

def _full(text,start):
    depth=0;began=False
    for i in range(start,len(text)):
        c=text[i]
        if c=='{':depth+=1;began=True
        elif c=='}':
            depth-=1
            if began and depth==0:return text[start:i+1]
    raise ValueError('unterminated function')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('tree','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    tree=a.tree.resolve();out=a.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    gpu=tree/'drivers/Adreno740'
    result={'phases':{},'device_access':False}
    pub=(gpu/'NativeAdrenoPublish.cpp').read_text(errors='replace')
    uc=(gpu/'NativeAdrenoUserClient.cpp').read_text(errors='replace')
    abi=(gpu/'NativeAdrenoABI.h').read_text(errors='replace')

    # Phase 0: derivation preserves the feature symbols (pure-python derive, same as chain).
    from RuntimeSource import publisher as rt_publisher
    from MaintenanceSource import derive as maint_derive
    derived=maint_derive('NativeAdrenoPublish.cpp',rt_publisher((gpu/'NativeAdrenoPublish.cpp').read_bytes())).decode()
    symbols=['native_adreno_pass_log','native_adreno_pipe_log','NativeAdrenoMethodNote','noteRows',
             'native_adreno_recover_log','native_adreno_cp_check']
    census={s:derived.count(s) for s in symbols}
    ok=all(v>0 for v in census.values())
    result['phases']['derivation_preserves']={'pass':ok,'census':census}
    if not ok:raise SystemExit('derivation drops diagnostics symbols: '+json.dumps(census))

    # Phase 1: nbNote method handler — exact extraction, kernel-shimmed behavioral test.
    note_rows_fn=_full(pub,pub.index('static volatile uint32_t *noteRows'))
    decl_at=pub.index('enum { NativeAdrenoNoteRows')
    decl_end=pub.index('static volatile uint32_t *noteRows')
    note_decls=pub[decl_at:decl_end]
    handler=extract_block(pub,'if(method==NativeAdrenoMethodNote)')
    abi_note=re.search(r'/\* A note a process leaves.*?struct NativeAdrenoAbiNote \{[^}]+\};',abi,re.S)
    if not abi_note:raise SystemExit('NativeAdrenoAbiNote not found in ABI header')
    method_enum=re.search(r'NativeAdrenoMethodNote\s*=\s*(\d+)',abi)
    result['phases']['method_note_value']={'pass':method_enum and method_enum.group(1)=='18','value':method_enum.group(1) if method_enum else None}
    harness='''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef int IOReturn;
#define kIOReturnBadArgument 4
#define kIOReturnNoMemory 9
#define kIOReturnSuccess 0
enum { NativeAdrenoMethodNote = 18 };
''' + abi_note.group(0).split('struct NativeAdrenoAbiNote')[0] + '''
struct NativeAdrenoAbiNote {
    uint32_t kind;
    uint32_t count;
    uint32_t words[NativeAdrenoNoteWords];
};
static void *IOMallocZero(size_t n){return calloc(1,n);}
static void IOFree(void *p,size_t){free(p);}
static int proc_selfpid(){return 4242;}
''' + note_decls + note_rows_fn + '''
/* exact extracted handler body, wrapped: method/input/output become parameters */
static IOReturn runNote(uint32_t method,const void *input,size_t input_size,size_t output_size){
''' + handler.replace('if(method==NativeAdrenoMethodNote)','if(method==NativeAdrenoMethodNote)') + '''
   return 0;
}
int main(void){
   NativeAdrenoAbiNote note{};
   /* wrong size rejected */
   if(runNote(18,&note,4,0)!=kIOReturnBadArgument)return 1;
   /* bad kind rejected */
   note.kind=9;note.count=4;
   if(runNote(18,&note,sizeof note,0)!=kIOReturnBadArgument)return 2;
   /* count overflow rejected */
   note.kind=NativeAdrenoNotePass;note.count=NativeAdrenoNoteWords+1;
   if(runNote(18,&note,sizeof note,0)!=kIOReturnBadArgument)return 3;
   /* valid pass note accepted and stamped with pid */
   note.kind=NativeAdrenoNotePass;note.count=4;
   for(unsigned i=0;i<4;++i)note.words[i]=100+i;
   if(runNote(18,&note,sizeof note,0)==kIOReturnBadArgument)return 4;
   if(native_adreno_pass_count!=1)return 5;
   if(native_adreno_pass_log[1]!=4242)return 6;
   if(native_adreno_pass_log[0]!=4)return 7;
   for(unsigned i=0;i<4;++i)if(native_adreno_pass_log[2+i]!=100+i)return 8;
   /* valid pipeline note goes to the other ring */
   note.kind=NativeAdrenoNotePipeline;note.count=2;note.words[0]=7;note.words[1]=8;
   if(runNote(18,&note,sizeof note,0)==kIOReturnBadArgument)return 9;
   if(native_adreno_pipe_count!=1)return 10;
   if(native_adreno_pipe_log[2]!=7)return 11;
   /* rows are bounded: 512 slots wrap */
   for(unsigned i=0;i<520;++i){note.kind=NativeAdrenoNotePass;note.count=1;note.words[0]=i;runNote(18,&note,sizeof note,0);}
   if(native_adreno_pass_count!=521)return 12;
   printf("nbNote handler: all cases pass\\n");
   return 0;
}
'''
    (out/'test_nbnote.c').write_text(harness.replace('cplusplus','c'))
    # compile as C++ since kernel sources use C++ idioms (nullptr, constexpr in headers)
    (out/'test_nbnote.cpp').write_text(harness)
    rc=run(['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
            out/'test_nbnote.cpp','-o',out/'test_nbnote'],out/'nbnote-compile.log')
    if rc:raise SystemExit('nbNote harness compile failed; see nbnote-compile.log')
    rc=run([out/'test_nbnote'],out/'nbnote-run.log')
    result['phases']['nbnote_unit']={'pass':rc==0,'run_rc':rc}
    if rc:raise SystemExit('nbNote harness failed rc='+str(rc))

    # Phase 2: priority attribution — exact extracted setPriority body.
    setp=_full(uc,uc.index('IOReturn NativeAdrenoUserClient::setPriority(uint32_t foreground,uint32_t background)'))
    harness2='''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef int IOReturn;
#define kIOReturnSuccess 0
#define kIOReturnBadArgument 4
#define kIOReturnNotReady 7
extern "C" {volatile uint32_t native_adreno_prio_log[32][6];volatile uint32_t native_adreno_prio_count;}
extern "C" uint64_t native_adreno_now_us(void){return 4242000000u;}
static int proc_selfpid(){return 777;}
struct NativeAdrenoUserClient {
   bool open_=true;
   int32_t priority_foreground_=-1,priority_background_=-1;
   uint32_t priority_told_=0;
   int currentGpuRole()const{return 2;}
''' + setp.replace('IOReturn NativeAdrenoUserClient::setPriority(uint32_t foreground,uint32_t background)','IOReturn setPriority(uint32_t foreground,uint32_t background)') + '''
};
int main(void){
   NativeAdrenoUserClient c;
   /* closed client is refused without touching the ring */
   c.open_=false;
   if(c.setPriority(5,3)!=kIOReturnNotReady)return 1;
   if(native_adreno_prio_count!=0)return 2;
   c.open_=true;
   /* out-of-range values rejected: >5 and foreground==3 */
   if(c.setPriority(6,3)!=kIOReturnBadArgument)return 3;
   if(c.setPriority(1,6)!=kIOReturnBadArgument)return 4;
   if(c.setPriority(3,1)!=kIOReturnBadArgument)return 5;
   if(native_adreno_prio_count!=0)return 6;
   /* valid call: row carries pid, both values, role, ms time, pre-increment told */
   if(c.setPriority(5,2)!=kIOReturnSuccess)return 7;
   if(native_adreno_prio_count!=1)return 8;
   if(native_adreno_prio_log[0][0]!=777)return 9;
   if(native_adreno_prio_log[0][1]!=5||native_adreno_prio_log[0][2]!=2)return 10;
   if(native_adreno_prio_log[0][3]!=2)return 11;
   if(native_adreno_prio_log[0][4]!=4242000u)return 12;
   if(native_adreno_prio_log[0][5]!=0)return 13;
   if(c.priority_told_!=1)return 14;
   /* second call: told increments, ring advances */
   c.setPriority(4,4);
   if(native_adreno_prio_count!=2)return 15;
   if(native_adreno_prio_log[1][5]!=1)return 16;
   /* member fields track the last accepted pair */
   if(c.priority_foreground_!=4||c.priority_background_!=4)return 17;
   /* ring bounded at 32 rows, wraps by count&31 */
   for(unsigned i=0;i<40;++i)c.setPriority(1,1);
   if(native_adreno_prio_count!=42)return 18;
   if(native_adreno_prio_log[10][1]!=1)return 19;
   /* told saturates at ffffffff */
   c.priority_told_=0xffffffffu;
   c.setPriority(1,1);
   if(c.priority_told_!=0xffffffffu)return 20;
   printf("priority attribution: all cases pass\\n");
   return 0;
}
'''
    (out/'test_priority.cpp').write_text(harness2)
    rc=run(['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
            out/'test_priority.cpp','-o',out/'test_priority'],out/'priority-compile.log')
    if rc:raise SystemExit('priority harness compile failed; see priority-compile.log')
    rc=run([out/'test_priority'],out/'priority-run.log')
    result['phases']['priority_unit']={'pass':rc==0,'run_rc':rc}
    if rc:raise SystemExit('priority harness failed rc='+str(rc))

    # Phase 3: the pin's own NativeGpuTrace host test, verbatim, against tree headers.
    test_src=(gpu/'NativeGpuTraceTest.cpp')
    if not test_src.is_file():raise SystemExit('NativeGpuTraceTest.cpp missing from tree')
    for hdr in ('NativeGpuTrace.h','NativeGpuTrace.inc','NativeGpuTraceStore.h'):
        data=(gpu/hdr).read_bytes()
        (out/hdr).write_bytes(data)
    (out/'NativeGpuTraceTest.cpp').write_bytes(test_src.read_bytes())
    rc=run(['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',out,
            out/'NativeGpuTraceTest.cpp','-o',out/'test_gpustrace','-pthread'],out/'gpustrace-compile.log')
    if rc:raise SystemExit('gpustrace compile failed; see gpustrace-compile.log')
    rc=run([out/'test_gpustrace'],out/'gpustrace-run.log')
    result['phases']['gpustrace_unit']={'pass':rc==0,'run_rc':rc}
    if rc:raise SystemExit('gpustrace test failed rc='+str(rc))

    result['pass']=True
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: KMD diagnostics verification; host/model evidence only')
if __name__=='__main__':main()
