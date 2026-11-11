"""Confirm the just-restored Y700 stock boot, record fresh power, then sleep its display."""
from pathlib import Path
import argparse,datetime,json,shlex,subprocess,time
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args();out=a.run.resolve()
assert out.is_relative_to(r/'edl_work')
flash=json.loads((out/'flash/result.json').read_text())
assert flash['success'] and flash['serial']=='HA2A1PT2'
assert flash['image_sha256']==flash['full_readback_sha256']=='8b8e72dad85a65d3bf6fc2c6b2a88064d0487e4696ae578e9fd4c46a33565890'
base=['D:/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe','-s','HA2A1PT2']
for attempt in range(10):
 q=subprocess.run(base+['shell','getprop','sys.boot_completed'],capture_output=True,text=True,timeout=6)
 if q.returncode==0 and q.stdout.strip()=='1':break
 time.sleep(3)
else:raise SystemExit('Android completion not observed in this window')
cmd='id; getprop ro.boot.verifiedbootstate; getprop sys.boot_completed; cat /sys/class/power_supply/battery/capacity /sys/class/power_supply/battery/voltage_now /sys/class/power_supply/battery/current_now /sys/class/power_supply/battery/temp /sys/class/power_supply/battery/status'
q=subprocess.run(base+['shell','su -c '+shlex.quote(cmd)],capture_output=True,text=True,timeout=15,check=True)
(out/'android-final.txt').write_text(q.stdout,encoding='utf-8');lines=q.stdout.strip().splitlines()
assert 'uid=0(root)' in lines[0] and lines[1:3]==['green','1']
subprocess.run(base+['shell','input','keyevent','223'],timeout=10,check=True)
q=subprocess.run(base+['shell','dumpsys','power'],capture_output=True,text=True,timeout=10,check=True)
wake=[x.strip() for x in q.stdout.splitlines() if 'mWakefulness=' in x or 'Display Power: state=' in x]
report={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'serial':'HA2A1PT2','root':True,'verifiedboot':'green','boot_completed':True,'capacity_percent':int(lines[3]),'voltage_mv':int(lines[4])/1000,'current_ma':int(lines[5])/1000,'temperature_c':int(lines[6])/10,'status':lines[7],'sleep_command_succeeded':True,'display_state':wake}
(out/'android-final.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))
