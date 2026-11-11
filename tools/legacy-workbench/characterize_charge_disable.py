"""Brief, self-restoring stock-Android characterization of charging_enable."""
import argparse,hashlib,json,shlex,subprocess,time
from pathlib import Path
import collect_power_evidence as power

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--run',action='store_true');p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
if not a.run:raise SystemExit('Offline only. --run performs the bounded stock comparison.')
root=Path(__file__).resolve().parents[1];out=a.output.resolve()
assert out.is_relative_to(root/'edl_work') and not out.exists()
assert power.run('get-state').strip()=='device'
assert power.run('shell','getprop','ro.serialno').strip()==power.SERIAL
assert power.run('shell','getprop','ro.product.device').strip()=='TB322FC'
assert power.run('shell','getprop','sys.boot_completed').strip()=='1'
assert power.root_command()[0]=='su'
script=r'''set -eu
node=/sys/class/power_supply/battery/charging_enable
old=$(cat "$node")
[ "$old" = 1 ] || { echo 'REFUSED: original charging_enable is not1'; exit 2; }
mv=$(cat /sys/class/power_supply/battery/voltage_now)
soc=$(cat /sys/class/power_supply/battery/capacity)
temp=$(cat /sys/class/power_supply/battery/temp)
online=$(cat /sys/class/power_supply/usb/online)
[ "$mv" -ge 3700000 ] && [ "$soc" -ge 30 ] && [ "$temp" -ge 50 ] && [ "$temp" -le 420 ] && [ "$online" = 1 ] || { echo 'REFUSED: power admission'; exit 3; }
restored=0
restore() {
  if [ "$restored" = 0 ]; then printf '%s\n' "$old" > "$node"; restored=1; fi
}
trap restore EXIT HUP INT TERM
sample() {
  printf 'SAMPLE\t%s\t%s\n' "$1" "$2"
  for f in charging_enable status capacity voltage_now current_now; do
    printf 'battery\t%s\t' "$f"; cat /sys/class/power_supply/battery/"$f"
  done
  for f in online voltage_now current_now input_current_limit; do
    printf 'usb\t%s\t' "$f"; cat /sys/class/power_supply/usb/"$f"
  done
  for f in fg1_current fg2_current; do printf 'gauge\t%s\t' "$f"; cat /sys/class/qcom-battery/"$f"; done
}
for n in 1 2 3; do sample before "$n"; sleep 2; done
printf '0\n' > "$node"
for n in 1 2 3 4 5 6 7 8; do sample disabled "$n"; sleep 2; done
restore
for n in 1 2 3; do sample restored "$n"; sleep 2; done
[ "$(cat "$node")" = "$old" ] || { echo 'RESTORE_FAILED'; exit 4; }
echo RESTORE_VERIFIED
'''
out.mkdir();(out/'device-script.sh').write_text(script,encoding='utf-8',newline='\n')
started=time.monotonic()
result=subprocess.run([power.ADB,'-s',power.SERIAL,'exec-out','su -c '+shlex.quote(script)],
    capture_output=True,timeout=60)
(out/'transcript.txt').write_bytes(result.stdout);(out/'stderr.txt').write_bytes(result.stderr)
lines=result.stdout.decode('utf-8','replace').splitlines();samples=[]
for line in lines:
    cols=line.split('\t')
    if len(cols)==3 and cols[0]=='SAMPLE':samples.append(dict(phase=cols[1],sample=int(cols[2]),values={}))
    elif len(cols)==3 and samples:samples[-1]['values'][cols[0]+'.'+cols[1]]=cols[2]
report=dict(returncode=result.returncode,seconds=time.monotonic()-started,
    restored='RESTORE_VERIFIED' in lines,script_sha256=hashlib.sha256(script.encode()).hexdigest(),samples=samples)
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(returncode=result.returncode,seconds=report['seconds'],restored=report['restored'],samples=len(samples)),indent=2))
if result.returncode or not report['restored']:raise SystemExit('Stock control restoration needs checking; no further experiment.')
