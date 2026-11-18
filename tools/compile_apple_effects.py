"""Compile local original Apple effects using measured source-bound motor inputs.
No guessed templates, calibration bypass, hardware access or native installation.
"""
from pathlib import Path
import argparse,hashlib,json,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'drivers/EldenHaptics'))
from AppleEffects import vibration,ahap
from EffectCompiler import load_bank,compile_vibration,compile_ahap
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--bank',type=Path,required=True)
    p.add_argument('--sample-map-library',type=Path,required=True);p.add_argument('--recipe');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    bank=load_bank(a.bank);raw=a.source.read_bytes()
    if a.source.suffix.lower()=='.ahap':report,pcm=compile_ahap(ahap(raw),bank,a.sample_map_library)
    else:
        if not a.recipe:raise ValueError('Select original named recipe')
        report,pcm=compile_vibration(a.recipe,vibration(raw),bank,a.sample_map_library)
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    for m,data in enumerate(pcm):(out/('motor'+str(m)+'.rtp')).write_bytes(data)
    report['source_path']=str(a.source.resolve());report['sample_mapper_sha256']=hashlib.sha256(a.sample_map_library.read_bytes()).hexdigest()
    report['compiler_source_sha256']=hashlib.sha256((ROOT/'drivers/EldenHaptics/EffectCompiler.py').read_bytes()).hexdigest()
    report['sample_map_source_sha256']=hashlib.sha256((ROOT/'drivers/EldenHaptics/SampleMap.c').read_bytes()).hexdigest()
    report['parser_source_sha256']=hashlib.sha256((ROOT/'drivers/EldenHaptics/AppleEffects.py').read_bytes()).hexdigest()
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Compiled source-bound dual motor effects; native delivery and actual calibrated hardware still require mainline acceptance')
if __name__=='__main__':main()
