"""Read fixed PMK8550 PON watchdog fields through stock regmap; no data writes."""
from pathlib import Path
import hashlib,json,shlex,subprocess,time
r=Path(__file__).resolve().parents[1];out=r/'edl_work/watchdog-stock-20261111'
assert not out.exists()
adb='D:/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe';serial='HA2A1PT2'
def run(cmd):
    return subprocess.run([adb,'-s',serial,'exec-out',cmd],capture_output=True,timeout=30)
pre=run('getprop ro.serialno; getprop ro.product.device; getprop sys.boot_completed; getprop ro.boot.verifiedbootstate; su -c '+shlex.quote('id; cat /sys/class/power_supply/battery/voltage_now /sys/class/power_supply/battery/capacity /sys/class/power_supply/battery/temp'))
assert pre.returncode==0 and pre.stdout.splitlines()[:4]==[serial.encode(),b'TB322FC',b'1',b'green'] and b'uid=0(root)' in pre.stdout
script=r'''set -eu
base=/data/local/tmp/elden-wdt-read-20261111
[ ! -e "$base" ]; mkdir "$base"
mounted=0; saved=0
cleanup(){
 if [ "$saved" = 1 ]; then printf '%s\n' "$old_address" > "$map/address"; printf '%s\n' "$old_count" > "$map/count"; fi
 if [ "$mounted" = 1 ]; then umount "$base"; fi
 rmdir "$base"
}
trap cleanup EXIT HUP INT TERM
mount -t debugfs debugfs "$base"; mounted=1
printf 'REGMAP_NAMES\n'; ls "$base/regmap"
map=$base/regmap/0-00
if [ -r "$map/name" ]; then cat "$map/name"; fi
[ -r "$map/data" ] && [ -w "$map/address" ] && [ -w "$map/count" ] || { echo REGMAP_FIXED_CURSOR_UNAVAILABLE; exit 2; }
old_address=$(cat "$map/address"); old_count=$(cat "$map/count"); saved=1
printf 'CURSOR_BEFORE address=%s count=%s\n' "$old_address" "$old_count"
printf '1\n' > "$map/count"
for reg in 0x1304 0x1305 0x1354 0x1355 0x1356 0x1357 0x1354 0x1355 0x1356 0x1357; do
 printf '%s\n' "$reg" > "$map/address"
 printf 'READ %s ' "$reg"; cat "$map/data"
done
printf '%s\n' "$old_address" > "$map/address"; printf '%s\n' "$old_count" > "$map/count"
printf 'CURSOR_AFTER address=%s count=%s\n' "$(cat "$map/address")" "$(cat "$map/count")"
saved=0; cleanup; trap - EXIT HUP INT TERM
echo WDT_READ_COMPLETE_REMOVED
'''
out.mkdir();(out/'before.txt').write_bytes(pre.stdout);(out/'device-script.sh').write_text(script,newline='\n')
start=time.monotonic();q=run('su -c '+shlex.quote(script))
(out/'capture.txt').write_bytes(q.stdout);(out/'stderr.txt').write_bytes(q.stderr)
removed=run('su -c '+shlex.quote('test ! -e /data/local/tmp/elden-wdt-read-20261111 && echo REMOVED'))
report={'returncode':q.returncode,'seconds':time.monotonic()-start,'removed':removed.stdout.strip()==b'REMOVED',
        'complete':b'WDT_READ_COMPLETE_REMOVED' in q.stdout,'hardware_writes':False,'reboot':False,
        'capture_sha256':hashlib.sha256(q.stdout).hexdigest(),'script_sha256':hashlib.sha256(script.encode()).hexdigest()}
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2));print(q.stdout.decode(errors='replace'));print(q.stderr.decode(errors='replace'))
assert report['removed'],'Restore cursor/unmount before further work'
if q.returncode:raise SystemExit(q.returncode)
