"""Generate the hash-bound Elden runtime and gated publisher; no device IO."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenGpu'
sys.path.insert(0,str(DRIVER));from RuntimeSource import board,publisher,memory
from FrameSource import presenter,display
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tree',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 (out/'EldenGpuRuntime.cpp').write_bytes(board())
 original=a.tree/'drivers/Adreno740/NativeAdrenoPublish.cpp';(out/'NativeAdrenoPublish.cpp').write_bytes(publisher(original.read_bytes()))
 (out/'NativeAdrenoMemory.cpp').write_bytes(memory((a.tree/'drivers/Adreno740/NativeAdrenoMemory.cpp').read_bytes()))
 for name in ('Runtime.h','CallGate.h','EldenGpuVotes.c','EldenGpuVotes.h','TestCallGate.cpp','TestQuiesce.cpp','PublisherQuiesce.inc','FrameBridge.cpp','FrameBridge.h'):shutil.copyfile(DRIVER/name,out/name)
 (out/'NativeGOPPresenter.cpp').write_bytes(presenter((a.tree/'drivers/GopFramebuffer/NativeGOPPresenter.cpp').read_bytes()))
 (out/'EldenDisplayGlue.cpp').write_bytes(display((ROOT/'profiles/v94-fbwc-observation/sources/EldenDisplayGlue.cpp').read_bytes()))
 shutil.copyfile(DRIVER/'upstream/Sm8750Rpmh.h',out/'Sm8750Rpmh.h')
 hashes={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in out.iterdir() if p.is_file()}
 (out/'source.json').write_text(json.dumps({'generated_sources':hashes,'publisher_before':hashlib.sha256(original.read_bytes()).hexdigest(),'compiled':False,'hardware':False},indent=2)+'\n')
 print('Generated Elden runtime and bounded publisher gate; build qualification still required')
if __name__=='__main__':main()
