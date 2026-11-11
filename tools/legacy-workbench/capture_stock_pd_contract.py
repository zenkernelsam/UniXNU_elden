"""Use the stock UCSI command mutex for fixed GET-only capability/status queries."""
import argparse,json,shlex,subprocess,time
from pathlib import Path
import collect_power_evidence as power
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--collect',action='store_true');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
if not a.collect:raise SystemExit('Use --collect for explicit stock UCSI GET commands')
root=Path(__file__).resolve().parents[1];out=a.output.resolve();assert out.is_relative_to(root/'edl_work') and not out.exists()
assert power.run('shell','getprop','ro.serialno').strip()==power.SERIAL
assert power.run('shell','getprop','ro.product.device').strip()=='TB322FC'
assert power.run('shell','getprop','sys.boot_completed').strip()=='1'
script=r'''set -eu
base=/data/local/tmp/elden_pd_read_20261109
[ ! -e "$base" ] || exit 2
mkdir "$base"
cleanup() { umount "$base" 2>/dev/null || true; rmdir "$base"; }
trap cleanup EXIT HUP INT TERM
mount -t debugfs debugfs "$base"
device=; count=0
for d in "$base"/usb/ucsi/*; do
 [ -f "$d/command" ] && [ -r "$d/response" ] || continue
 device=$d; count=$((count+1))
done
[ "$count" = 1 ] || { echo 'REFUSED: ambiguous UCSI debugfs'; exit 2; }
for command in 0x6 0x10012 0x20012 0x700820010 0x20012; do
 printf 'GET %s\n' "$command"
 printf '%s\n' "$command" > "$device/command"
 cat "$device/response"
done
cleanup
trap - EXIT HUP INT TERM
echo PD_GET_COMPLETE_REMOVED
'''
out.mkdir();(out/'device-script.sh').write_text(script,encoding='utf-8',newline='\n')
start=time.monotonic();r=subprocess.run([power.ADB,'-s',power.SERIAL,'exec-out','su -c '+shlex.quote(script)],capture_output=True,timeout=30)
(out/'capture.txt').write_bytes(r.stdout);(out/'stderr.txt').write_bytes(r.stderr)
result=dict(returncode=r.returncode,seconds=time.monotonic()-start,removed=b'PD_GET_COMPLETE_REMOVED' in r.stdout,
    get_only=True,control_words=['0x6','0x10012','0x20012','0x700820010','0x20012'],device_reboot=False)
(out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if r.returncode or not result['removed']:raise SystemExit('Verify debugfs cleanup before further work')
