"""Host qualification for tools/build_wave_manifest.py.

Builds a synthetic test-only bank + small Apple source fixtures, runs the
deterministic manifest builder twice (byte-identical output required), proves
production mode refuses the unqualified bank, compiles the generated C with
WaveCompiler/WaveFeed/SampleMap under ASan+UBSan/-Werror, drains every source
entry through both the compiler and the staged->confirmed feed, and emits a
scalar arm64 object of the generated manifest. Synthetic only: no hardware.
"""
from array import array
from pathlib import Path
import argparse,hashlib,json,plistlib,subprocess,re,sys
ROOT=Path(__file__).resolve().parents[1];DRV=ROOT/'drivers/EldenHaptics';TOOLS=ROOT/'tools'
FW='84598d2a5130f7fe6bccba5cfbcafa1341d2d5bfe2f7d1361142cb78783ce79d'
def sha(b):return hashlib.sha256(b).hexdigest()
def s16(vals):
    a=array('h',vals)
    if sys.byteorder!='little':a.byteswap()
    return a.tobytes()
def fixtures(base):
    base.mkdir(parents=True)
    tap=s16([(i*37)%2000-1000 for i in range(480)])
    loop=s16([(i*29)%4000-2000 for i in range(960)])
    hi=s16([(i*53)%3000-1500 for i in range(480)])
    bank={'schema':1,'rate_hz':24000,'board':'TB322FC','boost_mv':7000,'firmware_sha256':FW,
        'hardware_qualified':False,'legacy_on_template':'Loop','legacy_repeat_order':'on-off',
        'ahap_dynamic_policy':'intensity-multiply/sharpness-add-clamp',
        'templates':{
            'Tap':{'path':'tap.s16le','sha256':sha(tap),'format':'s16le','loop_frames':0,'tail_frames':0,'envelope_mode':'seconds'},
            'Loop':{'path':'loop.s16le','sha256':sha(loop),'format':'s16le','loop_frames':480,'tail_frames':480,'envelope_mode':'seconds'},
            'AhapLo':{'path':'tap.s16le','sha256':sha(tap),'format':'s16le','loop_frames':0,'tail_frames':0,'envelope_mode':'seconds'},
            'AhapHi':{'path':'hi.s16le','sha256':sha(hi),'format':'s16le','loop_frames':0,'tail_frames':0,'envelope_mode':'seconds'}},
        'ahap_templates':{
            'HapticTransient':[{'sharpness_q15':0,'template':'Tap'}],
            'HapticContinuous':[{'sharpness_q15':0,'template':'AhapLo'},
                                {'sharpness_q15':32767,'template':'AhapHi'}]},
        'channels':[{'gain':64,'drive_peak_q15':32767,'points':[[0,0],[32767,32767]],
                     'window_frames':480,'max_window_squares':1<<50,'max_window_on_frames':480}]*2}
    (base/'bank').mkdir();(base/'bank/tap.s16le').write_bytes(tap)
    (base/'bank/loop.s16le').write_bytes(loop);(base/'bank/hi.s16le').write_bytes(hi)
    (base/'bank/bank.json').write_text(json.dumps(bank,indent=2)+'\n')
    (base/'patterns.plist').write_bytes(plistlib.dumps({
        'AtomsRecipe':{'Amplitude':0.8,'Atoms':[{'Type':'Tap','StartTime':0.0,'Amplitude':1.0,'Duration':0.02}]},
        'OnOffRecipe':{'Intensity':0.5,'VibePattern':[True,25.0,False,10.0]},
        'RepeatingRecipe':{'Intensity':0.5,'OffDuration':0.02,'OnDuration':0.02,'TotalDuration':0.1},
        'EmptyRecipe':{}}))
    (base/'effect.ahap').write_text(json.dumps({'Version':1,'Pattern':[
        {'Event':{'Time':0.0,'EventType':'HapticTransient','EventParameters':[
            {'ParameterID':'HapticIntensity','ParameterValue':1.0},
            {'ParameterID':'HapticSharpness','ParameterValue':0.0}]}},
        {'Event':{'Time':0.02,'EventType':'HapticContinuous','EventDuration':0.02,'EventParameters':[
            {'ParameterID':'HapticIntensity','ParameterValue':0.8},
            {'ParameterID':'HapticSharpness','ParameterValue':0.5}]}},
        {'Parameter':{'ParameterID':'HapticIntensityControl','Time':0.01,'ParameterValue':0.5}},
        {'ParameterCurve':{'ParameterID':'HapticSharpnessControl','Time':0.0,
            'ParameterCurveControlPoints':[{'Time':0.0,'ParameterValue':0.0},
                                           {'Time':0.01,'ParameterValue':0.3}]}}]})+'\n')
    (base/'empty.plist').write_bytes(plistlib.dumps({'EmptyRecipe':{}}))
    (base/'collapse.ahap').write_text(json.dumps({'Version':1,'Pattern':[
        {'Event':{'Time':0.0,'EventType':'HapticTransient','EventParameters':[]}},
        {'ParameterCurve':{'ParameterID':'HapticIntensityControl','Time':0.0,
            'ParameterCurveControlPoints':[{'Time':0.0,'ParameterValue':0.0},
                                           {'Time':0.000001,'ParameterValue':0.5}]}}]})+'\n')
    (base/'late.ahap').write_text(json.dumps({'Version':1,'Pattern':[
        {'Event':{'Time':59.9999,'EventType':'HapticContinuous','EventDuration':0.01,'EventParameters':[]}}]})+'\n')
    (base/'boundary.ahap').write_text(json.dumps({'Version':1,'Pattern':[
        {'Event':{'Time':59.98,'EventType':'HapticContinuous','EventDuration':0.02,'EventParameters':[]}}]})+'\n')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',nargs='+',default=None)
    for key in ('llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    cc=a.cc or (['xcrun','clang'] if sys.platform=='darwin' else ['gcc'])
    work=out/'fixture';fixtures(work)
    def run(cmd,label,expect=0):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode!=expect:raise RuntimeError(label+' failed rc=%d; inspect retained log'%r.returncode)
        return (r.stdout+r.stderr).decode()
    build=[sys.executable,str(TOOLS/'build_wave_manifest.py'),'--source',str(work/'patterns.plist'),
           '--source',str(work/'effect.ahap'),'--bank',str(work/'bank/bank.json')]
    # 1. Test-only build twice: byte-identical generated output (determinism).
    run(build+['--test-only','--output',out/'gen1'],'build1')
    run(build+['--test-only','--output',out/'gen2'],'build2')
    for name in ('EldenWaveManifest.c','EldenWaveManifest.h','manifest.json'):
        if (out/'gen1'/name).read_bytes()!=(out/'gen2'/name).read_bytes():raise ValueError('Non-deterministic '+name)
    report=json.loads((out/'gen1/manifest.json').read_text())
    if report['production_ready'] or report['hardware_qualified'] or report['hardware']:
        raise ValueError('Test-only manifest must stay production_ready:false')
    if len(report['entries'])!=4 or {e['name'] for e in report['entries']}!={'AtomsRecipe','OnOffRecipe','RepeatingRecipe','effect'}:
        raise ValueError('Manifest entries differ')
    ids=[e['id'] for e in report['entries']]
    if ids!=sorted(ids) or any(not i or i&0x80000000 for i in ids):raise ValueError('Manifest id contract')
    if report['bank_sha256']!=sha((work/'bank/bank.json').read_bytes()):raise ValueError('Bank identity')
    head=(out/'gen1/EldenWaveManifest.c').read_text().split('*/')[0]
    if 'source_sha256:' not in head or 'bank_sha256:' not in head or 'production_ready: false' not in head:
        raise ValueError('Shas/production_ready missing from generated header')
    # 2. Production mode must refuse the unqualified synthetic bank.
    run(build+['--output',out/'genp'],'production-reject',expect=1)
    # 2b. A source set yielding no supported non-empty recipes refuses (no zero-entry manifest).
    run([sys.executable,str(TOOLS/'build_wave_manifest.py'),'--source',str(work/'empty.plist'),
         '--bank',str(work/'bank/bank.json'),'--test-only','--output',out/'genempty'],
        'empty-manifest-reject',expect=1)
    # 2c. Distinct AHAP point times collapsing to one 24 kHz frame refuse.
    run([sys.executable,str(TOOLS/'build_wave_manifest.py'),'--source',str(work/'collapse.ahap'),
         '--bank',str(work/'bank/bank.json'),'--test-only','--output',out/'gencollapse'],
        'collapse-points-reject',expect=1)
    # 2d. An event whose quantized end crosses the 60s compiled boundary refuses
    #     before any output is written.
    run([sys.executable,str(TOOLS/'build_wave_manifest.py'),'--source',str(work/'late.ahap'),
         '--bank',str(work/'bank/bank.json'),'--test-only','--output',out/'genlate'],
        'boundary-reject',expect=1)
    if (out/'genlate').exists():raise ValueError('Rejected build still wrote output')
    # 2e. An event ending exactly at the 60s boundary stays accepted.
    run([sys.executable,str(TOOLS/'build_wave_manifest.py'),'--source',str(work/'boundary.ahap'),
         '--bank',str(work/'bank/bank.json'),'--test-only','--output',out/'genboundary'],
        'boundary-accept')
    if json.loads((out/'genboundary/manifest.json').read_text())['entries'][0]['total_frames']!=1440000:
        raise ValueError('Boundary recipe frames differ')
    # 3. Compile + drain the generated manifest through WaveCompiler/WaveFeed.
    gen=out/'gen1'
    flags=[*cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
           '-DEXPECTED_SOURCES=4','-DEXPECTED_CARRIERS=3','-I',gen,'-I',DRV]
    tus=[gen/'EldenWaveManifest.c',DRV/'WaveCompiler.c',DRV/'WaveFeed.c',DRV/'SampleMap.c',DRV/'TestWaveManifest.c']
    run([*flags,*tus,'-o',out/'test'],'host-build')
    msg=run([out/'test'],'host-run')
    if 'PASS' not in msg:raise RuntimeError('Generated manifest did not qualify')
    # 4. Scalar arm64 object of the generated manifest TU.
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip()
    obj=out/'EldenWaveManifest.o'
    run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin',
         '-fno-stack-protector','-mgeneral-regs-only','-nostdinc','-isystem',Path(resource)/'include',
         '-Wno-error=incompatible-sysroot','-Wall','-Wextra','-Werror','-I',gen,'-I',DRV,
         '-c',gen/'EldenWaveManifest.c','-o',obj],'manifest-native')
    dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'manifest-asm')
    ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
    if any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar code')
    result={'passed':True,'host_output':msg,'objects':{'EldenWaveManifest.o':sha(obj.read_bytes())},
        'builder_sha256':sha((TOOLS/'build_wave_manifest.py').read_bytes()),
        'test_sha256':sha((DRV/'TestWaveManifest.c').read_bytes()),
        'manifest_c_sha256':sha((gen/'EldenWaveManifest.c').read_bytes()),
        'sources':4,'carriers_deduplicated':3,'deterministic':True,'production_reject':True,
        'empty_manifest_reject':True,'collapse_points_reject':True,
        'boundary_reject':True,'boundary_accept':True,
        'production_ready':False,'native_bound':False,'hardware':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(msg)
if __name__=='__main__':main()
