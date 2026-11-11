"""WSL: launch the actual external UniXNU bundle, capture menu, test return reset."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import threading
import time
import uuid
from stage2_store import Bundle, MIB, encode

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--assets',type=Path,required=True)
p.add_argument('--bundle',type=Path,required=True)
p.add_argument('--work',type=Path,required=True)
p.add_argument('--xnu-reference',type=Path,help='QEMU ADT and original-kernel registration metadata')
p.add_argument('--driver',type=Path,help='recorded neutral-debug DarwinBoot for kernel path only')
a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
assert bool(a.xnu_reference)==bool(a.driver)
original_sha=hashlib.sha256(a.bundle.read_bytes()).hexdigest()
if a.xnu_reference:
    metadata=json.loads((a.xnu_reference/'static-audit.json').read_text())
    assert metadata['candidate_sha256']=='3fd40e9f88bbcfdde801d9d5bfed9a9a836921393ff5bfed2c1058137c73e4b6'
    assert hashlib.sha256(a.driver.read_bytes()).hexdigest()=='8dee9a1a542cac747421723bcb31a7c52ec0975a25a99c9458e6ebb12c38743f'
    target=a.work/'bundle-qemu.fat';shutil.copyfile(a.bundle,target);a.bundle=target
    subprocess.run(['mcopy','-o','-i',a.bundle,a.xnu_reference/'qemu.adt','::EFI/UniXNU/Payloads/kernel.macho.adt'],check=True)
payload=a.bundle.read_bytes();sha=hashlib.sha256(payload).hexdigest()
assert len(payload)==56*MIB
(a.work/'bundle.header').write_bytes(encode(Bundle(0,1,len(payload),64*MIB,bytes.fromhex(sha))))
fat=a.work/'boot.fat'
subprocess.run(['mformat','-C','-T',str(80*MIB//512),'-v','EXTTEST','-i',fat,'::'],check=True)
subprocess.run(['mmd','-i',fat,'::EFI','::EFI/BOOT','::EFI/UniXNU'],check=True)
wrong=a.work/'wrong-parent.toml'
wrong.write_text('version = 1\n[boot]\ntimeout = -1\nfallback_timeout = 600\n[[entry]]\nid = "wrong"\ntitle = "WRONG PARENT CONFIG"\ntype = "action"\naction = "exit"\n')
for source,name in [(a.assets/'EldenExternalTest.efi','EFI/BOOT/BOOTAA64.EFI'),(a.bundle,'bundle.fat'),
                    (a.work/'bundle.header','bundle.header'),(wrong,'EFI/UniXNU/unixnu.toml')]:
    subprocess.run(['mcopy','-i',fat,source,'::'+name],check=True)
if a.driver:subprocess.run(['mcopy','-i',fat,a.driver,'::test-driver.efi'],check=True)
shutil.copyfile('/usr/share/AAVMF/AAVMF_VARS.fd',a.work/'vars.fd')
uart=a.work/'uart.log';sock=Path('/tmp')/('elden-external-'+uuid.uuid4().hex+'.sock')
command=['qemu-system-aarch64','-M','virt,gic-version=3,virtualization=off,secure=off',
 '-cpu','max','-accel','tcg','-m','1024','-smp','1','-display','none','-nic','none','-no-reboot',
 '-drive','if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd',
 '-drive',f'if=pflash,format=raw,file={a.work}/vars.fd',
 '-drive',f'if=none,id=boot,format=raw,readonly=on,file={fat}',
 '-device','virtio-blk-device,drive=boot','-device','ramfb','-device','qemu-xhci','-device','usb-kbd',
 '-serial',f'file:{uart}','-qmp',f'unix:{sock},server=on,wait=off','-monitor','none']
if a.xnu_reference:
    with socket.socket() as reserve:reserve.bind(('127.0.0.1',0));gdb_port=reserve.getsockname()[1]
    command+=['-gdb',f'tcp:127.0.0.1:{gdb_port}','-S']
report=dict(command=command,hardware_tested=False,passed=False,bundle_sha256=sha,
            original_bundle_sha256=original_sha,QEMU_ADT_substituted=bool(a.xnu_reference),
            launcher_sha256=hashlib.sha256((a.assets/'EldenExternalTest.efi').read_bytes()).hexdigest(),
            storage_transport='UEFI file fixture; no UFS emulation or tablet I/O')
with (a.work/'qemu.log').open('w') as log:
    child=subprocess.Popen(command,stdout=log,stderr=log)
    (a.work/'job.json').write_text(json.dumps(dict(pid=child.pid,command=command))+'\n')
    connection=None;gdb=None;runner=None;reply={}
    try:
        deadline=time.monotonic()+160;output=''
        if a.xnu_reference:
            sys.path.insert(0,str(a.xnu_reference));from gdb_packets import Packets
            for _ in range(100):
                try:gdb=socket.create_connection(('127.0.0.1',gdb_port),timeout=1);break
                except ConnectionRefusedError:time.sleep(.05)
            assert gdb is not None
            gdb.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1);wire=Packets(gdb)
            def request(value):
                gdb.settimeout(max(.1,deadline-time.monotonic()));return wire.request(value)
            assert 'aarch64' in request('qXfer:features:read:target.xml:0,1000')
            gate=int(metadata['storage_ready_function'],16);panic=0xfffffff007980214
            assert request(f'Z1,{gate:x},4')=='OK';assert request(f'Z1,{panic:x},4')=='OK'
            def run_guest():
                try:reply['stop']=request('c')
                except Exception as error:reply['error']=str(error)
            runner=threading.Thread(target=run_guest,daemon=True);runner.start()
        while time.monotonic()<deadline and child.poll() is None:
            try:output=uart.read_text(errors='replace') if uart.exists() else ''
            except OSError:time.sleep(.2);continue
            if 'ELDEN_EXTERNAL_TEST: FAIL' in output:raise RuntimeError(output[-5000:])
            if 'ELDEN_STAGE2: starting verified RAM bundle' in output:break
            time.sleep(.2)
        else:raise RuntimeError('launch timeout '+output[-5000:])
        connection=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);connection.connect(str(sock));connection.settimeout(10)
        reader=connection.makefile('rwb',buffering=0);json.loads(reader.readline())
        def qmp(name,arguments=None):
            message={'execute':name}
            if arguments is not None:message['arguments']=arguments
            reader.write((json.dumps(message)+'\n').encode())
            while True:
                reply=json.loads(reader.readline())
                if 'event' in reply:continue
                if 'error' in reply:raise RuntimeError(reply)
                return reply
        qmp('qmp_capabilities');time.sleep(4)
        qmp('screendump',{'filename':str(a.work/'menu.ppm')})
        print('Captured real external menu; selecting '+('XNU' if a.xnu_reference else 'safe return'),flush=True)
        if a.xnu_reference:
            for _ in range(8):
                qmp('send-key',{'keys':[{'type':'qcode','data':'up'}],'hold-time':60});time.sleep(.15)
        qmp('send-key',{'keys':[{'type':'qcode','data':'ret'}]})
        if a.xnu_reference:
            runner.join(timeout=max(.1,deadline-time.monotonic()))
            if runner.is_alive() or 'error' in reply:raise RuntimeError('kernel gate wait '+str(reply))
            def reg(number):return int.from_bytes(bytes.fromhex(request(f'p{number:x}')),'little')
            report['first_stop_pc']=hex(reg(32))
            if reg(32)!=gate:raise RuntimeError('kernel registration gate not reached')
            caller=reg(30);assert request(f'z1,{gate:x},4')=='OK';assert request(f'Z1,{caller:x},4')=='OK'
            request('c');assert reg(32)==caller and reg(0)==1
            report.update(passed=True,kernel_booted=True,kernel_sha256=metadata['candidate_sha256'],
                storage_registration_return=reg(0),stopped_before_UFS_MMIO=True,guest_code_or_PC_patched=False,
                DarwinBoot_sha256=hashlib.sha256(a.driver.read_bytes()).hexdigest(),
                firmware_environment='AAVMF plus neutral-debug DarwinBoot; not full tablet FD')
            qmp('screendump',{'filename':str(a.work/'kernel.ppm')})
        while time.monotonic()<deadline:
            if a.xnu_reference:break
            try:output=uart.read_text(errors='replace')
            except OSError:time.sleep(.2);continue
            if child.poll() is not None:break
            time.sleep(.2)
        if not a.xnu_reference and 'external code returned Success; restarting fixed bootstrap' not in output:
            raise RuntimeError('production reset path not observed '+output[-5000:])
        if not a.xnu_reference:
            if child.poll() is None:raise RuntimeError('reset did not stop no-reboot fixture')
            report.update(passed=True,return_via_cold_reset=True,exit_code=child.returncode)
    finally:
        if connection is not None:connection.close()
        if child.poll() is None:
            child.terminate()
            try:child.wait(timeout=5)
            except subprocess.TimeoutExpired:child.kill();child.wait()
        if gdb is not None:gdb.close()
        if runner is not None:runner.join(timeout=1)
        (a.work/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='command'},indent=2))
