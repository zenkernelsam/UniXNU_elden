"""Qualify exact native allocator helper provenance and Gen4 DMA contracts."""
from pathlib import Path
import argparse,hashlib,json,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();sys.path.insert(0,str(DRIVER));from DmaSource import derive
    files,helpers=derive();spec=json.loads((DRIVER/'dma-source.json').read_text());checks=[]
    def check(v,name):
        if not v:raise ValueError('Actual camera DMA source mismatch: '+name)
        checks.append(name)
    table=ROOT/spec['table_source_manifest'];check(sha(table)==spec['table_manifest_sha256'],'original4K table manifest SHA')
    record=json.loads(table.read_text());check(record['revision']==spec['ArmSmmu4K_revision'],'original4K pinned revision')
    for row in record['files'].values():check(sha(table.parent/row['output'])==row['output_sha256'],'unchanged original4K source '+row['output'])
    pipeline=json.loads((DRIVER/'pipeline-source.json').read_text())
    for name in ('sm8750.dtsi','camss.c'):
        row=next(x for x in pipeline['files'] if x['file']==name);p=DRIVER/'upstream'/name;check(sha(p)==row['sha256'],'exact Gen4 '+name)
    soc=(DRIVER/'upstream/sm8750.dtsi').read_text();camss=(DRIVER/'upstream/camss.c').read_text()
    check('iommus = <&apps_smmu 0x1c00 0x00>;' in soc,'exact shared CAMSS SID1c00')
    check('dma_set_mask_and_coherent(dev, 0xffffffff)' in camss,'exact original32bit streaming/coherent DMA')
    check(len(helpers)==6,'six exact original native allocator/PAR/cache helper bodies')
    source=(DRIVER/'NativeDmaMethods.inc').read_text();binding=(DRIVER/'DmaBinding.c').read_text()
    check('lease.slot<8||lease.slot>=132' in source and 'lease.bank<7' in source,'original routes0..7/banks0..6 cannot be repurposed')
    check('lease.id0!=0x4c017e84||lease.id1!=0x60000053||lease.id2!=0x5111' in source,'exact observed Gen4 IDs; original provider still mandatory')
    check(source.count('owner.early(')>=2 and 'owner->early(' in source and 'install_attempted=1' in source,'early route preflight before actual install')
    check('cursor+=rounded+4096' in source and 'cursor=4096' in source,'guardpages remain outside each frame mapping')
    check('kIOMapUnique|kIOMapPrefault|kIOMapWriteCombineCache' in source,'actual fresh prefaultNC alias')
    check('camera_dma_verify(c,c->data_descriptor' in source and '0xff' in source and '0x44' in source,'actual all4K primaryWB/NC PAR validation')
    check('EldenWifiSmmu4KBuild(' in source and 'EldenWifiSmmu4KAuditTables(' in source,'actual original4K physical graph build/audit')
    check('camera_dma_slot' in source and 'c->slot_state[index]=1' in source and 'c->slot_state[index]=3' in source,'actual queue/completion/borrow ownership states')
    check('EldenCameraFrame' in source and 'saved.timestamp_ns!=frame->timestamp_ns' in source,'retained exact frame-return identity')
    check('elden_camera_board_shutdown(&c->board)' in binding and 'elden_camera_dma_stop(c->dma' in binding,'actual boardstop before DMA retirement')
    check('delete ' not in source and 'release()' not in source and 'complete(' not in source,'no descriptors/aliases/table release before full reset')
    output={'passed':True,'checks':checks,'check_count':len(checks),'helpers':helpers,'generated_sha256':{n:hashlib.sha256(b).hexdigest() for n,b in files.items()},
        'table_revision':record['revision'],'Linux_commit':spec['linux_commit'],'stream_id':spec['CAMSS_stream_id'],'hardware':False}
    a.output.write_text(json.dumps(output,indent=2)+'\n');print('PASS exact Gen4 camera DMA/allocator source provenance: '+str(len(checks))+' checks')
if __name__=='__main__':main()
