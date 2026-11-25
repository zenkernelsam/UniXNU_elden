"""Compile selected actual presenter lifecycle methods against object peers."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
def method(text,signature):
 if text.count(signature)!=1:raise ValueError('Method identity changed: '+signature)
 start=text.index(signature);at=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[at]=='{')-(text[at]=='}');at+=1
 return text[start:at]
STUBS={'IOKit/IOService.h':'''#pragma once
#include <stdint.h>
class IOMemoryMap;
class IOMemoryDescriptor{public:virtual ~IOMemoryDescriptor()=default;};
class IOService{public:unsigned refs=1;virtual ~IOService()=default;virtual bool init(){return true;}
 virtual bool attach(IOService*){return true;}virtual void detach(IOService*){}virtual bool start(IOService*){return true;}
 virtual void stop(IOService*){}virtual void release(){--refs;}static IOService *getResourceService(){static IOService r;return &r;}};
class OSMetaClass{public:static void *preModLoad(const char *){static int h;return &h;}static int postModLoad(void *){return 0;}};
''','IOKit/IOLib.h':'''#pragma once
using IOReturn=int;
enum{kIOReturnSuccess=0,kIOReturnUnsupported=1,kIOReturnNotPermitted=2,kIOReturnNotReady=3,kIOReturnTimeout=4,kIOReturnNoMemory=5,kIOReturnBadArgument=6,kIOReturnBusy=7,kIOReturnError=8};
void IOLog(const char *,...);
''','mach/kmod.h':'#pragma once\nstruct kmod_info_t{const char *name;};\n',
'pexpert/pexpert.h':'#pragma once\nusing boolean_t=int;\nboolean_t PE_parse_boot_argn(const char *,void *,int);\n'}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--derived',type=Path,required=True);p.add_argument('--wire',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);text=(a.derived/'NativeGOPPresenter.cpp').read_text()
 names=['bool NativeGOPPresenter::offer(','IOReturn NativeGOPPresenter::waitSwap(','IOReturn NativeGOPPresenter::completeSwap(','void NativeGOPPresenter::abandonSwap(','void NativeGOPPresenter::dropKept(','void NativeGOPPresenter::noteFences(']
 methods='\n'.join(method(text,n) for n in names);(out/'ActualFrameMethods.inc').write_text(methods+'\n')
 glue_text=(a.derived/'EldenDisplayGlue.cpp').read_text()
 gnames=['bool native_legacy_gpu_offer(','void native_legacy_gpu_fallback_observed(','void native_legacy_copy_timing_observed(','void native_legacy_swap_validation_observed(']
 glue='\n'.join(method(glue_text,n) for n in gnames);(out/'ActualGlueMethods.inc').write_text(glue+'\n')
 stubs=out/'stubs'
 for rel,body in STUBS.items():
  target=stubs/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(body)
 cmd=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(out),'-I',str(a.wire),str(ROOT/'drivers/EldenGpu/TestFrame.cpp'),'-o',str(out/'frame-test')]
 r=subprocess.run(cmd,capture_output=True);(out/'build.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
 r=subprocess.run([str(out/'frame-test')],capture_output=True);(out/'run.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
 frame_out=r.stdout.decode()
 cmd=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(out),'-I',str(stubs),'-I',str(a.wire),'-I',str(ROOT/'drivers/EldenGpu'),str(ROOT/'drivers/EldenGpu/tests/test_frame_diag.cpp'),str(ROOT/'drivers/EldenGpu/FrameBridge.cpp'),'-o',str(out/'frame-diag-test')]
 r=subprocess.run(cmd,capture_output=True);(out/'build-diag.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
 r=subprocess.run([str(out/'frame-diag-test')],capture_output=True);(out/'run-diag.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
 diag_out=r.stdout.decode()
 (out/'result.json').write_text(json.dumps({'passed':True,'actual_methods':names,'methods_sha256':hashlib.sha256(methods.encode()).hexdigest(),'glue_functions':gnames,'glue_sha256':hashlib.sha256(glue.encode()).hexdigest(),'output':frame_out+diag_out,'hardware':False},indent=2)+'\n');print(frame_out+diag_out)
if __name__=='__main__':main()
