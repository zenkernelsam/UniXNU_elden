"""Test exact shared archive and compiled camera startup rejection boundaries."""
from pathlib import Path
import argparse,copy,hashlib,json,subprocess,sys
from peripheral_archive_audit import retained_archive,members
from camera_startup_audit import check_camera_startup

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('kernel-build','prelink-tool','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);tests=[]
    report=json.loads((a.kernel_build/'result.json').read_text());command=json.loads((a.kernel_build/'link-command.json').read_text())
    argument='-Wl,-force_load,'+report['backlight_original_path']
    def test(name,fn,refuse=False):
        try:fn()
        except ValueError as e:
            if not refuse:raise
            tests.append({'name':name,'refused':True,'reason':str(e)})
        else:
            if refuse:raise ValueError('Bad gate accepted: '+name)
            tests.append({'name':name,'accepted':True})
    test('actual_shared_archive_2members',lambda:retained_archive(argument,command,report))
    for field in ('one_original_I2c10_GENI_owner','changed_backlight_archive','backlight_original_path','backlight_shared_path'):
        wrong=copy.deepcopy(report);wrong.pop(field)
        test('missing_'+field,lambda wrong=wrong:retained_archive(argument,command,wrong),True)
    wrong=copy.deepcopy(report);wrong['changed_backlight_archive']['GeniI2c.o']['after']='f'*64
    test('original_GENI_misdeclared',lambda:retained_archive(argument,command,wrong),True)
    wrong=copy.deepcopy(report);wrong['changed_backlight_archive']['Platform.o']['after']='f'*64
    test('changed_platform_hash_mismatch',lambda:retained_archive(argument,command,wrong),True)
    replacement='-Wl,-force_load,'+report['backlight_shared_path']
    test('duplicate_shared_controller',lambda:retained_archive(argument,command+[replacement],report),True)
    # Actual original GENI bytes changed in a copied archive, even with honest
    # declarations, must be rejected as an unrelated controller replacement.
    bad=out/'bad/libEldenBacklight.a';bad.parent.mkdir();original=Path(report['backlight_original_path']);current=Path(report['backlight_shared_path'])
    for n in members(current):(bad.parent/n).write_bytes(subprocess.check_output(['ar','p',current,n]))
    (bad.parent/'GeniI2c.o').write_bytes((bad.parent/'Aw99706.o').read_bytes())
    subprocess.run(['ar','rcs',bad,*members(current)],cwd=bad.parent,check=True,capture_output=True)
    r=copy.deepcopy(report);r['backlight_shared_path']=str(bad);b=members(original);v=members(bad)
    r['changed_backlight_archive']={n:{'before':b[n],'after':v[n]} for n in b}
    cmd=[x.replace(str(current),str(bad)) for x in command]
    test('honestly_declared_GENI_replacement',lambda:retained_archive(argument,cmd,r),True)
    sys.path.insert(0,str(a.prelink_tool));from native_prelink import macho,exports
    raw=(a.kernel_build/'kernel.macho').read_bytes();names={n:va for va,g in exports(raw,macho(raw)).items() for n in g};symbol=lambda n:names[n]
    calls=[]
    for caller,callee in [('_elden_camera_capture_bind','_elden_camera_sensor_bind'),('_elden_camera_capture_probe','_elden_camera_sensor_probe'),
        ('_elden_camera_capture_start','_elden_camera_sensor_start'),('_elden_camera_powered_capture_bind','_elden_camera_capture_bind'),
        ('_elden_camera_powered_capture_probe','_elden_camera_capture_probe'),('_elden_camera_powered_capture_start','_elden_camera_capture_start')]:
        calls.append((symbol(caller)+4,symbol(callee)))
    native=a.kernel_build/'native.macho'
    test('actual_powered_wrapper_bounds',lambda:check_camera_startup(calls,symbol,native,True))
    test('power_calls_without_profile',lambda:check_camera_startup(calls,symbol,native),True)
    for name in ('_elden_camera_native_cci_open','_elden_camera_native_pipeline_open','_elden_camera_powered_capture_bind','_elden_camera_powered_capture_probe','_elden_camera_powered_capture_start'):
        test('outer_'+name,lambda name=name:check_camera_startup(calls+[(symbol('_elden_camera_power_resource')+4,symbol(name))],symbol,native,True),True)
    for caller,callee in [('_elden_camera_powered_capture_bind','_elden_camera_capture_bind'),('_elden_camera_powered_capture_probe','_elden_camera_capture_probe'),('_elden_camera_powered_capture_start','_elden_camera_capture_start')]:
        badcalls=[x for x in calls if x[1]!=symbol(callee)]+[(symbol(caller)-4,symbol(callee))]
        test('before_actual_'+caller,lambda badcalls=badcalls:check_camera_startup(badcalls,symbol,native,True),True)
    (out/'result.json').write_text(json.dumps({'passed':True,'checks':len(tests),'tests':tests,'actual_archives_and_function_bounds':True,
        'injected_negative_edges':True,'kernel_sha256':sha(a.kernel_build/'kernel.macho'),'hardware':False},indent=2)+'\n')
    print('PASS actual shared camera archive/function rejection tests: '+str(len(tests))+' checks')
if __name__=='__main__':main()
