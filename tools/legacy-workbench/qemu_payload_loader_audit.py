"""WSL: execute the real EFI decoder against complete assets in AAVMF/QEMU."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import subprocess
import sys
import time
import uuid
import zlib

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--assets',type=Path,required=True)
p.add_argument('--work',type=Path,required=True)
p.add_argument('--timeout',type=int,default=180)
p.add_argument('--boot-xnu',action='store_true',help='run accepted DarwinBoot through EBS to the real kernel registration gate')
p.add_argument('--reference',type=Path,help='qualified kernel QEMU fixture containing metadata and GDB packet helper')
p.add_argument('--driver',type=Path,help='explicit alternate DarwinBoot test binary; reported separately from the accepted FD binary')
p.add_argument('--recovery-contract',action='store_true',help='replace only the Linux kernel with an independent EFI initrd consumer')
p.add_argument('--fdt-mailbox-contract',action='store_true',help='exercise live FDT mailbox rejection, copy and restoration')
a=p.parse_args()
if a.fdt_mailbox_contract:a.recovery_contract=True
assert not (a.boot_xnu and a.recovery_contract)
assets=a.assets.resolve();work=a.work.resolve();work.mkdir(parents=True,exist_ok=True)
job=work/'job.json'
if job.exists():
    previous=json.loads(job.read_text())
    try:
        os.kill(previous['pid'],0)
    except ProcessLookupError:
        pass
    else:
        raise SystemExit('Previous fixture PID is still live; inspect it before restarting')
report=json.loads((assets/'compression-report.json').read_text())
assert report['compression_complete']
names=['kernel.macho','Image.efi','recovery-initrd.cpio']
expected={}
for index,name in enumerate(names):
    data=(assets/name).read_bytes()
    item=report['assets'][name]
    assert hashlib.sha256(data).hexdigest()==item['sha256']
    assert hashlib.sha256((assets/(name+'.lzma')).read_bytes()).hexdigest()==item['compressed_sha256']
    expected[index]=(len(data),zlib.crc32(data)&0xffffffff)
fat=work/'boot-fat.img'
subprocess.run(['mformat','-C','-T',str(56*1024*1024//512),'-v','ELDENAUDIT','-i',fat,'::'],check=True)
subprocess.run(['mmd','-i',fat,'::EFI','::EFI/BOOT','::EFI/UniXNU','::EFI/UniXNU/Payloads','::EFI/UniXNU/Rescue'],check=True)
files=[(assets/('EldenLoaderTest.efi' if a.boot_xnu else 'EldenPayloadLoader.efi'),'EFI/BOOT/BOOTAA64.EFI'),
       (assets/'kernel.macho.lzma','EFI/UniXNU/Payloads/kernel.macho.lzma'),
       (assets/'Image.efi.lzma','EFI/UniXNU/Rescue/Image.efi.lzma'),
       (assets/'recovery-initrd.cpio.lzma','EFI/UniXNU/Rescue/recovery-initrd.cpio.lzma')]
if a.recovery_contract:
    subprocess.run(['qemu-system-aarch64','-M',f'virt,gic-version=3,virtualization=off,secure=off,dumpdtb={work}/qemu.dtb',
                    '-cpu','max','-m','1024','-smp','1','-display','none','-nic','none'],check=True,
                   stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    files[0]=(assets/('EldenFdtMailboxTest.efi' if a.fdt_mailbox_contract else 'EldenRecoveryTest.efi'),'EFI/BOOT/BOOTAA64.EFI')
    files[2]=(assets/'EldenInitrdConsumer.efi.lzma','EFI/UniXNU/Rescue/Image.efi.lzma')
    files.append((assets/'EldenPayloadLoader.efi','EFI/UniXNU/EldenPayloadLoader.efi'))
    files.append((work/'qemu.dtb','EFI/UniXNU/qemu.dtb'))
if a.boot_xnu:
    assert a.reference is not None
    metadata=json.loads((a.reference/'static-audit.json').read_text())
    assert metadata['candidate_sha256']==report['assets']['kernel.macho']['sha256']
    driver=a.driver.resolve() if a.driver else assets/'DarwinBoot.efi'
    driver_sha=hashlib.sha256(driver.read_bytes()).hexdigest()
    if not a.driver:assert driver_sha=='b3c05c02ca9122d5f2cd1d855635148cbfdaa210b824e8a0a0fafb7867c5511b'
    subprocess.run(['mmd','-i',fat,'::EFI/UniXNU/Drivers'],check=True)
    files += [(assets/'EldenPayloadLoader.efi','EFI/UniXNU/EldenPayloadLoader.efi'),
              (driver,'EFI/UniXNU/Drivers/DarwinBoot.efi'),
              (a.reference/'qemu.adt','EFI/UniXNU/Payloads/kernel.macho.adt')]
for source,destination in files:
    subprocess.run(['mcopy','-i',fat,source,'::'+destination],check=True)
shutil.copyfile('/usr/share/AAVMF/AAVMF_VARS.fd',work/'vars.fd')
sock=Path('/tmp')/('elden-payload-'+uuid.uuid4().hex+'.sock')
uart=work/'uart.log'
command=['qemu-system-aarch64','-M','virt,gic-version=3,virtualization=off,secure=off',
    '-cpu','max','-accel','tcg','-m','1024','-smp','1','-display','none','-nic','none','-no-reboot',
    '-drive','if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd',
    '-drive',f'if=pflash,format=raw,file={work}/vars.fd',
    '-drive',f'if=none,id=boot,format=raw,readonly=on,file={fat}',
    '-device','virtio-blk-device,drive=boot','-device','ramfb',
    '-serial',f'file:{uart}','-qmp',f'unix:{sock},server=on,wait=off','-monitor','none']
if a.boot_xnu:
    with socket.socket() as reserve:
        reserve.bind(('127.0.0.1',0));gdb_port=reserve.getsockname()[1]
    command += ['-gdb',f'tcp:127.0.0.1:{gdb_port}','-S']
result=dict(application_sha256=hashlib.sha256((assets/'EldenPayloadLoader.efi').read_bytes()).hexdigest(),
            expected={str(k):dict(bytes=v[0],crc32=f'{v[1]:08x}') for k,v in expected.items()},
            firmware_decoder_executed=False,kernel_booted=False,hardware_tested=False,command=command)
result['mode']='xnu_handoff' if a.boot_xnu else 'decode_audit'
if a.recovery_contract:
    result.update(mode='recovery_contract',real_linux_kernel_used=False,TWRP_boot_tested=False,
        consumer_sha256=hashlib.sha256((assets/'EldenInitrdConsumer.efi').read_bytes()).hexdigest(),
        original_initrd_sha256=report['assets']['recovery-initrd.cpio']['sha256'])
    result['fdt_mailbox_contract']=a.fdt_mailbox_contract
if a.boot_xnu:
    result['DarwinBoot_sha256']=driver_sha
    result['exact_accepted_DarwinBoot']=driver_sha=='b3c05c02ca9122d5f2cd1d855635148cbfdaa210b824e8a0a0fafb7867c5511b'
with (work/'qemu.log').open('w') as log:
    child=subprocess.Popen(command,stdout=log,stderr=log)
    job.write_text(json.dumps(dict(pid=child.pid,command=command))+'\n')
    connection=None;gdb=None;wire=None;guest_running=False
    try:
        deadline=time.monotonic()+a.timeout
        if a.boot_xnu:
            sys.path.insert(0,str(a.reference))
            from gdb_packets import Packets
            for _ in range(100):
                try:
                    gdb=socket.create_connection(('127.0.0.1',gdb_port),timeout=1);break
                except ConnectionRefusedError:
                    if child.poll() is not None:raise RuntimeError('QEMU exited before GDB connection')
                    time.sleep(.05)
            assert gdb is not None
            gdb.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
            wire=Packets(gdb)
            def request(value):
                gdb.settimeout(max(.1,deadline-time.monotonic()))
                return wire.request(value)
            def reg(number):
                raw=request(f'p{number:x}')
                if not re.fullmatch('[0-9a-fA-F]{16}',raw):
                    raise RuntimeError(f'invalid GDB register {number}: {raw!r}')
                return int.from_bytes(bytes.fromhex(raw),'little')
            def memory(address,size):
                return bytes.fromhex(request(f'm{address:x},{size:x}'))
            gate=int(metadata['storage_ready_function'],16)
            panic=0xfffffff007980214
            # QEMU enables its individual-register packets only after XML
            # target-description negotiation (gdb_has_xml).
            description=request('qXfer:features:read:target.xml:0,1000')
            assert 'aarch64' in description, 'unexpected debugger architecture'
            assert request(f'Z1,{gate:x},4')=='OK'
            assert request(f'Z1,{panic:x},4')=='OK'
            print('Running EFI loader and recorded DarwinBoot binary; waiting for XNU',flush=True)
            guest_running=True
            result['gdb_stop_reply']=request('c')
            guest_running=False
            stopped=reg(32)
            if stopped!=gate:
                result['failure_pc']=hex(stopped)
                if stopped==panic:
                    result['panic_format']=memory(reg(0),256).split(b'\0',1)[0].decode(errors='replace')
                raise RuntimeError('kernel registration gate was not reached')
            caller=reg(30)
            assert request(f'z1,{gate:x},4')=='OK'
            assert request(f'Z1,{caller:x},4')=='OK'
            guest_running=True
            request('c')
            guest_running=False
            assert reg(32)==caller and reg(0)==1
            result.update(firmware_decoder_executed=True,kernel_booted=True,
                UEFI_handoff_tested=True,firmware_environment='AAVMF; not the full Y700 vendor FD',
                kernel_sha256=metadata['candidate_sha256'],storage_registration_return=reg(0),
                stopped_before_UFS_MMIO=True,guest_code_or_PC_patched=False)
            print('EFI -> recorded DarwinBoot -> XNU -> storage class registration: PASS',flush=True)
        previous_lines=[]
        while not a.boot_xnu and time.monotonic()<deadline:
            try:
                text=uart.read_text(errors='replace') if uart.exists() else ''
            except OSError as error:
                if child.poll() is not None:raise
                result.setdefault('observer_read_errors',[]).append(error.errno)
                time.sleep(.25)
                continue
            text=re.sub(r'\x1b\[[0-?]*[ -/]*[@-~]','',text)
            lines=[line.strip() for line in text.splitlines() if any(mark in line for mark in
                ('ELDEN_LOADER:','ELDEN_INITRD_CONSUMER:','ELDEN_RECOVERY_TEST:','ELDEN_MAILBOX_TEST:'))]
            if lines!=previous_lines:
                for line in lines[len(previous_lines):]:print(line,flush=True)
                previous_lines=lines
            completion='ELDEN_MAILBOX_TEST: PASS 4 rounds, mailbox and original initrd' if a.fdt_mailbox_contract else 'ELDEN_RECOVERY_TEST: PASS 2 rounds, original initrd'
            if a.recovery_contract and completion in text:
                values=[(int(n),int(crc,16)) for n,crc in re.findall(
                    r'ELDEN_INITRD_CONSUMER: PASS bytes=(\d+) crc32=([0-9a-fA-F]{8})',text)]
                assert values==[expected[2],expected[2]], 'recovery protocol consumer did not read the original initrd twice'
                result.update(firmware_decoder_executed=True,loadfile2_contract_passed=True,
                              error_return_cleanup_passed=True,fresh_retry_passed=True,
                              original_initrd_read_count=len(values))
                if a.fdt_mailbox_contract:result.update(invalid_pointer_rejected=True,oversize_FDT_rejected=True,live_FDT_copy_and_restore_passed=True)
                break
            if not a.recovery_contract and 'ELDEN_LOADER: AUDIT COMPLETE' in text:
                actual={int(i):(int(n),int(crc,16)) for i,n,crc in re.findall(
                    r'ELDEN_LOADER: audit (\d+) bytes=(\d+) crc32=([0-9a-fA-F]{8})',text)}
                assert actual==expected, 'EFI decoder output length/CRC differs from original assets'
                result['firmware_decoder_executed']=True
                result['actual']={str(k):dict(bytes=v[0],crc32=f'{v[1]:08x}') for k,v in actual.items()}
                break
            if child.poll() is not None:raise RuntimeError('QEMU exited before audit completion')
            if 'ELDEN_LOADER: audit error' in text:raise RuntimeError('EFI decoder reported an error')
            if 'ELDEN_RECOVERY_TEST: FAIL' in text:raise RuntimeError('EFI recovery contract test failed')
            if 'ELDEN_MAILBOX_TEST: FAIL' in text:raise RuntimeError('EFI FDT mailbox contract test failed')
            time.sleep(.25)
        if not result['firmware_decoder_executed']:raise RuntimeError('bounded EFI audit timeout')
    except Exception as error:
        result['error']=str(error) or type(error).__name__
        if wire is not None:
            try:
                gdb.settimeout(3)
                if guest_running:wire.request(None)
                deadline=time.monotonic()+3
                result['failure_pc']=hex(reg(32))
            except Exception:pass
    finally:
        if child.poll() is None:
            try:
                connection=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);connection.settimeout(2);connection.connect(str(sock))
                reader=connection.makefile('r')
                reader.readline()
                def qmp(name,arguments=None):
                    request={'execute':name}
                    if arguments is not None:request['arguments']=arguments
                    connection.sendall((json.dumps(request)+'\n').encode())
                    while True:
                        reply=json.loads(reader.readline())
                        if 'return' in reply or 'error' in reply:return reply
                qmp('qmp_capabilities')
                result['qemu_status']=qmp('query-status')
                result['screenshot']=qmp('screendump',{'filename':str(work/'frame.ppm')})
                qmp('quit')
                child.wait(timeout=3)
            except Exception:
                child.terminate()
                try:child.wait(timeout=3)
                except subprocess.TimeoutExpired:child.kill();child.wait()
        if connection is not None:connection.close()
        if gdb is not None:gdb.close()
        sock.unlink(missing_ok=True)
result['process_exit_code']=child.returncode
(work/'audit.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps({key:value for key,value in result.items() if key!='command'},indent=2),flush=True)
if not result['firmware_decoder_executed']:raise SystemExit(1)
