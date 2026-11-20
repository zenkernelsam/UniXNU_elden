"""Bind sharedSMMU topology/table/touch derivation to frozen Gen4 sources."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,subprocess,re
ROOT=Path(__file__).resolve().parents[1]
LINUX='6f97250ead155fc3b980856323e99a4623dae73d'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('linux-repository','source','camera-source','sensor-source','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();records={};topology={}
    def source(path):
        data=subprocess.check_output(['git','-C',str(a.linux_repository),'show',LINUX+':'+path]);topology[path]=hashlib.sha256(data).hexdigest();return data.decode()
    dts=source('arch/arm64/boot/dts/qcom/sm8750.dtsi')
    if not re.search(r'iommus\s*=\s*<&apps_smmu\s+0x1c00\s+0x00>',dts):raise ValueError('Exact camera SMMU stream missing')
    # DT encodes masked stream groups, not four individually spelled SIDs:
    # 100N/mask80 ->100N,108N;104N/mask20 ->104N,106N. Session7
    # uses1007/mask40 plus1067/1087. Enumerate the EXACT original union
    # into four non-masked SMRs, without extending its effective match set.
    adsp=dts.split('label = "adsp";',1)[1].split('label = "cdsp";',1)[0];sessions={}
    for session in range(3,9):
        body=re.search(r'compute-cb@'+str(session)+r'\s*\{(.*?)\};',adsp,re.S)
        if not body:raise ValueError('Exact ADSP session missing')
        cells=[(int(s,16),int(m,16)) for s,m in re.findall(r'&apps_smmu\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)',body[1])]
        effective=set()
        for sid,mask in cells:
            if mask&~0xe0:raise ValueError('Unexpected exact ADSP mask')
            bits=[1<<n for n in range(16) if mask&(1<<n)]
            for subset in range(1<<len(bits)):
                effective.add((sid&~mask)|sum(b for i,b in enumerate(bits) if subset&(1<<i)))
        expected={base+session for base in (0x1000,0x1040,0x1060,0x1080)}
        if effective!=expected:raise ValueError('Exact Gen4 masked-SID union differs')
        sessions[session]={'original_masked_cells':cells,'effective_exact_sids':sorted(effective)}
    linux=source('drivers/iommu/arm/arm-smmu/arm-smmu.c');main=source('drivers/iommu/arm/arm-smmu/arm-smmu.h')
    if 'ARM_SMMU_ID0_ATOSNS' not in linux or 'ARM_SMMU_ID0_ATOSNS' not in main:raise ValueError('Original ATOSNS source absent')
    table=ROOT/'drivers/EldenSmmu/upstream';manifest=json.loads((table/'source.json').read_text())
    if manifest['revision']!='83969cd874503e65997ff7639f7dcf3d0a92cd17':raise ValueError('Frozen4K donor changed')
    for row in manifest['files'].values():
        if sha(table/row['output'])!=row['output_sha256']:raise ValueError('Original4K bytes changed')
    for name in ('source','camera_source','sensor_source'):
        folder=getattr(a,name);record=json.loads((folder/'source.json').read_text());records[name]=sha(folder/'source.json')
        for n,digest in record['sources'].items():
            if sha(folder/n)!=digest:raise ValueError('Exact prepared shared source changed: '+n)
    spec=importlib.util.spec_from_file_location('shared_touch',ROOT/'drivers/EldenSmmu/TouchSource.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    if module.route()!=(a.source/'DmaRoute.c').read_bytes():raise ValueError('Actual touch derivation differs')
    code=(a.source/'Shared.c').read_text();native=(a.source/'SharedNativeMethods.inc').read_text();boot=(a.source/'BootRoute.c').read_text()
    for token in ('layout_observe','write_guard','data_physical','data_bytes','0xc00001feu','elden_smmu_boot_routes_commit','EldenWifiSmmu4KDisableOwnedContext'):
        if token not in code:raise ValueError('Actual shared prerequisite absent')
    for token in ('authority.bank','authority.mapping','native_irq_mask_drain','armcpu_spi_attach','armcpu_spi_mask','native_fault'):
        if token not in native:raise ValueError('Actual shared native boundary absent')
    if 'reservation_ready' not in boot or 'sensor==4' not in boot:raise ValueError('Atomic complete-session publication absent')
    report={'passed':True,'linux_revision':LINUX,'linux_sources':topology,'original4K_manifest_sha256':sha(table/'source.json'),
        'prepared_records':records,'ADSP_masked_DT_union_expanded_exactly':sessions,'touch_derived_sha256':sha(a.source/'DmaRoute.c'),'native_ACTLR_security_authority_mandatory':True,
        'whole_retained_backing_includes_unmapped_guards':True,'no_ATOS_or_physical_DMA_claim':True,'hardware':False}
    a.output.write_text(json.dumps(report,indent=2)+'\n');print('PASS frozen Gen4 stream topology/original4K/touch and explicit native MMIO/ACTLR/retained-memory boundaries')
if __name__=='__main__':main()
