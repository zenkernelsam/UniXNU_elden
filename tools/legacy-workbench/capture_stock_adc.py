"""Record the original driver's USB thermometer conversions in a private trace instance."""
import argparse,json,shlex,subprocess,time
from pathlib import Path
import collect_power_evidence as power
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--capture',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
if not a.capture:raise SystemExit('Use --capture for original-driver reads and isolated tracing')
root=Path(__file__).resolve().parents[1];out=a.output.resolve();assert out.is_relative_to(root/'edl_work') and not out.exists()
assert power.run('shell','getprop','ro.serialno').strip()==power.SERIAL
assert power.run('shell','getprop','ro.product.device').strip()=='TB322FC'
assert power.run('shell','getprop','sys.boot_completed').strip()=='1'
assert power.root_command()[0]=='su'
script=r'''set -eu
base=/sys/kernel/tracing/instances/elden_adc_20261109
zone1=; zone2=
for zone in /sys/class/thermal/thermal_zone*; do
 name=$(cat "$zone/type")
 case "$name" in usb1-conn-therm) zone1=$zone;; usb2-conn-therm) zone2=$zone;; esac
done
[ -n "$zone1" ] && [ -n "$zone2" ] || { echo 'REFUSED: connector zones missing'; exit 2; }
[ ! -e "$base" ] || { echo 'REFUSED: existing trace instance'; exit 2; }
mkdir "$base"
cleanup() {
 if [ -d "$base" ]; then
  echo 0 > "$base/tracing_on"
  for event in spmi_read_begin spmi_read_end spmi_write_begin spmi_write_end; do
   echo 0 > "$base/events/spmi/$event/enable"
  done
  rmdir "$base"
 fi
}
trap cleanup EXIT HUP INT TERM
echo 0 > "$base/tracing_on"
echo 256 > "$base/buffer_size_kb"
echo mono_raw > "$base/trace_clock"
for event in spmi_read_begin spmi_read_end spmi_write_begin spmi_write_end; do
 echo 'sid == 0 && addr >= 36864 && addr <= 37375' > "$base/events/spmi/$event/filter"
 echo 1 > "$base/events/spmi/$event/enable"
done
echo 1 > "$base/tracing_on"
for pass in 1 2; do
 echo "ELDEN_ADC_BEGIN port=1 pass=$pass" > "$base/trace_marker"
 value=$(cat "$zone1/temp")
 echo "ELDEN_ADC_END port=1 pass=$pass mc=$value" > "$base/trace_marker"
 printf 'USB1 pass=%s temperature_mc=%s\n' "$pass" "$value"
 echo "ELDEN_ADC_BEGIN port=2 pass=$pass" > "$base/trace_marker"
 value=$(cat "$zone2/temp")
 echo "ELDEN_ADC_END port=2 pass=$pass mc=$value" > "$base/trace_marker"
 printf 'USB2 pass=%s temperature_mc=%s\n' "$pass" "$value"
 sleep 1
done
echo 0 > "$base/tracing_on"
cat "$base/trace"
for f in "$base"/per_cpu/cpu*/stats; do printf '%s\n' "$f"; cat "$f"; done
cleanup
trap - EXIT HUP INT TERM
echo ADC_TRACE_REMOVED
'''
out.mkdir();(out/'device-script.sh').write_text(script,encoding='utf-8',newline='\n')
start=time.monotonic();r=subprocess.run([power.ADB,'-s',power.SERIAL,'exec-out','su -c '+shlex.quote(script)],capture_output=True,timeout=30)
(out/'trace.txt').write_bytes(r.stdout);(out/'stderr.txt').write_bytes(r.stderr)
record=dict(returncode=r.returncode,seconds=time.monotonic()-start,trace_removed=b'ADC_TRACE_REMOVED' in r.stdout,
    original_thermal_reads=True,custom_spmi_transactions=False,device_reboot=False,output_bytes=len(r.stdout))
(out/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
if r.returncode or not record['trace_removed']:raise SystemExit('Check trace cleanup before another capture')
