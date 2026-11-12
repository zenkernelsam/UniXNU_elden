"""Read the exact TB322FC Linux Peach profile; do not configure hardware."""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
REVISION='6f97250ead155fc3b980856323e99a4623dae73d'
PREFIX='drivers/net/wireless/ath/ath12k/wifi7/'

def audit(repository,output):
    if output.exists():raise ValueError('New evidence directory required')
    inputs={name:subprocess.check_output(['git','-C',str(repository),'show',REVISION+':'+PREFIX+name]) for name in ('hw.c','pci.c','hal.c')}
    text=inputs['hw.c'].decode();starts=list(re.finditer(r'\{\s*\.name\s*=\s*"([^"]+)"',text));entries={}
    for i,match in enumerate(starts):
        if match[1] in ('peach hw2.0','qcc2072 hw1.0'):
            if match[1] in entries:raise ValueError('Duplicate hardware profile')
            entries[match[1]]=text[match.start():starts[i+1].start() if i+1<len(starts) else len(text)]
    if set(entries)!= {'peach hw2.0','qcc2072 hw1.0'}:raise ValueError('Missing chip profile')
    def field(entry,name):
        values=re.findall(r'\.'+re.escape(name)+r'\s*=\s*([^,]+),',entry)
        if len(values)>1:raise ValueError('Ambiguous hardware field: '+name)
        return ' '.join(values[0].split()) if values else None
    keys=('hw_rev','dir','board_size','cal_offset','m3_loader','download_aux_ucode','download_tme_lite','board_api1_only',
          'internal_sleep_clock','hw_ops','host_ce_config','target_ce_config','mhi_config','qmi_cnss_feature_bitmap')
    profiles={name:{key:field(value,key) for key in keys} for name,value in entries.items()}
    peach=profiles['peach hw2.0'];other=profiles['qcc2072 hw1.0']
    required={'hw_rev':'ATH12K_HW_PEACH_HW20','dir':'"PEACH/hw2.0"','internal_sleep_clock':'false',
              'download_tme_lite':'true','board_api1_only':'true','hw_ops':'&qcc2072_ops',
              'mhi_config':'&ath12k_wifi7_mhi_config_wcn7850',
              'qmi_cnss_feature_bitmap':'BIT(CNSS_WLAN_EN_SUPPORT_V01) | BIT(CNSS_AUX_UC_SUPPORT_V01)'}
    if any(peach[k]!=v for k,v in required.items()) or other['internal_sleep_clock']!='true':raise ValueError('Reviewed chip contract changed')
    pci=inputs['pci.c'].decode();start=pci.index('\tcase PEACH_DEVICE_ID:');end=pci.index('\tcase QCC2072_DEVICE_ID:',start);selection=pci[start:end]
    for token in ('ATH12K_PCI_W7_SOC_HW_VERSION_2','ATH12K_HW_PEACH_HW20','return -EOPNOTSUPP'):
        if token not in selection:raise ValueError('Version-qualified Peach selection missing')
    if not re.search(r'#define PEACH_DEVICE_ID\s+0x110e',pci):raise ValueError('Unexpected PCI ID')
    hal=inputs['hal.c'].decode();start=hal.index('[ATH12K_HW_PEACH_HW20]');end=hal.index('\n\t},',start)+5;hal_entry=hal[start:end]
    for token in ('hal_qcc2072_ops','hal_rx_desc_qcc2072','ath12k_hw_hal_params_wcn7850','qcc2072_regs'):
        if token not in hal_entry:raise ValueError('Reviewed Peach datapath mapping changed')
    output.mkdir(parents=True)
    (output/'peach-profile.txt').write_text(entries['peach hw2.0'])
    (output/'pci-selection.txt').write_text(selection)
    (output/'hal-selection.txt').write_text(hal_entry)
    report={'repository':'h0cheung/tb322fc-linux-kernel','revision':REVISION,
            'sources':{PREFIX+k:hashlib.sha256(v).hexdigest() for k,v in inputs.items()},
            'profiles':profiles,'different_fields':[k for k in keys if peach[k]!=other[k]],
            'pci_device':'0x110e','required_soc_major':2,'unknown_version_rejected':True,
            'decision':'Port the dedicated Peach profile and its feature dependencies. QCC2072 datapath sharing does not make the whole QCC2072 hardware profile interchangeable.',
            'runtime_changed':False,'hardware_tested':False,'device_operations':False}
    (output/'result.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({'different_fields':report['different_fields'],'device_operations':False}))
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repository',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();audit(a.repository,a.output)
if __name__=='__main__':main()
