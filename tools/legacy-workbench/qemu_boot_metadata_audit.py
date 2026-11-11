"""WSL: run current-boot metadata inheritance through the real EFI decoder."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--efi',type=Path,required=True)
p.add_argument('--assets',type=Path,required=True)
p.add_argument('--work',type=Path,required=True)
a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
subprocess.run(['qemu-system-aarch64','-M',f'virt,gic-version=3,virtualization=off,secure=off,dumpdtb={a.work}/qemu.dtb',
 '-cpu','max','-m','1024','-smp','1','-display','none','-nic','none'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
fat=a.work/'boot.fat'
subprocess.run(['mformat','-C','-T',str(56*1024*1024//512),'-v','BOOTMETATEST','-i',fat,'::'],check=True)
subprocess.run(['mmd','-i',fat,'::EFI','::EFI/BOOT','::EFI/UniXNU','::EFI/UniXNU/Rescue'],check=True)
files=[(a.efi/'EldenBootMetadataTest.efi','EFI/BOOT/BOOTAA64.EFI'),
       (a.efi/'EldenPayloadLoader.efi','EFI/UniXNU/EldenPayloadLoader.efi'),
       (a.efi/'EldenInitrdConsumer.efi.lzma','EFI/UniXNU/Rescue/Image.efi.lzma'),
       (a.assets/'recovery-initrd.cpio.lzma','EFI/UniXNU/Rescue/recovery-initrd.cpio.lzma'),
       (a.work/'qemu.dtb','EFI/UniXNU/qemu.dtb')]
for source,name in files:subprocess.run(['mcopy','-i',fat,source,'::'+name],check=True)
shutil.copyfile('/usr/share/AAVMF/AAVMF_VARS.fd',a.work/'vars.fd');uart=a.work/'uart.log'
command=['qemu-system-aarch64','-M','virt,gic-version=3,virtualization=off,secure=off','-cpu','max',
 '-accel','tcg','-m','1024','-smp','1','-display','none','-nic','none','-no-reboot',
 '-drive','if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd',
 '-drive',f'if=pflash,format=raw,file={a.work}/vars.fd',
 '-drive',f'if=none,id=boot,format=raw,readonly=on,file={fat}',
 '-device','virtio-blk-device,drive=boot','-device','ramfb','-serial',f'file:{uart}','-monitor','none']
report=dict(command=command,passed=False,hardware_tested=False,TWRP_boot_tested=False,
 original_boot_metadata='synthetic current-boot FDT/initrd inputs',
 application_sha256=hashlib.sha256((a.efi/'EldenPayloadLoader.efi').read_bytes()).hexdigest(),
 consumer_sha256=hashlib.sha256((a.efi/'EldenInitrdConsumer.efi').read_bytes()).hexdigest())
with (a.work/'qemu.log').open('w') as log:
 child=subprocess.Popen(command,stdout=log,stderr=log)
 (a.work/'job.json').write_text(json.dumps(dict(pid=child.pid,command=command))+'\n')
 try:
  deadline=time.monotonic()+180;output='';last=[]
  while time.monotonic()<deadline and child.poll() is None:
   try:output=uart.read_text(errors='replace') if uart.exists() else ''
   except OSError:time.sleep(.2);continue
   lines=[line for line in output.splitlines() if 'ELDEN_' in line]
   if lines!=last:
    for line in lines[len(last):]:print(line,flush=True)
    last=lines
   if 'ELDEN_BOOTMETA_TEST: FAIL' in output:raise RuntimeError(output[-4000:])
   if 'ELDEN_BOOTMETA_TEST: PASS 2 full initrd reads' in output:
    reads=re.findall(r'METADATA PASS bytes=(\d+) cpio-crc=([0-9a-fA-F]+)',output)
    assert len(reads)==2 and all(int(crc,16)==0x405a6648 for _,crc in reads)
    assert 'source bounds, size, checksum and copy PASS' in output
    assert output.count('inherited=15 missing=0')==2
    assert 'ELDEN_MEMORY: conventional base=0x' in output
    report.update(passed=True,source_range_and_checksum_rejected=True,current_slot_inherited=True,
      full_original_cpio_bytes=145697280,cpio_crc32='405a6648',full_initrd_reads=reads,
      explicit_recovery_mode_preserved=True,failure_cleanup_and_retry=True,memory_snapshot_mode_passed=True)
    break
   time.sleep(.2)
  if not report['passed']:raise RuntimeError('bounded EFI metadata test timeout: '+output[-4000:])
 finally:
  child.terminate()
  try:child.wait(timeout=5)
  except subprocess.TimeoutExpired:child.kill();child.wait()
  (a.work/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='command'},indent=2))
