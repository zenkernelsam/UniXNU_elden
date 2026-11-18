"""Import local Apple AHAP/plist semantics; raw assets stay external.
Gain/frequency/waveform templates are deliberately not guessed here.
"""
from pathlib import Path
import argparse,hashlib,json,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'drivers/EldenHaptics'))
from AppleEffects import ahap,vibration
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True)
    p.add_argument('--expected-sha256',required=True);p.add_argument('--format',choices=('ahap','vibration'),required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    data=a.source.read_bytes()
    if hashlib.sha256(data).hexdigest()!=a.expected_sha256:raise ValueError('Apple source identity differs')
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);result=(ahap if a.format=='ahap' else vibration)(data)
    (out/'effects.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({'format':result['format'],'recipes':len(result.get('recipes',{})),'rejected':len(result.get('rejected',{})),
                      'haptic_events':len(result.get('events',[])),'audio_sidecars':len(result.get('audio_sidecars',[])),
                      'motor_output':False}))
if __name__=='__main__':main()
