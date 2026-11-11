"""Explicit fixed-target raw ADB upload; use only after --check-device admission.

Windows adb exec-in passes binary input unchanged. Remote progress/status are
read with root because KernelSU creates them under its root umask/SELinux label.
This entry point installs only the fixed image and target from the C writer.
"""
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import threading
import time
from install_ipados_root import IMAGE, SHA, OUT, PREFIX, REMOTE, file_hash

def shell(command):
    return subprocess.run([*PREFIX,'shell','su -c '+shlex.quote(command)],
                          capture_output=True,timeout=30)

def main():
    import argparse
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--write-reviewed-root',action='store_true',required=True)
    p.parse_args()
    p.error("Full raw stdin transfer failed on this device; use install_ipados_root_chunks.py instead.")
    assert IMAGE.stat().st_size==9381609472 and file_hash(IMAGE)==SHA
    assert json.loads((OUT/'raw32-roundtrip.json').read_text())['passed']
    r=shell(REMOTE+' --check');assert r.returncode==0 and b'TARGET VERIFIED' in r.stdout
    stamp=time.strftime('%Y%m%d-%H%M%S')
    remote_log='/data/local/tmp/elden-raw-root-'+stamp+'.log'
    remote_status=remote_log+'.status'
    command=REMOTE+' --write-image > '+remote_log+' 2>&1; code=$?; echo "$code" > '+remote_status
    log=OUT/('raw-write-'+stamp+'.log');error_log=OUT/('raw-adb-'+stamp+'.log')
    started=time.monotonic();errors=[];sent=[0];status=None
    print('Binary input admitted; raw stream starting.',flush=True)
    with IMAGE.open('rb') as source,error_log.open('xb') as err,log.open('xb') as output:
        process=subprocess.Popen([*PREFIX,'exec-in','su -c '+shlex.quote(command)],
                                 stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=err)
        def feed():
            try:
                while block:=source.read(1<<20):
                    process.stdin.write(block);sent[0]+=len(block)
            except Exception as e:errors.append(repr(e))
            finally:
                try:process.stdin.close()
                except OSError as e:errors.append(repr(e))
        sender=threading.Thread(target=feed);sender.start();shown=b'';last_note=started
        # Do not overlap root-shell sessions with the raw exec-in session.
        while process.poll() is None:
            if time.monotonic()-last_note>30:
                print('Host supplied '+str(sent[0])+' bytes.',flush=True);last_note=time.monotonic()
            time.sleep(2)
        while status is None:
            try:
                r=shell('cat '+remote_log+' 2>/dev/null; if test -f '+remote_status+'; then echo DEVICE_STATUS; cat '+remote_status+'; fi; true')
                if r.returncode==0:
                    content=r.stdout.replace(b'\r\n',b'\n');current,marker,code=content.partition(b'DEVICE_STATUS\n')
                    if current.startswith(shown):
                        delta=current[len(shown):]
                        if delta:output.write(delta);output.flush();print(delta.decode(errors='replace').rstrip(),flush=True)
                        shown=current
                    else:raise RuntimeError('remote log was replaced or truncated')
                    if marker:status=int(code.strip())
            except (subprocess.TimeoutExpired,ValueError) as e:
                print('Progress read will retry: '+str(e),flush=True)
            if time.monotonic()-last_note>30:
                print('Host supplied '+str(sent[0])+' bytes; awaiting device completion.',flush=True);last_note=time.monotonic()
            if status is None:time.sleep(3)
        sender.join();rc=process.wait()
    record=dict(input_bytes=IMAGE.stat().st_size,input_sha256=SHA,supplied=sent[0],adb_rc=rc,
                device_rc=status,errors=errors,seconds=time.monotonic()-started,
                device_log=remote_log,device_status=remote_status,
                writer_sha256=hashlib.sha256((OUT/'write-ipados-root').read_bytes()).hexdigest())
    record['complete_device_sha256_verified']=(not errors and rc==0 and status==0 and
        sent[0]==IMAGE.stat().st_size and ('IPADOS_ROOT_FULL_SHA256_VERIFIED '+SHA).encode() in shown)
    (OUT/('raw-result-'+stamp+'.json')).write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    if not record['complete_device_sha256_verified']:raise RuntimeError('root install incomplete; inspect recorded status')
    record['device_written']=True
    (OUT/'result.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    print('Root partition completely written and SHA verified.',flush=True)

if __name__=='__main__':main()
