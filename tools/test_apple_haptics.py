"""Test real Apple asset import, exact timing/parameters and calibrated sample code.

Apple assets must be the previously hash-bound local OS17 extraction; output
stores only verdicts and hashes. This does not ship raw patterns or make hardware
calibration, waveform equivalence or physical-output claims.
"""
from pathlib import Path
import argparse,hashlib,json,plistlib,subprocess,sys
from decimal import Decimal
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'drivers/EldenHaptics'))
from AppleEffects import ahap,vibration,json_text,q15,micros
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--assets',type=Path,required=True);p.add_argument('--llvm',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);verdicts={};cases=0
    def check(value):
        nonlocal cases
        cases+=1
        if not value:raise ValueError('Importer assertion '+str(cases))
    table=a.assets/'D83-SystemSoundVibrationPatterns.plist';table_hash='f50d5002f808bc85cb5bb284b6ef9d5928703a21738122b23686e1ab5c05910c'
    check(sha(table)==table_hash);original=plistlib.loads(table.read_bytes());imported=vibration(table.read_bytes())
    check(len(imported['recipes'])==142 and not imported['rejected'])
    for name,source in original.items():
        converted=imported['recipes'][name]
        if 'Atoms' not in source:continue
        check(len(converted['atoms'])==len(source['Atoms']))
        for before,after in zip(source['Atoms'],converted['atoms']):
            check(before['Type']==after['kind']);check(micros(before['StartTime'])==after['time_us'])
            check(after['intensity_q15']==q15(before.get('Amplitude',before.get('Ampliude',1))))
            if 'Frequency' in before:check(Decimal(after['frequency_hz'])==Decimal(str(before['Frequency'])))
            if 'Envelope' in before:
                check(len(after['envelope'])==len(before['Envelope']))
                for b,c in zip(before['Envelope'],after['envelope']):
                    check(Decimal(c['time_raw'])==Decimal(str(b['Time'])) and c['level_q15']==q15(b['Level']))
    verdicts['vibration_table']={'sha256':table_hash,'recipes':len(imported['recipes']),'atoms':sum(len(v.get('atoms',[])) for v in imported['recipes'].values())}
    inventory=json.loads((a.assets/'assets.json').read_text());check(inventory['D83_table_sha256']==table_hash)
    for item in inventory['assets']:
        if not item['path'].endswith('.ahap'):continue
        path=a.assets/'assets'/item['path'];check(sha(path)==item['sha256'])
        raw=path.read_bytes();before,removed=json_text(raw);converted=ahap(raw)
        expected=[e['Event'] for e in before['Pattern'] if 'Event' in e and e['Event']['EventType'].startswith('Haptic')]
        check(len(expected)==len(converted['events']))
        for source,event in zip(expected,converted['events']):
            check(source['EventType']==event['kind'] and micros(source.get('Time',0))==event['time_us'])
            for value in source.get('EventParameters',[]):
                target='intensity_q15' if value['ParameterID']=='HapticIntensity' else 'sharpness_q15'
                check(event[target]==q15(value['ParameterValue']))
        verdicts[Path(item['path']).name]={'sha256':item['sha256'],'events':len(expected),'audio_sidecars':len(converted['audio_sidecars']),
                                       'trailing_commas':removed}
    # Meaningful negative fixtures: duplicate/nonfinite/unknown/unsafe patterns
    # must not silently turn into a click or a guessed calibrated waveform.
    rejected=[b'{"Version":1,"Version":1,"Pattern":[]}',b'{"Version":1,"Pattern":[{"Event":{"EventType":"HapticTransient","Time":NaN}}]}',
              b'{"Version":1,"Pattern":[{"Event":{"EventType":"Unknown","Time":0}}]}',
              b'{"Version":1,"Pattern":[{"Event":{"EventType":"HapticContinuous","Time":0}}]}',
              b'{"Version":1,"Pattern":[{"Event":{"EventType":"HapticTransient","EventParameters":[{"ParameterID":"HapticIntensity","ParameterValue":2}]}}]}']
    for data in rejected:
        try:ahap(data)
        except ValueError:check(True)
        else:raise ValueError('Malformed pattern accepted')
    accepted=ahap(b'{"Version":1,"Metadata":{"note":"bracket ,} stays"},"Pattern":[{"Event":{"EventType":"HapticTransient","Time":0,"EventParameters":[],},},],}')
    check(accepted['trailing_commas_removed']==4 and len(accepted['events'])==1)
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(name+' failed; logs retained')
        return r.stdout.decode()
    driver=ROOT/'drivers/EldenHaptics';exe=out/'sample-test'
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',driver/'SampleMap.c',driver/'TestSampleMap.c','-o',exe],'sample-build')
    sample=run([exe],'sample-run');resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'SampleMap.o'
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
         '-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror','-c',driver/'SampleMap.c','-o',obj],'sample-native')
    (out/'result.json').write_text(json.dumps({'passed':True,'import_checks':cases,'assets':verdicts,'sample_result':sample,'sample_object_sha256':sha(obj),
        'sources':{p.name:sha(p) for p in [driver/'AppleEffects.py',driver/'SampleMap.c',driver/'SampleMap.h']},
        'official_source_semantics_preserved':True,'calibration_peer_only':True,'motor_output':False,'hardware':False},indent=2)+'\n')
    print(json.dumps({'passed':True,'asset_count':len(verdicts),'import_checks':cases,'sample_result':sample}))
if __name__=='__main__':main()
