"""Validate native catalog generation using explicitly synthetic host fixtures.

Positive schema fixtures claim measurement only to exercise the validation
format. They are NOT native build/candidate inputs or a hardware certificate;
the resulting headers are used solely in a host C++ structural test executable.
"""
from pathlib import Path
from array import array
import argparse,copy,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenHaptics'
sys.path.insert(0,str(DRIVER));sys.path.insert(0,str(ROOT/'tools'))
from NativeInputs import catalog_header,actuator_header,sha,shared_header,FIRMWARE
from prepare_haptics_native import calibration
from EffectCompiler import load_bank,compile_vibration
from AppleEffects import vibration
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);fixture=out/'synthetic-host-only';fixture.mkdir()
    evidence=fixture/'measurement-peer.txt';evidence.write_text('Synthetic host schema peer; NOT a measured motor or bandwidth input.\n')
    row={'path':evidence.name,'sha256':sha(evidence)}
    carrier=fixture/'carrier.s16le';carrier.write_bytes(array('h',[1024,-1024]*12).tobytes())
    template={'path':carrier.name,'sha256':sha(carrier),'format':'s16le','loop_frames':0,'tail_frames':0,'envelope_mode':'normalized'}
    channel={'gain':16,'drive_peak_q15':8192,'points':[[0,0],[32767,8192]],'window_frames':24000,'max_window_squares':1<<42,'max_window_on_frames':24000}
    bank={'schema':1,'board':'TB322FC','rate_hz':24000,'boost_mv':7000,'firmware_sha256':FIRMWARE,'hardware_qualified':True,
          'review_notes':'SYNTHETIC HOST FORMAT TEST ONLY','evidence':[row],'templates':{'Microtap':template},'channels':[channel,channel]}
    bank_path=fixture/'bank.json';bank_path.write_text(json.dumps(bank,indent=2)+'\n')
    source=fixture/'pattern.plist'
    import plistlib
    source.write_bytes(plistlib.dumps({'Peer':{'Atoms':[{'Type':'Microtap','Amplitude':1}]}}))
    library=out/'libSampleMap.dylib'
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; logs retained')
        return r.stdout.decode()
    run(['xcrun','clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-shared','-fPIC',DRIVER/'SampleMap.c','-o',library],'mapper')
    manifest,pcm=compile_vibration('Peer',vibration(source.read_bytes()),load_bank(bank_path),library)
    manifest['sample_mapper_sha256']=sha(library)
    for field,name in [('compiler_source_sha256','EffectCompiler.py'),('sample_map_source_sha256','SampleMap.c'),('parser_source_sha256','AppleEffects.py')]:manifest[field]=sha(DRIVER/name)
    manifest_path=fixture/'manifest.json';manifest_path.write_text(json.dumps(manifest,indent=2)+'\n')
    for m,data in enumerate(pcm):(fixture/('motor'+str(m)+'.rtp')).write_bytes(data)
    cal={'board':'TB322FC','chip_ids':[0x9370,0x9370],'firmware_sha256':FIRMWARE,'boost_mv':7000,'bus_hz':1000000,
         'hardware_qualified':True,'evidence':[row],'review_notes':'SYNTHETIC HOST FORMAT TEST ONLY','temperature_centi_c':[0,4000],
         'points':[{'intensity_q15':0,'gain':[0,0]},{'intensity_q15':32767,'gain':[16,16]}],
         'limits':{'max_gain':[16,16],'max_duration_us':20000,'max_late_us':1000000,'window_us':1000000,'max_on_us':[1000000,1000000],
                   'max_dual_on_us':1000000,'max_sum_gain':32,'square_budget':[1<<32,1<<32]},
         'shared_budget':{'window_us':1000000,'max_on_us':[1000000,1000000],'max_dual_on_us':1000000,'max_squares':[1<<42,1<<42]},
         'rtp':{'window_frames':24000,'max_duration_us':60000000,'max_service_gap_us':10000,'measured_min_payload_bps':60000,'measured_bus_hz':1000000,
                'bandwidth_evidence_sha256':sha(evidence),'max_sample':[127,127],'max_window_squares':[1<<40,1<<40],'max_window_on_frames':[24000,24000],'max_sum_gain':32}}
    cal_path=fixture/'calibration.json';cal_path.write_text(json.dumps(cal,indent=2)+'\n')
    cal['actuator']={'window_frames':24000,'max_on_frames':[24000,24000],'max_squares':[1<<40,1<<40],'gain':[16,16],
        'max_run_us':60000000,'max_gap_us':10000,'max_missing_frames':512,'silence_frames':64,
        'measured_min_payload_bps':60000,'max_startup_lateness_us':250000,'measured_bus_hz':1000000,
        'bandwidth_evidence_sha256':sha(evidence),'decimation':'box2-48k-to24k','evidence':[row],
        'review_notes':'SYNTHETIC HOST FORMAT TEST ONLY',
        'channels':[{'drive_peak_q15':8192,'points':[[0,0],[32767,8192]],'mapper_square_budget_q30':1<<42} for _ in range(2)]}
    cal_path.write_text(json.dumps(cal,indent=2)+'\n')
    catalog={'schema':1,'board':'TB322FC','firmware_sha256':FIRMWARE,'native_calibration_sha256':sha(cal_path),'hardware_qualified':True,
             'review_notes':'SYNTHETIC HOST FORMAT TEST ONLY','evidence':[row],
             'effects':[{'id':1,'name':'peer','manifest':manifest_path.name,'manifest_sha256':sha(manifest_path),'mask':3,'source_path':source.name,
                         'bank_path':bank_path.name,'sample_mapper_library':'../libSampleMap.dylib'}]}
    catalog_path=fixture/'catalog.json';catalog_path.write_text(json.dumps(catalog,indent=2)+'\n')
    native_header,record=catalog_header(catalog_path,cal_path);cal_header,cal_record=calibration(cal_path,FIRMWARE)
    act_header,act_record=actuator_header(cal_path);(fixture/'EldenActuatorCalibration.h').write_text(act_header)
    (fixture/'ActuatorLive.h').write_bytes((DRIVER/'actuator/Live.h').read_bytes());(fixture/'Abi.h').write_bytes((DRIVER/'actuator/Abi.h').read_bytes())
    (fixture/'EldenHapticsCatalog.h').write_text(native_header);(fixture/'EldenHapticsCalibration.h').write_text(cal_header)
    test=fixture/'TestInputs.cpp';test.write_text('''#include "EldenHapticsCatalog.h"
#include "EldenHapticsCalibration.h"
#include "EldenActuatorCalibration.h"
#include <cassert>
int main(){assert(ELDEN_HAPTICS_EFFECT_COUNT==1);assert(!elden_haptics_catalog_check(elden_haptics_effects,1,&elden_haptics_rtp_limits));
auto e=elden_haptics_catalog_find(elden_haptics_effects,1,1);assert(e&&e->frames==24&&e->gain[0]==16);assert(!elden_haptics_catalog_find(elden_haptics_effects,1,2));
EldenSharedBudget b={};assert(!elden_shared_budget_init(&b,&elden_haptics_shared_limits));
assert(ELDEN_HAPTICS_HAS_ACTUATOR&&elden_actuator_limits.gain[0]==16&&elden_actuator_limits.window_frames==24000);return 0;}
''')
    run(['xcrun','clang','-std=c11','-fsanitize=address,undefined','-I',DRIVER,'-c',DRIVER/'Catalog.c','-o',out/'catalog.o'],'catalog')
    run(['xcrun','clang','-std=c11','-fsanitize=address,undefined','-I',DRIVER,'-c',DRIVER/'Budget.c','-o',out/'budget.o'],'budget')
    run(['xcrun','clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',DRIVER,test,out/'catalog.o',out/'budget.o','-o',out/'inputs'],'headers')
    run([out/'inputs'],'headers-run');negative=[]
    def refuses(name,value=None,raw=None):
        path=fixture/('negative-'+name+'.json');path.write_text(raw if raw is not None else json.dumps(value))
        try:catalog_header(path,cal_path)
        except (ValueError,KeyError):negative.append(name)
        else:raise ValueError('Native input failed to reject '+name)
    for field,value in [('hardware_qualified',False),('native_calibration_sha256','0'*64),('firmware_sha256','0'*64),('evidence',[])]:
        bad=copy.deepcopy(catalog);bad[field]=value;refuses(field,bad)
    refuses('duplicates',raw=catalog_path.read_text().replace('"schema": 1','"schema": 1, "schema": 1',1))
    bad=copy.deepcopy(catalog);bad['effects'][0]['manifest_sha256']='0'*64;refuses('manifest',bad)
    original=(fixture/'motor0.rtp').read_bytes();(fixture/'motor0.rtp').write_bytes(b'\x7f'*len(original));refuses('pcm',catalog);(fixture/'motor0.rtp').write_bytes(original)
    original_manifest=manifest_path.read_bytes();forged=b'\x01'*len(original);(fixture/'motor0.rtp').write_bytes(forged)
    manifest['channels'][0]['pcm_sha256']=hashlib.sha256(forged).hexdigest();manifest_path.write_text(json.dumps(manifest));forged_catalog=copy.deepcopy(catalog)
    forged_catalog['effects'][0]['manifest_sha256']=sha(manifest_path);refuses('source-replay',forged_catalog)
    manifest_path.write_bytes(original_manifest);(fixture/'motor0.rtp').write_bytes(original)
    original_bank=bank_path.read_bytes();bank['hardware_qualified']=False;bank_path.write_text(json.dumps(bank));refuses('bank',catalog);bank_path.write_bytes(original_bank)
    default,record0=catalog_header(None,None)
    if record0 is not None or '#define ELDEN_HAPTICS_HAS_CATALOG 0' not in default:raise ValueError('Default output not disabled')
    actuator_negative=[]
    def actuator_refuses(name,value):
        path=fixture/('actuator-negative-'+name+'.json');path.write_text(json.dumps(value))
        try:actuator_header(path)
        except (ValueError,KeyError):actuator_negative.append(name)
        else:raise ValueError('Actuator input failed to reject '+name)
    for field,value in [('hardware_qualified',False),('board','TB321FU'),('firmware_sha256','0'*64),('boost_mv',8500),('chip_ids',[0x9270,0x9370])]:
        bad=copy.deepcopy(cal);bad[field]=value;actuator_refuses(field,bad)
    for field,value in [('window_frames',24001),('max_on_frames',[24001,24000]),('gain',[17,16]),('max_gap_us',10001),
                        ('measured_min_payload_bps',59999),('measured_bus_hz',400000),('decimation','linear-scale'),
                        ('bandwidth_evidence_sha256','0'*64),('evidence',[]),('max_squares',[1<<50,1<<40])]:
        bad=copy.deepcopy(cal);bad['actuator'][field]=value;actuator_refuses(field,bad)
    default_act,default_record=actuator_header(None)
    if default_record is not None or '#define ELDEN_HAPTICS_HAS_ACTUATOR 0' not in default_act:raise ValueError('Default Actuator not disabled')
    result={'passed':True,'catalog_negative_cases':negative,'default_catalog_disabled':True,'actual_C_catalog_budget_header_test':True,
            'actuator_negative_cases':actuator_negative,'default_actuator_disabled':True,'actual_Actuator_positive_schema_host_header_only':True,
            'positive_fixture_is_synthetic_host_only':True,'hardware_calibration_accepted':False,'native_build_from_positive_fixture':False,'tablet_access':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))
if __name__=='__main__':main()
