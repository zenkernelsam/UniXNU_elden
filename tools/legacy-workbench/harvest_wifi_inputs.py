"""Offline Y700 Wi-Fi source/firmware catalog; never opens a device."""
import hashlib,json,mmap,subprocess
from pathlib import Path
from audit_boot_candidate import Fat
ROOT=Path(__file__).resolve().parents[1]
REV='6f97250ead155fc3b980856323e99a4623dae73d'
OUT=ROOT/'research/wifi-elden-20261108';OUT.mkdir(exist_ok=True)
def sha(b):return hashlib.sha256(b).hexdigest()
def save(name,data):
    p=OUT/name
    if p.exists():assert p.read_bytes()==data,'Existing source differs: '+name
    else:p.write_bytes(data)
sources={}
for file in ['arch/arm64/boot/dts/qcom/sm8750-lenovo-elden.dts','arch/arm64/boot/dts/qcom/sm8750.dtsi',
             'drivers/net/wireless/ath/ath12k/pci.c','drivers/net/wireless/ath/ath12k/core.c',
             'drivers/net/wireless/ath/ath12k/hw.h']:
    r=subprocess.run(['git','-C',str(ROOT/'research/tb322fc-linux-kernel'),'show',REV+':'+file],capture_output=True,check=True)
    name=Path(file).name;save(name,r.stdout);sources[name]=dict(repository='h0cheung/tb322fc-linux-kernel',revision=REV,path=file,sha256=sha(r.stdout))
board=(OUT/'sm8750-lenovo-elden.dts').read_text();soc=(OUT/'sm8750.dtsi').read_text()
assert 'compatible = "pci17cb,110e"' in board and 'qcom,msi-iova = /bits/ 64 <0x18000000>' in board
assert 'wlan-enable-gpios = <&tlmm 16 GPIO_ACTIVE_HIGH>' in board
assert 'reset-gpios = <&tlmm 102 GPIO_ACTIVE_LOW>' in board and 'wake-gpios = <&tlmm 104 GPIO_ACTIVE_LOW>' in board
assert '0x100 &apps_smmu 0x1401 0x0 0x1' in soc
backup=ROOT/'Backup/B_edl_stock/lun4/modem_a.bin';assets=[]
with backup.open('rb') as f,mmap.mmap(f.fileno(),0,access=mmap.ACCESS_READ) as data:
    fat=Fat(data);assert fat.bps==4096 and fat.bits==16
    visited=set()
    def walk(cluster,prefix,depth):
        assert depth<8 and cluster not in visited
        visited.add(cluster)
        for name,(first,size,flags) in fat.directory(cluster).items():
            if name in ('.','..'):continue
            path=prefix+'/'+name
            if flags&0x10:walk(first,path,depth+1)
            elif 'peach' in path or name.startswith(('qwlan','bdwlan','phy_ucode','tme_patch','wcnss')):
                b=fat.read(path) if size else b'';assert len(b)==size
                item=dict(path=path,bytes=size,sha256=sha(b))
                if b[:4]==b'\x7fELF':item.update(elf_class=b[4],elf_machine=int.from_bytes(b[18:20],'little'))
                assets.append(item)
    walk(fat.root_cluster,'',0)
    backup_sha=sha(data)
facts=dict(pci_vendor=0x17cb,pci_device=0x110e,wlan_enable_tlmm=16,perst_tlmm=102,wake_tlmm=104,
    endpoint_rid=0x100,apps_smmu_base=0x15000000,endpoint_sid=0x1401,msi_iova=0x18000000,
    msi_spi=list(range(141,149)),global_spi=140,phy=dict(base=0x1c06000,bytes=0x2000,lanes=2),
    windows=dict(parf=[0x1c00000,0x3000],dbi=[0x40000000,0xf1d],elbi=[0x40000f20,0xa8],
        atu=[0x40001000,0x1000],config=[0x40100000,0x100000],mhi=[0x1c03000,0x1000]),
    hardware_tested=False,msi_note='0x18000000 is an IOVA, not proof of a usable physical MSI destination',
    power_note='Use the Y700 six-rail/shared-owner facts; do not import OnePlus seven-rail policy')
report=dict(sources=sources,resources=facts,backup=dict(bytes=backup.stat().st_size,sha256=backup_sha),
    firmware_assets=assets,device_called=False,firmware_extracted=False,
    native_network_published=False,notes=['Runtime PCI identity and DMA route still require live qualification',
        'Original IO80211Family is absent from the present30-module kernel; native ABI/prelink/capacity remain work items'])
(OUT/'inputs.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(sources=len(sources),firmware_assets=len(assets),assets=[dict(path=a['path'],bytes=a['bytes']) for a in assets if 'bdwlan.e' not in a['path']],
    device_operations=False,resources='17cb:110e; RID100/SID1401; MSI IOVA18000000'),indent=2))
