"""WSL: actual EFI platform rejection, SHA, RAM FAT, cleanup and retry."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import time
from stage2_store import Bundle, MIB, encode

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--assets',type=Path,required=True)
p.add_argument('--work',type=Path,required=True)
a=p.parse_args();a.work.mkdir(parents=True,exist_ok=True)
subprocess.run(['qemu-system-aarch64','-M',f'virt,gic-version=3,virtualization=off,secure=off,dumpdtb={a.work}/qemu.dtb',
                '-cpu','max','-m','1024','-smp','1','-display','none','-nic','none'],check=True,
                stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
bundle=a.work/'bundle.fat'
subprocess.run(['mformat','-C','-T',str(8*MIB//512),'-v','STAGE2TEST','-i',bundle,'::'],check=True)
subprocess.run(['mmd','-i',bundle,'::EFI','::EFI/BOOT'],check=True)
subprocess.run(['mcopy','-i',bundle,a.assets/'EldenStage2Consumer.efi','::EFI/BOOT/BOOTAA64.EFI'],check=True)
(a.work/'proof.txt').write_bytes(b'independent ram fat\n')
subprocess.run(['mcopy','-i',bundle,a.work/'proof.txt','::proof.txt'],check=True)
data=bundle.read_bytes();header=encode(Bundle(0,1,len(data),64*MIB,hashlib.sha256(data).digest()))
(a.work/'bundle.header').write_bytes(header)
fat=a.work/'boot.fat'
subprocess.run(['mformat','-C','-T',str(32*MIB//512),'-v','UFSSTAGE2','-i',fat,'::'],check=True)
subprocess.run(['mmd','-i',fat,'::EFI','::EFI/BOOT'],check=True)
for source,name in [(a.assets/'EldenStage2Test.efi','EFI/BOOT/BOOTAA64.EFI'),
                    (a.assets/'EldenUfsProbe.efi','probe.efi'),(bundle,'bundle.fat'),
                    (a.work/'bundle.header','bundle.header'),(a.work/'qemu.dtb','qemu.dtb')]:
    subprocess.run(['mcopy','-i',fat,source,'::'+name],check=True)
shutil.copyfile('/usr/share/AAVMF/AAVMF_VARS.fd',a.work/'vars.fd')
uart=a.work/'uart.log'
command=['qemu-system-aarch64','-M','virt,gic-version=3,virtualization=off,secure=off',
 '-cpu','max','-accel','tcg','-m','1024','-smp','1','-display','none','-nic','none','-no-reboot',
 '-drive','if=pflash,format=raw,readonly=on,file=/usr/share/AAVMF/AAVMF_CODE.fd',
 '-drive',f'if=pflash,format=raw,file={a.work}/vars.fd',
 '-drive',f'if=none,id=boot,format=raw,readonly=on,file={fat}',
 '-device','virtio-blk-device,drive=boot','-device','ramfb','-serial',f'file:{uart}','-monitor','none']
report=dict(command=command,hardware_tested=False,passed=False,
            efi={name:hashlib.sha256((a.assets/name).read_bytes()).hexdigest()
                 for name in ['EldenUfsProbe.efi','EldenStage2Test.efi','EldenStage2Consumer.efi']})
with (a.work/'qemu.log').open('w') as log:
    child=subprocess.Popen(command,stdout=log,stderr=log)
    try:
        deadline=time.monotonic()+100
        output=''
        while time.monotonic()<deadline and child.poll() is None:
            try:output=uart.read_text(errors='replace') if uart.exists() else ''
            except OSError:time.sleep(.2);continue
            if 'ELDEN_STAGE2_TEST: FAIL' in output:raise RuntimeError(output[-5000:])
            if 'ELDEN_STAGE2_TEST: PASS' in output:
                assert output.count('ELDEN_CONSUMER: independent volume proof ok')==2
                assert 'non-SM8750 rejected before MMIO' in output
                report['passed']=True;break
            time.sleep(.2)
        if not report['passed']:raise RuntimeError('EFI contract timeout: '+output[-5000:])
    finally:
        child.terminate()
        try:child.wait(timeout=5)
        except subprocess.TimeoutExpired:child.kill();child.wait()
        (a.work/'audit.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
