"""Compare the shared planner/executor with the exact pre-consolidation code."""
import argparse,hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REV='174365f537be6ce34bd000c6e688d9160e7d0f3f'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--wsl-distro');p.add_argument('--compiler',default='cc');a=p.parse_args()
    if a.output.exists():raise ValueError('New output required')
    a.output.mkdir(parents=True);old=a.output/'legacy';old.mkdir()
    for name in ('WcnPower.c','WcnPower.h'):
        data=subprocess.check_output(['git','-C',str(ROOT),'show',REV+':drivers/EldenBluetooth/'+name]);(old/name).write_bytes(data)
    if sha(old/'WcnPower.c')!='6a1d69b8660ebe5bbeb4cc1415ebe5d780b34126d74b1bf811e71c959543ff4f':raise ValueError('Legacy source changed')
    def path(v):
        v=str(Path(v).resolve());return '/mnt/'+v[0].lower()+v[2:].replace('\\','/') if a.wsl_distro else v
    prefix=['wsl','-d',a.wsl_distro,'--'] if a.wsl_distro else []
    flags=[a.compiler,'-std=c11','-O1','-g','-Wall','-Wextra','-fsanitize=address,undefined','-fno-omit-frame-pointer']
    if a.wsl_distro:flags[1:1]=['-fno-pie','-no-pie']
    def run(command,label):
        result=subprocess.run(prefix+command,capture_output=True)
        (a.output/(label+'.stdout')).write_bytes(result.stdout);(a.output/(label+'.stderr')).write_bytes(result.stderr)
        if result.returncode:raise RuntimeError(result.stderr.decode('utf-8',errors='backslashreplace'))
        return result.stdout.decode()
    names=['elden_wcn_rail_specs','elden_wcn_power_acquire','elden_wcn_power_stop','elden_wcn_power_finish','elden_wcn_power_check','elden_wcn_parent_decode','elden_wcn_cpr_plan','elden_wcn_power_cpr']
    old_obj=a.output/'legacy.o';new_obj=a.output/'shared.o';source=ROOT/'drivers/EldenBluetooth'
    run(flags+['-D'+n+'=legacy_'+n for n in names]+['-c',path(old/'WcnPower.c'),'-o',path(old_obj)],'legacy-build')
    run(flags+['-Werror','-c',path(source/'WcnPower.c'),'-o',path(new_obj)],'shared-build')
    test=a.output/'test-compatibility'
    run(flags+['-Werror','-I'+path(source),path(source/'TestCprCompatibility.c'),path(old_obj),path(new_obj),'-o',path(test)],'compatibility-build')
    comparisons=run([path(test)],'comparisons')
    sequence=a.output/'test-sequence'
    run(flags+['-Wno-misleading-indentation','-I'+path(source),path(source/'TestWcnPower.c'),path(new_obj),'-o',path(sequence)],'sequence-build')
    sequence_result=run([path(sequence)],'sequence')
    result={'legacy_revision':REV,'legacy_source_sha256':sha(old/'WcnPower.c'),'source_sha256':{n:sha(source/n) for n in ('WcnPower.c','WcnPower.h','TestCprCompatibility.c','TestWcnPower.c')},
            'comparisons':comparisons,'original_sequence_checks':sequence_result,'passed':True,'callbacks_are_host_recorders':True,'hardware_operations':False}
    (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
