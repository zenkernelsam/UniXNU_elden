"""Generate the hash-bound Elden runtime and gated publisher; no device IO."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenGpu'
sys.path.insert(0,str(DRIVER));from RuntimeSource import board,publisher,memory
from FrameSource import presenter,display
from SubmitSource import derive as submit_derive
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tree',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 (out/'EldenGpuRuntime.cpp').write_bytes(board())
 original=a.tree/'drivers/Adreno740/NativeAdrenoPublish.cpp';(out/'NativeAdrenoPublish.cpp').write_bytes(publisher(original.read_bytes()))
 (out/'NativeAdrenoMemory.cpp').write_bytes(memory((a.tree/'drivers/Adreno740/NativeAdrenoMemory.cpp').read_bytes()))
 pin=json.loads((DRIVER/'upstream/submit-source.json').read_text())
 submit_cpp_src=(a.tree/'drivers/Adreno740/NativeAdrenoSubmit.cpp').read_bytes()
 submit_h_src=(a.tree/'drivers/Adreno740/NativeAdrenoSubmit.h').read_bytes()
 for name,data in (('NativeAdrenoSubmit.cpp',submit_cpp_src),('NativeAdrenoSubmit.h',submit_h_src)):
  if hashlib.sha256(data).hexdigest()!=pin['files'][name]:raise ValueError('Submit source differs from the derivation pin: '+name)
 submit_cpp,submit_h=submit_derive(submit_cpp_src,submit_h_src)
 (out/'NativeAdrenoSubmit.cpp').write_bytes(submit_cpp);(out/'NativeAdrenoSubmit.h').write_bytes(submit_h)
 for name in ('Runtime.h','CallGate.h','EldenGpuVotes.c','EldenGpuVotes.h','TestCallGate.cpp','TestQuiesce.cpp','PublisherQuiesce.inc','FrameBridge.cpp','FrameBridge.h'):shutil.copyfile(DRIVER/name,out/name)
 (out/'NativeGOPPresenter.cpp').write_bytes(presenter((a.tree/'drivers/GopFramebuffer/NativeGOPPresenter.cpp').read_bytes()))
 (out/'EldenDisplayGlue.cpp').write_bytes(display((ROOT/'profiles/v94-fbwc-observation/sources/EldenDisplayGlue.cpp').read_bytes()))
 shutil.copyfile(DRIVER/'upstream/Sm8750Rpmh.h',out/'Sm8750Rpmh.h')
 hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()}
 (out/'source.json').write_text(json.dumps({'generated_sources':hashes,'publisher_before':hashlib.sha256(original.read_bytes()).hexdigest(),
  'submit_cpp_before':pin['files']['NativeAdrenoSubmit.cpp'],'submit_h_before':pin['files']['NativeAdrenoSubmit.h'],
  'submit_donor':pin['donor']['commit'],'compiled':False,'hardware':False},indent=2)+'\n')
 print('Generated Elden runtime and bounded publisher gate; build qualification still required')
if __name__=='__main__':main()
