"""Prepare the actual camera capture session source manifest."""
from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];CAM=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve()
    # The frame manifest already carries the exact actual header set the
    # session compiles against; add the session source and rebind every sha.
    subprocess.run([sys.executable,str(ROOT/'tools'/'prepare_camera_frames.py'),'--output',str(out)],check=True)
    for name in ('CaptureSession.h','CaptureSession.c'):shutil.copyfile(CAM/name,out/name)
    record=json.loads((out/'source.json').read_text())
    record['sources']={f.name:sha(f) for f in out.iterdir() if f.is_file() and f.name!='source.json'}
    record['actual_camera_capture_session_source']=True
    record['linked_native_image']=False
    (out/'source.json').write_text(json.dumps(record,indent=2)+'\n',newline='\n')
    print('Prepared original camera capture session source manifest')
if __name__=='__main__':main()
