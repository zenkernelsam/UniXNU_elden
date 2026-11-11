"""Prepare or explicitly collect a small read-only Android/TWRP evidence set.

Default performs no device access. --collect uses only the configured Y700
serial and read commands; it does not set properties, mount, flash, or reboot.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
from device_target import ADB,SERIAL

PROPERTIES=(
    'ro.bootmode','ro.boot.hardware','ro.boot.bootdevice','ro.boot.boot_devices',
    'ro.boot.slot_suffix','ro.boot.dynamic_partitions','ro.boot.usbcontroller',
    'ro.boot.usb.dwc3_msm','sys.usb.controller','sys.usb.config','sys.usb.state',
    'sys.usb.ffs.ready','init.svc.adbd','twrp.modules.loaded')
SCRIPT='''set -u
printf '@@kernel\\n'
uname -r
printf '@@properties\\n'
'''+''.join(f"printf '{p}='; getprop {p}\n" for p in PROPERTIES)+'''
printf '@@usb\\n'
for file in /sys/class/udc/*/state /sys/class/usb_role/*/role /sys/class/typec/port*/data_role /sys/bus/platform/devices/a600000.ssusb/mode; do
    if [ -r "$file" ]; then printf '%s=' "$file"; cat "$file"; fi
done
printf '@@storage\\n'
for file in /sys/class/block/sd?/size /sys/class/block/sd?/queue/logical_block_size; do
    if [ -r "$file" ]; then printf '%s=' "$file"; cat "$file"; fi
done
printf '@@modules\\n'
if [ -r /proc/modules ]; then
    while read -r line; do
        case "$line" in *ufs*|*dwc3*|*novatek*|*glink*|*qcom_scm*) printf '%s\\n' "$line";; esac
    done < /proc/modules
fi
printf '@@mounts\\n'
if [ -r /proc/mounts ]; then
    while read -r source target type rest; do
        case "$target" in /data|/metadata|/config|/dev/usb-ffs/adb) printf '%s %s %s %s\\n' "$source" "$target" "$type" "$rest";; esac
    done < /proc/mounts
fi
printf '@@done\\n'
'''


def analyze(raw):
    sections={};section=None
    for line in raw.replace('\r\n','\n').splitlines():
        if line.startswith('@@'):
            section=line[2:];sections[section]=[]
        elif section is not None:sections[section].append(line)
    if 'done' not in sections or 'properties' not in sections:
        raise ValueError('incomplete collection; do not infer missing hardware')
    props=dict(line.split('=',1) for line in sections['properties'] if '=' in line)
    notes=[]
    for key in ('ro.boot.usbcontroller','sys.usb.controller','ro.boot.slot_suffix','ro.boot.bootdevice','ro.boot.dynamic_partitions'):
        if not props.get(key):notes.append('Missing '+key+'; compare the recovery handoff before changing drivers.')
    if props.get('sys.usb.ffs.ready')!='1':notes.append('FunctionFS is not reported ready; inspect adbd/configfs before blaming the USB PHY.')
    if props.get('init.svc.adbd')!='running':notes.append('adbd is not reported running.')
    if props.get('sys.usb.config')!=props.get('sys.usb.state'):notes.append('Requested USB configuration and applied state differ.')
    return dict(kernel=sections.get('kernel',[]),properties=props,usb=sections.get('usb',[]),
                storage=sections.get('storage',[]),modules=sections.get('modules',[]),
                mounts=sections.get('mounts',[]),notes=notes,
                claims='Android/TWRP observations only; does not certify XNU UFS or mounted iPadOS root')


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--collect',action='store_true')
    p.add_argument('--input',type=Path,help='analyze an existing collection without connecting')
    p.add_argument('--output',type=Path,default=Path('edl_work/evening-evidence'))
    a=p.parse_args()
    if a.collect and a.input:p.error('choose collection or existing input')
    if a.input:raw=a.input.read_text(encoding='utf-8')
    elif a.collect:
        prefix=[str(ADB),'-s',SERIAL]
        uid=subprocess.run([*prefix,'shell','id -u'],check=True,capture_output=True,text=True,timeout=15).stdout.strip()
        if uid not in ('0','2000'):raise RuntimeError('unexpected adb uid')
        command='sh -c '+shlex.quote(SCRIPT)
        if uid!='0':command='su -c '+shlex.quote(command)
        raw=subprocess.run([*prefix,'shell',command],check=True,capture_output=True,text=True,timeout=45).stdout
    else:
        print('Ready. Default is offline. Owner can run --collect after connecting Y700; no write commands are used.')
        return
    result=analyze(raw);a.output.mkdir(parents=True,exist_ok=True)
    (a.output/'read-only.txt').write_text(raw,encoding='utf-8')
    (a.output/'report.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
