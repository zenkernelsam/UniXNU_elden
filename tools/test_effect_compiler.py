"""Exercise original Apple recipes with explicitly synthetic, non-delivered
template/calibration peers and the actual C signed-sample mapper.
"""
from pathlib import Path
from array import array
import argparse,hashlib,json,math,subprocess,sys,plistlib
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenHaptics';sys.path.insert(0,str(DRIVER))
from AppleEffects import vibration,ahap
from EffectCompiler import load_bank,compile_vibration,compile_ahap,render_atoms,render_vibration
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--vibration',type=Path,required=True);p.add_argument('--ahap-dir',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);fixtures=out/'synthetic-peer';fixtures.mkdir()
    parsed=vibration(a.vibration.read_bytes());raw=plistlib.loads(a.vibration.read_bytes());types={atom['Type'] for row in raw.values() for atom in row.get('Atoms',[]) if atom['Type']!='Silence'}
    pcm=array('h',[int(12000*math.sin(i*2*math.pi/24)) for i in range(24)]);data=pcm.tobytes()
    carrier=fixtures/'fixture.pcm';carrier.write_bytes(data)
    templates={name:{'path':carrier.name,'sha256':sha(carrier),'format':'s16le','loop_frames':24,'tail_frames':0,'envelope_mode':'normalized'} for name in types}
    templates['legacy-peer']={**next(iter(templates.values()))}
    for row in parsed['recipes'].values():
        for atom in row.get('atoms',[]):
            if atom['kind']=='Silence':continue
            for key in ['frequency_hz','starting_gain_q15']:
                if key in atom:templates[atom['kind']][key]=atom[key]
    channel={'gain':16,'drive_peak_q15':8192,'points':[[0,0],[32767,8192]],'window_frames':24000,
             'max_window_squares':1<<60,'max_window_on_frames':24000}
    bank={'schema':1,'board':'TB322FC','rate_hz':24000,'boost_mv':7000,'firmware_sha256':'84598d2a5130f7fe6bccba5cfbcafa1341d2d5bfe2f7d1361142cb78783ce79d',
        'hardware_qualified':False,'templates':templates,'channels':[channel,channel],'legacy_on_template':'legacy-peer',
        'legacy_repeat_order':'on-off','ahap_dynamic_policy':'intensity-multiply/sharpness-add-clamp',
        'ahap_templates':{k:[{'sharpness_q15':0,'template':'legacy-peer'},{'sharpness_q15':32767,'template':'legacy-peer'}] for k in ['HapticTransient','HapticContinuous']}}
    config=fixtures/'bank.json';config.write_text(json.dumps(bank,indent=2)+'\n');bank=load_bank(config,allow_test=True)
    try:load_bank(config)
    except ValueError:pass
    else:raise ValueError('Synthetic bank was accepted for delivery')
    duplicate=fixtures/'duplicate.json';duplicate.write_text(config.read_text().replace('"schema": 1','"schema": 1, "schema": 1',1))
    try:load_bank(duplicate,allow_test=True)
    except ValueError:pass
    else:raise ValueError('Duplicate calibration keys accepted')
    library=out/'libSampleMap.dylib'
    cmd=['xcrun','clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',str(DRIVER/'SampleMap.c'),'-o',str(library)]
    run=subprocess.run(cmd,capture_output=True);(out/'mapper-build.log').write_bytes(run.stdout+run.stderr);run.check_returncode()
    summaries={};rejected={}
    for name in parsed['recipes']:
        try:report,outputs=compile_vibration(name,parsed,bank,library)
        except ValueError as e:rejected[name]=str(e);continue
        if report['hardware_qualified']:raise ValueError('Fixture mislabeled as real qualification')
        summaries[name]={'frames':report['frames'],'sha256':[hashlib.sha256(v).hexdigest() for v in outputs]}
    ahap_summaries={}
    for path in sorted(a.ahap_dir.glob('*.ahap')):
        report,outputs=compile_ahap(ahap(path.read_bytes()),bank,library)
        ahap_summaries[path.name]={'frames':report['frames'],'source_sha256':report['source_sha256'],'audio_sidecars_executed':report['audio_sidecars_executed']}
    # Direct timing/loop/envelope and clipping policy checks independent of C
    # mapping; these synthetic PCM outputs are never installed or delivered.
    atom={'kind':'legacy-peer','time_us':0,'duration_us':10000,'intensity_q15':32767,'master_q15':32767}
    rendered=render_atoms({'atoms':[atom]},bank)
    if len(rendered)!=240 or list(rendered[:24])!=list(pcm):raise ValueError('Explicit carrier/loop timing changed')
    envelope={**atom,'envelope':[{'time_raw':'0','level_q15':32767},{'time_raw':'1','level_q15':0}]}
    ramp=render_atoms({'atoms':[envelope]},bank)
    if abs(ramp[-2])>=abs(rendered[-2]):raise ValueError('Normalized source envelope not preserved')
    bad={**atom,'kind':'missing-template'}
    try:render_atoms({'atoms':[bad]},bank)
    except ValueError:pass
    else:raise ValueError('Unknown type received a guessed carrier')
    result={'passed':True,'original_source_sha256':sha(a.vibration),'compiled_recipe_count':len(summaries),'rejected_recipes':rejected,
        'ahap_count':len(ahap_summaries),'original_template_types':sorted(types),'actual_C_mapper_sha256':sha(library),
        'synthetic_template_and_gain_peers':True,'production_bank_refused':True,'hardware':False,'native_delivery':False,
        'recipes':summaries,'ahap':ahap_summaries}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ['recipes','ahap','original_template_types']}))
if __name__=='__main__':main()
