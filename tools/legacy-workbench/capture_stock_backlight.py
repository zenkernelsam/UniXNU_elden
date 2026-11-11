"""Observe the stock wake/sleep backlight transactions in an isolated trace instance."""
import argparse,json,shlex,subprocess,time
from pathlib import Path
import collect_power_evidence as power
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
if not a.capture:raise SystemExit('No device action without --capture')
root=Path(__file__).resolve().parents[1];out=a.output.resolve();assert out.is_relative_to(root/'edl_work') and not out.exists()
assert power.run('shell','getprop','ro.serialno').strip()==power.SERIAL
assert power.run('shell','getprop','ro.product.device').strip()=='TB322FC'
assert power.run('shell','getprop','sys.boot_completed').strip()=='1'
assert power.root_command()[0]=='su'
script=r'''set -eu
base=/sys/kernel/tracing/instances/elden_backlight_20261109
bl=/sys/class/backlight/panel0-backlight
[ "$(cat "$bl/actual_brightness")" = 0 ] && [ "$(cat "$bl/bl_power")" = 4 ] || { echo 'REFUSED: screen not initially off'; exit 2; }
[ ! -e "$base" ] || { echo 'REFUSED: trace instance already exists'; exit 2; }
mkdir "$base"
restored=0
cleanup() {
 if [ "$restored" = 0 ]; then input keyevent 223; fi
 if [ -d "$base" ]; then
  echo 0 > "$base/tracing_on"
  for event in i2c_write i2c_read i2c_reply i2c_result; do echo 0 > "$base/events/i2c/$event/enable"; done
  rmdir "$base"
 fi
}
trap cleanup EXIT HUP INT TERM
echo 0 > "$base/tracing_on"
echo 128 > "$base/buffer_size_kb"
for event in i2c_write i2c_read i2c_reply; do
 echo 'adapter_nr == 4 && (addr == 17 || addr == 118)' > "$base/events/i2c/$event/filter"
 echo 1 > "$base/events/i2c/$event/enable"
done
echo 'adapter_nr == 4' > "$base/events/i2c/i2c_result/filter"
echo 1 > "$base/events/i2c/i2c_result/enable"
echo 1 > "$base/tracing_on"
echo ELDEN_BACKLIGHT_WAKE > "$base/trace_marker"
input keyevent 224
sleep 4
printf 'WAKE_BRIGHTNESS='; cat "$bl/actual_brightness"
echo ELDEN_BACKLIGHT_SLEEP > "$base/trace_marker"
input keyevent 223
sleep 2
[ "$(cat "$bl/actual_brightness")" = 0 ] && [ "$(cat "$bl/bl_power")" = 4 ] || { echo 'RESTORE_FAILED'; exit 3; }
restored=1
echo 0 > "$base/tracing_on"
cat "$base/trace"
for f in "$base"/per_cpu/cpu*/stats; do printf '%s\n' "$f"; cat "$f"; done
echo SCREEN_OFF_RESTORED
'''
out.mkdir();(out/'device-script.sh').write_text(script,encoding='utf-8',newline='\n')
started=time.monotonic();r=subprocess.run([power.ADB,'-s',power.SERIAL,'exec-out','su -c '+shlex.quote(script)],capture_output=True,timeout=30)
(out/'trace.txt').write_bytes(r.stdout);(out/'stderr.txt').write_bytes(r.stderr)
report=dict(returncode=r.returncode,seconds=time.monotonic()-started,screen_off_restored=b'SCREEN_OFF_RESTORED' in r.stdout,
    direct_i2c_transactions=False,scope='stock wake/sleep; trace only adapter4, addresses11/76')
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if r.returncode or not report['screen_off_restored']:raise SystemExit('Check stock screen/trace restoration before another operation')
