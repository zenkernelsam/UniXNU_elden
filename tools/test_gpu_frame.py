"""Compile selected actual presenter lifecycle methods against object peers."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
def method(text,signature):
 if text.count(signature)!=1:raise ValueError('Method identity changed')
 start=text.index(signature);at=text.index('{',start)+1;depth=1
 while depth:
  depth+=(text[at]=='{')-(text[at]=='}');at+=1
 return text[start:at]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--derived',type=Path,required=True);p.add_argument('--wire',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);text=(a.derived/'NativeGOPPresenter.cpp').read_text()
 names=['bool NativeGOPPresenter::offer(','IOReturn NativeGOPPresenter::waitSwap(','IOReturn NativeGOPPresenter::completeSwap(','void NativeGOPPresenter::abandonSwap(','void NativeGOPPresenter::dropKept(']
 methods='\n'.join(method(text,n) for n in names);(out/'ActualFrameMethods.inc').write_text(methods+'\n')
 cmd=['xcrun','clang++','-std=c++17','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(out),'-I',str(a.wire),str(ROOT/'drivers/EldenGpu/TestFrame.cpp'),'-o',str(out/'frame-test')]
 r=subprocess.run(cmd,capture_output=True);(out/'build.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
 r=subprocess.run([str(out/'frame-test')],capture_output=True);(out/'run.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
 (out/'result.json').write_text(json.dumps({'passed':True,'actual_methods':names,'methods_sha256':hashlib.sha256(methods.encode()).hexdigest(),'output':r.stdout.decode(),'hardware':False},indent=2)+'\n');print(r.stdout.decode())
if __name__=='__main__':main()
