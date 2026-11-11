"""Test a conservative OEM charging window, then restore its exact original value."""
import argparse,json,shlex,subprocess,time
from pathlib import Path
import collect_power_evidence as power
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--run',action='store_true');p.add_argument('--node',choices=('protection_setting','protection_setting_eu'),required=True)
p.add_argument('--exercise-resume',action='store_true')
p.add_argument('--output',type=Path,required=True);a=p.parse_args()
if not a.run:raise SystemExit('No action without --run')
root=Path(__file__).resolve().parents[1];out=a.output.resolve();assert out.is_relative_to(root/'edl_work') and not out.exists()
assert power.run('shell','getprop','ro.serialno').strip()==power.SERIAL
assert power.run('shell','getprop','ro.product.device').strip()=='TB322FC'
assert power.run('shell','getprop','sys.boot_completed').strip()=='1'
assert power.root_command()[0]=='su'
node='/sys/class/power_supply/battery/'+a.node
old_text=power.run('exec-out','su -c '+shlex.quote('cat '+node)).strip()
assert old_text.isdecimal();old=int(old_text);assert 0<=old<=199100
script=r'''set -eu
node=__NODE__
[ "$(cat "$node")" = __OLD_TEXT__ ] || { echo 'REFUSED: baseline changed'; exit 2; }
[ "$(cat /sys/class/power_supply/battery/charging_enable)" = 1 ] || exit 2
mv=$(cat /sys/class/power_supply/battery/voltage_now)
soc=$(cat /sys/class/power_supply/battery/capacity)
temp=$(cat /sys/class/power_supply/battery/temp)
[ "$mv" -ge 3700000 ] && [ "$soc" -ge 45 ] && [ "$soc" -le 80 ] && [ "$temp" -ge 50 ] && [ "$temp" -le 420 ] || exit 3
[ "$(cat /sys/class/power_supply/usb/online)" = 1 ] || exit 3
restored=0
restore() {
 if [ "$restored" = 0 ]; then
  printf '__OLD__\n' > "$node"
  if [ "$(cat /sys/class/power_supply/battery/charging_enable)" != 1 ]; then printf '1\n' > /sys/class/power_supply/battery/charging_enable; fi
  restored=1
 fi
}
trap restore EXIT HUP INT TERM
sample() {
 printf 'SAMPLE\t%s\t%s\n' "$1" "$2"
 printf 'battery\twindow\t'; cat "$node"
 for f in charging_enable status capacity current_now; do printf 'battery\t%s\t' "$f"; cat /sys/class/power_supply/battery/"$f"; done
 for f in online current_now; do printf 'usb\t%s\t' "$f"; cat /sys/class/power_supply/usb/"$f"; done
}
for n in 1 2 3; do sample before "$n"; sleep 2; done
printf '135040\n' > "$node"
for n in 1 2 3 4 5 6 7 8; do sample window "$n"; sleep 2; done
__RESUME__
restore
for n in 1 2 3; do sample restored "$n"; sleep 2; done
[ "$(cat "$node")" = __OLD_TEXT__ ] || { echo RESTORE_FAILED; exit 4; }
echo RESTORE_VERIFIED
'''.replace('__NODE__',node).replace('__OLD_TEXT__',shlex.quote(old_text)).replace('__OLD__',str(old))
script=script.replace('__RESUME__',"printf '175080\\n' > \"$node\"\nfor n in 1 2 3 4 5 6 7 8; do sample resume \"$n\"; sleep 2; done" if a.exercise_resume else '')
if a.exercise_resume:script=script.replace('[ "$soc" -le 80 ]','[ "$soc" -le 70 ]')
out.mkdir();(out/'device-script.sh').write_text(script,encoding='utf-8',newline='\n')
started=time.monotonic();r=subprocess.run([power.ADB,'-s',power.SERIAL,'exec-out','su -c '+shlex.quote(script)],capture_output=True,timeout=60)
(out/'transcript.txt').write_bytes(r.stdout);(out/'stderr.txt').write_bytes(r.stderr)
lines=r.stdout.decode('utf-8','replace').splitlines();samples=[]
for line in lines:
 c=line.split('\t')
 if len(c)==3 and c[0]=='SAMPLE':samples.append(dict(phase=c[1],sample=int(c[2]),values={}))
 elif len(c)==3 and samples:samples[-1]['values'][c[0]+'.'+c[1]]=c[2]
result=dict(node=a.node,original=old,test=135040,resume_window=175080 if a.exercise_resume else None,restored='RESTORE_VERIFIED' in lines,returncode=r.returncode,
    seconds=time.monotonic()-started,samples=samples)
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({k:v for k,v in result.items() if k!='samples'},indent=2))
if r.returncode or not result['restored']:raise SystemExit('Restore needs verification; no next experiment')
