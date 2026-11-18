"""Test the actual mailbox and its real queue/GENI/AW composition, no tablet IO."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenHaptics'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','firmware','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    r=json.loads((a.source/'source.json').read_text());firmware=r['upstream']['firmware']
    if a.firmware.stat().st_size!=firmware['bytes'] or sha(a.firmware)!=firmware['sha256']:raise ValueError('Original firmware differs')
    for name,digest in r['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Prepared source differs')
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    flags=['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source/'haptics']
    run([*flags,a.source/'haptics/Mailbox.c',DRIVER/'TestMailbox.c','-o',out/'mailbox'],'mailbox-build');m=run([out/'mailbox'],'mailbox-run')
    files=[a.source/'haptics'/n for n in ['Mailbox.c','Queue.c','GeniCore.c','Bus.c','AwChip.c']]
    run([*flags,*files,DRIVER/'TestDelivery.c','-o',out/'delivery'],'delivery-build');d=run([out/'delivery',a.firmware],'delivery-run')
    (out/'result.json').write_text(json.dumps({'passed':True,'mailbox_output':m,'composition_output':d,'source_record_sha256':sha(a.source/'source.json'),
        'native_worker_or_IOKit_modeled_by_test':False,'actual_portable_queue_bus_aw_composed':True,'hardware_modeled':True,
        'gain_limits_synthetic_not_delivered':True,'hardware':False},indent=2)+'\n');print(m+d)
if __name__=='__main__':main()
