"""One admitted Y700 power probe, after native battery admission; default offline.

Waits in fastboot, starts the bound logger, flashes only the reviewed boot_a
candidate, and watches the established RPC/panic return. Never retries a write.
"""
import argparse,datetime,hashlib,json,os,subprocess,sys,time
from pathlib import Path
from avb_guard import check_image
from flash_verified_boot import FASTBOOT,SERIAL,fastboot,read_fastboot_power

ROOT=Path(__file__).resolve().parents[1]
IMAGE=ROOT/'edl_work/power-inspect-20261107/boot-signed.img'
REFERENCE=ROOT/'edl_work/hid-20261107/boot-signed.img'
EXPECTED='5806d0266cae4216fd3cbcd1a44285143f93a918981a004804ae8f98da04a079'
SESSION='0829a44ab63b0cb5'
FLAGS=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0

def present():
    r=subprocess.run([FASTBOOT,'devices'],capture_output=True,text=True,timeout=8,creationflags=FLAGS)
    return r.returncode==0 and any(row.split()[0]==SERIAL for row in r.stdout.splitlines() if row.split())

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--run',action='store_true');p.add_argument('--output',type=Path)
    p.add_argument('--wait-power-seconds',type=int,default=7200)
    a=p.parse_args()
    check=check_image(IMAGE,'boot_a')
    if check.sha256!=EXPECTED or check.index!=1738713600 or check.location!=0:raise RuntimeError('candidate admission differs')
    if check_image(REFERENCE,'boot_a').sha256!='7282b272395a3c7a9a099bad3ab1f10b20a6951320129a96373f3410a868a363':raise RuntimeError('reference admission differs')
    if not a.run:
        print('Offline admitted: exact power candidate, native battery gate, one flash and bounded return monitor.');return
    if not a.output or a.wait_power_seconds<1:p.error('--run requires fresh output and positive wait duration')
    out=a.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'edl_work'):p.error('output must be fresh under project edl_work')
    out.mkdir(parents=True)
    report=dict(image_sha256=EXPECTED,session=SESSION,serial=SERIAL,index=1738713600,
                started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),phase='waiting_power',write_attempts=0)
    def save():
        (out/'result.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    save();deadline=time.monotonic()+a.wait_power_seconds
    while time.monotonic()<deadline:
        try:
            if present():
                if 'current-slot: a' not in fastboot('getvar','current-slot'):raise RuntimeError('target slot differs')
                power=read_fastboot_power();report['last_power']=power
                report['last_observation_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
                print('Native power '+json.dumps(power),flush=True)
                save()
                if power['battery_soc_ok']=='yes':break
            else:
                report['last_connection']='target absent';save()
        except Exception as error:
            report['last_observation_error']=str(error);save()
        time.sleep(min(120,max(0,deadline-time.monotonic())))
    else:
        report['phase']='held_low_power_or_missing_target';save();return
    # Power is checked again by the writer immediately before its EDL transition.
    report['phase']='admitted';save()
    log_handle=(out/'console.txt').open('w',encoding='utf-8')
    listener=subprocess.Popen([sys.executable,'-u','-X','utf8',ROOT/'edl_work/elden_usb_console.py',
        '--listen','--expect-session',SESSION,'--status-interval','3','--seconds','360','--wait-seconds','150',
        '--reboot-after-ready','--output',out/'usb'],cwd=ROOT,stdout=log_handle,stderr=subprocess.STDOUT,creationflags=FLAGS)
    try:
        report['write_attempts']=1;report['phase']='flashing';save()
        with (out/'flash.txt').open('w',encoding='utf-8') as f:
            r=subprocess.run([sys.executable,'-u','-X','utf8',ROOT/'edl_work/flash_verified_boot.py',IMAGE,REFERENCE,
                '--write','--expect-sha',EXPECTED,'--output',out/'flash'],cwd=ROOT,stdout=f,stderr=subprocess.STDOUT,creationflags=FLAGS)
        report['writer_exit']=r.returncode
        if r.returncode:report['phase']='writer_stopped';save();return
        flashed=json.loads((out/'flash/result.json').read_text())
        if not flashed.get('success') or flashed.get('full_readback_sha256')!=EXPECTED:raise RuntimeError('full readback not qualified')
        report['full_device_sha256']=flashed['full_readback_sha256'];report['phase']='waiting_return';save()
        deadline=time.monotonic()+540;health=0
        while time.monotonic()<deadline:
            if present():
                if 'current-slot: a' in fastboot('getvar','current-slot'):health+=1
                else:health=0
                if health>=3:break
            else:health=0
            time.sleep(1)
        report['returned_to_fastboot']=health>=3
        if health>=3:
            report['phase']='returned_to_fastboot';report['returned_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
            try:listener.wait(timeout=12)
            except subprocess.TimeoutExpired:pass
        else:report['phase']='return_unconfirmed_stop_live'
        raws=sorted((out/'usb').glob('*.bin'))
        if raws:
            raw=raws[-1].read_bytes();text=raw.decode('utf-8','replace')
            report['raw_sha256']=hashlib.sha256(raw).hexdigest();report['raw_bytes']=len(raw)
            report['power_diagnostic_completed']='ELDEN_POWER_INSPECT end;' in text
            report['power_begin_observed']='ELDEN_POWER_INSPECT begin writes=0' in text
        save()
    except Exception as error:
        report['phase']='stopped_exception';report['error']=str(error);save()
    finally:
        if listener.poll() is None:listener.terminate();listener.wait(timeout=10)
        log_handle.close();save()
    print(json.dumps(report,indent=2),flush=True)

if __name__=='__main__':
    # A task-scoped system-awake request; the PC screen may still turn off.
    # No persistent power-plan or USB settings are changed.
    awake=None
    if os.name=='nt' and '--run' in sys.argv:
        import ctypes
        awake=ctypes.windll.kernel32.SetThreadExecutionState
        if not awake(0x80000001):raise RuntimeError('could not keep the test host awake')
    try:main()
    finally:
        if awake:awake(0x80000000)
