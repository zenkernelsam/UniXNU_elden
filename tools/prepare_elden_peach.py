"""Backport the pinned TB322FC Peach delta onto the official ath12k Linux base.

Writes a new source snapshot only. No reference checkout edits, compilation,
hardware writes or automatic selection in existing kernel/boot profiles.
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
PROFILE=ROOT/'drivers/EldenWifi/peach'

def digest(data):return hashlib.sha256(data).hexdigest()
def sections(text):
    out={};current=None
    for line in text.splitlines(keepends=True):
        if line.startswith('diff --git '):
            m=re.fullmatch(r'diff --git a/(\S+) b/\1\n',line)
            if not m or m[1] in out:raise ValueError('Invalid/duplicate patch path')
            current=[];out[m[1]]=current
        elif line.startswith('@@'):
            m=re.match(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@',line)
            if current is None or not m:raise ValueError('Invalid hunk')
            current.append({'old':[],'new':[],'added':[],'old_count':int(m[2] or 1),'new_count':int(m[4] or 1)})
        elif current and line[:1] in (' ','+','-') and not line.startswith(('---','+++')):
            if line[0]!='+':current[-1]['old'].append(line[1:])
            if line[0]!='-':current[-1]['new'].append(line[1:])
            if line[0]=='+':current[-1]['added'].append(line[1:])
        elif line.startswith(('---','+++')):pass
        else:raise ValueError('Unrecognized patch content')
    for hunks in out.values():
        for h in hunks:
            if len(h['old'])!=h['old_count'] or len(h['new'])!=h['new_count']:raise ValueError('Truncated hunk')
            h['old']=''.join(h['old']);h['new']=''.join(h['new'])
    return out

def replace(text,old,new):
    if text.count(old)!=1:raise ValueError('Missing/ambiguous source anchor: '+old[:100])
    return text.replace(old,new,1)

def array_end(text,anchor):
    if text.count(anchor)!=1:raise ValueError('Ambiguous array')
    masked=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',lambda m:' '*len(m[0]),text,flags=re.S)
    at=text.index(anchor)+len(anchor);depth=1
    while depth and at<len(masked):
        depth+=(masked[at]=='{')-(masked[at]=='}');at+=1
    if depth or text[at:at+1]!=';':raise ValueError('Unclosed array')
    return at-1

def prepare(repository,output):
    if output.exists():raise ValueError('New output directory required')
    manifest=json.loads((PROFILE/'sources.json').read_text());patch=(PROFILE/'upstream-peach.patch').read_bytes()
    if digest(patch)!=manifest['patch_sha256']:raise ValueError('Original patch changed')
    parsed=sections(patch.decode())
    if set(parsed)!=set(manifest['files']):raise ValueError('Patch scope differs')
    sources={};results={};prefix='drivers/net/wireless/ath/ath12k/'
    adapted={'core.h','wifi7/hal.c','wifi7/hw.c'}
    for path,info in manifest['files'].items():
        data=b'' if info['base_sha256'] is None else subprocess.check_output(['git','-C',str(repository),'show',manifest['base_commit']+':'+path])
        if info['base_sha256'] and digest(data)!=info['base_sha256']:raise ValueError('Base source changed: '+path)
        sources[path]=data;text=data.decode();relative=path.removeprefix(prefix)
        if relative not in adapted:
            for h in parsed[path]:
                if not h['old']:
                    if text:raise ValueError('New header would overwrite source')
                    text=h['new']
                else:text=replace(text,h['old'],h['new'])
        elif relative=='core.h':
            text=replace(text,'\tATH12K_HW_QCC2072_HW10,\n};','\tATH12K_HW_QCC2072_HW10,\n\tATH12K_HW_PEACH_HW20,\n};')
        else:
            hunks=parsed[path]
            if len(hunks)!=1:raise ValueError('Unexpected hardware-table patch')
            # This hunk only adds an entry. Preserve the original added lines,
            # placing them before the old version's closing array instead.
            h=hunks[0]
            if h['new_count']-h['old_count']!=len(h['added']):raise ValueError('Hardware entry unexpectedly removes source')
            block=''.join(h['added'])
            if relative=='wifi7/hw.c':
                client='\t\t.client = {\n\t\t\t.max_client_single = 512,\n\t\t\t.max_client_dbs = 128,\n\t\t\t.max_client_dbs_sbs = 128,\n\t\t},\n'
                block=replace(block,client,'');block=replace(block,'\t\t.host_alloc_ml_id = false,\n','')
                if 'host_alloc_ml_id' in text:raise ValueError('Base gained host ML-ID ownership')
                anchor='ath12k_wifi7_hw_params[] = {'
            else:anchor='ath12k_wifi7_hw_ver_map[] = {'
            end=array_end(text,anchor);text=text[:end]+block+text[end:]
        results[path]=text
    # The copied7.2 per-device client limits already live in7.0's selected
    # DEFAULT memory profile; do not add a second, incompatible struct layout.
    core=results[prefix+'core.c'];default=core.split('[ATH12K_QMI_MEMORY_MODE_DEFAULT] = {',1)[1].split('[ATH12K_QMI_MEMORY_MODE_LOW_512_M]',1)[0]
    for name,value in [('max_client_single',512),('max_client_dbs',128),('max_client_dbs_sbs',128)]:
        if not re.search(r'\.'+name+r'\s*=\s*'+str(value)+r'\s*,',default):raise ValueError('Client-limit equivalence missing')
    pci7=results[prefix+'wifi7/pci.c'];peach=pci7.split('\tcase PEACH_DEVICE_ID:',1)[1].split('\tcase QCC2072_DEVICE_ID:',1)[0]
    if 'ab->target_mem_mode = ATH12K_QMI_MEMORY_MODE_DEFAULT;' not in peach:raise ValueError('Peach memory profile differs')
    # Keep the existing non-Peach DMA behavior; the new coherent mask and its
    # failure handling apply only to the matched Qualcomm Peach endpoint.
    dma='\tret = dma_set_mask_and_coherent(&pdev->dev, ab_pci->dma_mask);\n\tif (ret) {\n\t\tath12k_err(ab, "failed to set DMA mask: %d\\n", ret);\n\t\tgoto release_region;\n\t}\n'
    scoped='\tif (pdev->vendor == PCI_VENDOR_ID_QCOM && pdev->device == 0x110e) {\n'+''.join('\t'+line for line in dma.splitlines(keepends=True))+'\t} else {\n\t\tdma_set_mask(&pdev->dev, ab_pci->dma_mask);\n\t\tdma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));\n\t}\n'
    results[prefix+'pci.c']=replace(results[prefix+'pci.c'],dma,scoped)
    qmi=results[prefix+'qmi.c']
    qmi=replace(qmi,'\t\tif (!ab->hw_params->fw.download_aux_ucode)\n','\t\tif (ab->hw_params->fw.download_tme_lite &&\n\t\t    !ab->hw_params->fw.download_aux_ucode)\n')
    qmi=replace(qmi,'\tif (hw_params->fw.download_aux_ucode &&\n\t    (ab->qmi.target.fw_caps & QMI_WLANFW_AUX_UC_SUPPORT_V01)) {',
        '\tif (hw_params->fw.download_aux_ucode &&\n\t    (!hw_params->fw.download_tme_lite ||\n\t     (ab->qmi.target.fw_caps & QMI_WLANFW_AUX_UC_SUPPORT_V01))) {')
    results[prefix+'qmi.c']=qmi
    # No stub is selected in a production compile: the native power provider
    # must resolve this real dependency before a kernel can be linked.
    output.mkdir(parents=True)
    records={}
    for path,text in results.items():
        target=output/'tree'/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_text(text,newline='\n')
        records[path]={'base_sha256':manifest['files'][path]['base_sha256'],'output_sha256':digest(text.encode())}
    report={'base_commit':manifest['base_commit'],'origin_commit':manifest['origin_commit'],'patch_sha256':manifest['patch_sha256'],
        'files':records,'hardware_tables_relocated_for_base':sorted(adapted),'default_client_limits':[512,128,128],
        'unsupported_host_ml_id_ownership':False,'non_peach_dma_and_aux_behavior_preserved':True,
        'required_compile_defines':{'CONFIG_POWER_SEQUENCING_QCOM_WCN':1},'required_native_symbol':'pwrseq_qcom_wcn_set_ol_cpr',
        'compiled':False,'native_gateway_implemented':False,'kernel_linked':False,'hardware_operations':False}
    (output/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'files':len(records),'compiled':False,'hardware_operations':False}))
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--linux-repository',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();prepare(a.linux_repository,a.output)
if __name__=='__main__':main()
