"""Prepare exact WL2868C functions and the sole qualified I2C10/backlight owner."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--backlight-variant',choices=('original','pd-power'),default='pd-power');a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);sys.path.insert(0,str(DRIVER))
    from PowerSource import derive as power
    from SharedBusSource import derive as shared,PINS
    body,power_record=power();files,deltas=shared(a.backlight_variant)
    (out/'PowerRegulator.inc').write_bytes(body)
    for name,text in files.items():(out/name).write_bytes(text.encode())
    for name in ('Power.c','Power.h','PowerBus.c','PowerBinding.c','PowerBinding.h','SharedI2c10.h','SharedI2c10Methods.inc','Core.h',
                 'NativePipeline.h','SensorBinding.h','NativeCci.h','Cci.h','Pipeline.h'):
        shutil.copyfile(DRIVER/name,out/name)
    for name in ('Aw99706.c','Aw99706.h','Brightness.h','SettledTrial.h','Lifecycle.h'):
        shutil.copyfile(ROOT/'drivers/EldenBacklight'/name,out/name)
    for name,digest in {'GeniI2c.c':'34eb6effebb1750a538cf092ce68ceddd4855b7d44678a71305a02e454bf8b7b',
                        'GeniI2c.h':'2ade734ab7cdbd41fcea68c7f7ff4392b779b2d04666118407a1533e22258e7c',
                        'EldenBacklightResources.h':'73685844525265522f8a915e708db4aaae9b07c78d724279e84d7da531643e2e'}.items():
        source=ROOT/'profiles/v91/sources/backlight'/name
        if sha(source)!=digest:raise ValueError('Original backlight GENI source changed: '+name)
        shutil.copyfile(source,out/name)
    report={'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},
        'fixed_source':json.loads((DRIVER/'power-source.json').read_text()),'original_backlight_pins':PINS,
        'original_regulator_functions':power_record,'shared_bus_deltas':deltas,
        'backlight_variant':a.backlight_variant,'one_original_SE2_GENI_engine':True,'physical_power_owner_installed':False,
        'camera_auto_start':False,'hardware':False}
    (out/'source.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n')
    print('Prepared actual WL2868C bodies and serialized original I2C10/backlight owner; no physical startup')
if __name__=='__main__':main()
