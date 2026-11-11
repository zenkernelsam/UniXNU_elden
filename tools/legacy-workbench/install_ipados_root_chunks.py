"""Install the fixed root through bounded ADB sync chunks and root-owned staging.

No binary data passes through Windows adb shell stdin. Whole-image validation
precedes the bounded block writer, whose header commit follows device readback.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import time
from install_ipados_root import IMAGE,SHA,OUT,PREFIX,REMOTE,file_hash
CHUNK=128<<20

def shell(command,root=False,timeout=90):
    if root:command='su -c '+shlex.quote(command)
    r=subprocess.run([*PREFIX,'shell','-T',command],capture_output=True,timeout=timeout)
    if r.returncode:raise RuntimeError((r.stdout+r.stderr).decode(errors='replace'))
    return r.stdout

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--write-reviewed-root',action='store_true',required=True);p.parse_args()
    assert IMAGE.stat().st_size==9381609472 and file_hash(IMAGE)==SHA
    assert shell('id -u',True).strip()==b'0'
    assert not shell('getprop ro.twrp.version').strip()
    assert b'TARGET VERIFIED' in shell(REMOTE+' --check',True)
    stamp=time.strftime('%Y%m%d-%H%M%S');stage='/data/local/tmp/elden-root-staged-'+stamp+'.img'
    chunkpath='/data/local/tmp/elden-root-chunk-'+stamp+'.bin'
    state=dict(input_sha256=SHA,stage=stage,chunk_path=chunkpath,chunks=[],device_written=False)
    statepath=OUT/('chunk-stage-'+stamp+'.json');logpath=OUT/('chunk-write-'+stamp+'.log')
    temp=OUT/('upload-chunk-'+stamp+'.bin');start=time.monotonic()
    shell('test ! -e '+stage+' && (set -C; : > '+stage+') && chmod 600 '+stage,True)
    with IMAGE.open('rb') as source:
        at=0
        while data:=source.read(CHUNK):
            temp.write_bytes(data);expected=hashlib.sha256(data).hexdigest()
            r=subprocess.run([*PREFIX,'push','-Z',str(temp),chunkpath],capture_output=True,timeout=180)
            (OUT/('chunk-push-'+stamp+'.log')).open('ab').write(r.stdout+r.stderr)
            if r.returncode:raise RuntimeError('ADB chunk failed: '+(r.stdout+r.stderr).decode(errors='replace'))
            actual=shell('sha256sum '+chunkpath).decode().split()[0]
            if actual!=expected:raise RuntimeError('chunk transport hash mismatch')
            if int(shell('stat -c %s '+stage,True).strip())!=at:raise RuntimeError('staging file length changed')
            shell('dd if='+chunkpath+' of='+stage+' bs=1048576 seek='+str(at//1048576)+' conv=notrunc 2>/dev/null',True)
            at+=len(data)
            if int(shell('stat -c %s '+stage,True).strip())!=at:raise RuntimeError('staging append length mismatch')
            state['chunks'].append(dict(end=at,bytes=len(data),sha256=expected))
            statepath.write_text(json.dumps(state,indent=2)+'\n',encoding='utf-8')
            print('Staged and checked '+str(at)+' / '+str(IMAGE.stat().st_size),flush=True)
    digest=shell('sha256sum '+stage,True,timeout=180).decode().split()[0]
    if digest!=SHA:raise RuntimeError('complete staged image digest mismatch')
    print('Complete staged image SHA verified; starting bounded partition write.',flush=True)
    state['staged_sha256']=digest;statepath.write_text(json.dumps(state,indent=2)+'\n',encoding='utf-8')
    with logpath.open('xb') as log:
        child=subprocess.Popen([*PREFIX,'shell','-T','su -c '+shlex.quote(REMOTE+' --write-image < '+stage)],
                               stdin=subprocess.DEVNULL,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        for line in child.stdout:
            log.write(line);log.flush();print(line.decode(errors='replace').rstrip(),flush=True)
        rc=child.wait()
    complete='IPADOS_ROOT_FULL_SHA256_VERIFIED '+SHA
    if rc or complete not in logpath.read_text(errors='replace'):raise RuntimeError('device write/verification incomplete')
    state.update(device_written=True,complete_device_sha256_verified=True,seconds=time.monotonic()-start,
                 writer_sha256=hashlib.sha256((OUT/'write-ipados-root').read_bytes()).hexdigest())
    statepath.write_text(json.dumps(state,indent=2)+'\n',encoding='utf-8')
    (OUT/'result.json').write_text(json.dumps(state,indent=2)+'\n',encoding='utf-8')
    shell('rm -- '+stage,True);shell('rm -- '+chunkpath);temp.unlink()
    print('IPADOS ROOT INSTALLED AND VERIFIED.',flush=True)

if __name__=='__main__':main()
