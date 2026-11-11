"""Freeze and upload only ordinary source inputs for the Mac Y700 build."""
import argparse,hashlib,json,subprocess,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
DEVICE=Path('D:/Desktop/Git/UniXNU_elden')
DEST='ciscohe@192.168.68.54'
REMOTE='/Users/ciscohe/Desktop/UniXNU_elden'
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--attempt',type=int,default=1)
args=parser.parse_args();assert 1<=args.attempt<=99
suffix='' if args.attempt==1 else '-v'+str(args.attempt)
OUT=ROOT/('edl_work/battery-telemetry-20261108/attempt'+str(args.attempt));OUT.mkdir(parents=True,exist_ok=True)
SOURCE=ROOT/('research/battery-telemetry-20261108/source-input'+suffix);SOURCE.mkdir(parents=True,exist_ok=True)
files={}
for p in (DEVICE/'drivers/EldenPmicGlink').iterdir():
    if p.is_file() and p.suffix in ('.h','.c','.cpp') or p.name=='LICENSE':
        files[p.name]=p.read_bytes()
files['EldenPowerResources.h']=(DEVICE/'soc/sm8750/include/EldenPowerResources.h').read_bytes()
files['EldenCpuPerfResources.h']=(DEVICE/'soc/sm8750/include/EldenCpuPerfResources.h').read_bytes()
files['EldenScmiPerf.h']=(ROOT/'research/cpu-power-20261109/EldenScmiPerf.h').read_bytes()
for p in (DEVICE/'drivers/EldenUsbThermal').iterdir():
    if p.is_file() and (p.suffix in ('.h','.c','.cpp') or p.name=='LICENSE.GPL-2.0'):
        assert p.name not in files,'Ambiguous source name: '+p.name
        files[p.name]=p.read_bytes()
files['EldenAdcResources.h']=(DEVICE/'soc/sm8750/include/EldenAdcResources.h').read_bytes()
for name,data in files.items():
    p=SOURCE/name
    if p.exists():assert p.read_bytes()==data,'Input freeze already differs: '+name
    else:p.write_bytes(data)
builder=(ROOT/'edl_work/build_startup_ucsi_powerinputs.py').read_text()
old='out/startup-ucsi-powerinputs-20261108'
assert builder.count(old)==1
builder=builder.replace(old,'out/startup-battery-telemetry'+suffix+'-20261108')
assert builder.count("out/adsp-pmic-ucsi45-20261108/")==2
builder=builder.replace("out/adsp-pmic-ucsi45-20261108/","out/battery-telemetry"+suffix+"-20261108/")
marker="env['XNU_EXTRA_LD_ARCHIVES']=' '.join('-Wl,-force_load,'+str(path) for path in paths)"
assert builder.count(marker)==1
builder=builder.replace(marker,marker+"\nenv.update(ELDEN_BLUETOOTH_BUILD='0',ELDEN_BLUETOOTH_HCI_BUILD='0',ELDEN_BLUETOOTH_UART_BUILD='0',ELDEN_WCN_POWER_BUILD='0',ELDEN_BT_NATIVE_BUILD='0',ELDEN_TOUCH_RUNTIME_BUILD='0',ELDEN_TOUCH_HID_BUILD='0',ELDEN_HID_PHYSICAL_TOUCH='0',XNU_ELDEN_DATA_STORE='0',ELDEN_DATA_INSPECT_BUILD='0')")
(OUT/'build_startup_battery_telemetry.py').write_text(builder,encoding='utf-8',newline='\n')
manifest={}
archive=OUT/'source-upload.zip'
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED) as z:
    for p in SOURCE.iterdir():
        if not p.is_file():continue
        name='research/battery-telemetry-20261108/source-input'+suffix+'/'+p.name
        data=p.read_bytes();manifest[name]=hashlib.sha256(data).hexdigest();z.writestr(name,data)
    for p,name in [(ROOT/'edl_work/compile_battery_runtime_mac.py','out/compile_battery_runtime'+suffix+'_mac.py'),
                   (OUT/'build_startup_battery_telemetry.py','out/build_startup_battery_telemetry'+suffix+'.py')]:
        data=p.read_bytes();manifest[name]=hashlib.sha256(data).hexdigest();z.writestr(name,data)
    z.writestr('manifest.json',json.dumps(manifest,indent=2)+'\n')
digest=hashlib.sha256(archive.read_bytes()).hexdigest()
installer='''from pathlib import Path
import hashlib,json,zipfile
r=Path('/Users/ciscohe/Desktop/UniXNU_elden').resolve()
p=r/'out/battery-telemetry-source-upload%s.zip'
assert hashlib.sha256(p.read_bytes()).hexdigest()==%r
with zipfile.ZipFile(p) as z:
    m=json.loads(z.read('manifest.json'))
    assert set(z.namelist())==set(m)|{'manifest.json'}
    assert len(z.namelist())==len(set(z.namelist()))
    for n,h in m.items():
        t=(r/n).resolve();assert t.is_relative_to(r) and not t.is_symlink()
        b=z.read(n);assert hashlib.sha256(b).hexdigest()==h
        if t.exists():assert t.is_file() and t.read_bytes()==b,'Existing source differs: '+n
        else:t.parent.mkdir(parents=True,exist_ok=True);t.write_bytes(b)
print('Installed frozen battery source inside the Y700 repo; prior inputs preserved')
'''%(suffix,digest)
install=OUT/'install.py';install.write_text(installer,encoding='utf-8',newline='\n')
subprocess.run(['scp',str(archive),DEST+':'+REMOTE+'/out/battery-telemetry-source-upload'+suffix+'.zip'],check=True)
subprocess.run(['scp',str(install),DEST+':'+REMOTE+'/out/install-battery-telemetry'+suffix+'.py'],check=True)
subprocess.run(['ssh','-o','BatchMode=yes',DEST,'cd '+REMOTE+' && .venv/bin/python out/install-battery-telemetry'+suffix+'.py'],check=True)
(OUT/'upload.json').write_text(json.dumps(dict(source_files=len(manifest),zip_sha256=digest,files=manifest,
    device_operations=False,original_archives_preserved=True),indent=2)+'\n')
print(json.dumps(dict(files=len(manifest),zip_sha256=digest),indent=2))
