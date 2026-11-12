"""Run the official runtime fixture on the already prepared Peach source tree."""
import argparse,hashlib,json,os,sys,subprocess
from pathlib import Path
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True);p.add_argument('--resume-fixture-abi',action='store_true');a=p.parse_args()
    build=a.build.resolve();module=build/'component';test=module/'tools/test-mainline-runtime.py'
    output=module/'out/mainline-runtime-test'
    if output.exists() and not a.resume_fixture_abi:raise ValueError('New test output required')
    if a.resume_fixture_abi:
        failure=(build/'runtime-test.log').read_text()
        if (output/'report.json').exists() or 'pointer to incorrect function type' not in failure or 'config defined here' not in failure:
            raise ValueError('Resume only the recorded callback-ABI fixture failure')
    if not (module/'out/elden-prepared.json').exists():raise ValueError('Missing source preparation')
    before=sha(test);text=test.read_text()
    marker='subprocess.run(["bash", str(root / "tools/prepare.sh")], check=True)'
    if text.count(marker)!=1:raise ValueError('Unexpected upstream test recipe')
    # Reuse the identical source snapshot already audited for the native build.
    # The upstream fixture's own assertions/protocol code stay unchanged.
    text=text.replace(marker,'assert (root / "out/elden-prepared.json").is_file()')
    text=text.replace('ThreadPoolExecutor(8)','ThreadPoolExecutor(4)')
    name=test.with_name('test-elden-prepared-runtime.py')
    if name.exists() and (not a.resume_fixture_abi or name.read_text()!=text):raise ValueError('Derived test path already exists or changed')
    if not name.exists():name.write_text(text)
    fixture=module/'tests/mainline_runtime_test.c';original=fixture.read_text();fixture_before=sha(fixture)
    for old,new in [
        ('static int config(struct ieee80211_hw *hw, u32 changed) { (void)hw; (void)changed; return 0; }',
         'static int config(struct ieee80211_hw *hw, int radio_idx, u32 changed) { (void)hw; (void)radio_idx; (void)changed; return 0; }'),
        ('static int rts(struct ieee80211_hw *hw, u32 threshold) { (void)hw; (void)threshold; return 0; }',
         'static int rts(struct ieee80211_hw *hw, int radio_idx, u32 threshold) { (void)hw; (void)radio_idx; (void)threshold; return 0; }')]:
        if original.count(old)!=1:raise ValueError('Unexpected upstream fixture signature')
        original=original.replace(old,new)
    original+='\n_Static_assert(__builtin_types_compatible_p(__typeof__(&config), __typeof__(ops.config)), "config callback ABI");\n'
    original+='_Static_assert(__builtin_types_compatible_p(__typeof__(&rts), __typeof__(ops.set_rts_threshold)), "RTS callback ABI");\n'
    backup=fixture.with_suffix('.original')
    if backup.exists():raise ValueError('Fixture backup already exists')
    fixture.rename(backup);fixture.write_text(original)
    if a.resume_fixture_abi:
        for n in ('runtime-test','mainline_runtime_test.o','mainline_runtime_test.inputs.json','mainline_runtime_test.log'):
            prior=output/n
            if prior.exists():prior.rename(output/(n+'.before-abi-fix'))
    receipts=[json.loads(line) for line in (build/'trash-receipts.jsonl').read_text().splitlines()]
    trash=str(Path(receipts[0]['destination']).parent);hooks=build/'hooks'
    env=dict(os.environ,PYTHONDONTWRITEBYTECODE='1',PYTHONPATH=str(hooks),ELDEN_INPUT_MANIFEST=str(build/'provided-inputs.json'),
        ELDEN_GENERATED_ROOTS=json.dumps([str(module/'out'),str(build/'objects')]),ELDEN_BUILD_TRASH=trash,ELDEN_RETIRE_JOURNAL=str(build/'trash-receipts.jsonl'))
    env['PATH']=str(hooks/'bin')+os.pathsep+str(Path(sys.executable).parent)+os.pathsep+env['PATH']
    log_path=build/('runtime-test-abi-fixed.log' if a.resume_fixture_abi else 'runtime-test.log')
    with log_path.open('x') as log:
        subprocess.run([sys.executable,str(name)],env=env,stdout=log,stderr=subprocess.STDOUT,check=True)
    report=json.loads((output/'report.json').read_text())
    if not report['passed'] or not report['radio_simulated'] or report['hardware_verified']:raise ValueError('Unexpected fixture result')
    record={'original_test_sha256':before,'derived_recipe_sha256':sha(name),'report_sha256':sha(output/'report.json'),
        'passed':True,'upstream_fixture_assertions_unchanged':True,'fixture_original_sha256':fixture_before,'fixture_fixed_sha256':sha(fixture),
        'fixture_abi_changes':['config: add int radio_idx','set_rts_threshold: add int radio_idx','compile-time pointer type assertions'],
        'radio_simulated':True,'output':report['output'],
        'scope':'Official cfg80211/mac80211 lifecycle and simulated radio; excludes real ath12k/MHI/QMI transport and board power.',
        'hardware_operations':False}
    (build/'runtime-test.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
if __name__=='__main__':main()
