"""Verify three real gate consumers, single provider and frozen recovery inputs."""
from pathlib import Path
import argparse,hashlib,json,re,struct,subprocess,sys
from audit_entry_relocation import symbol,entry
from audit_hid_kernel import at_va
from link_v91_kernel import verify_original_executables
from replay_xnu_objects import sections
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    if sys.flags.optimize:raise SystemExit('Assertions required')
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--profile',choices=('v111-spmi-gate','v112-uart-route','v113-uart-gic','v114-hci-length'),default='v111-spmi-gate')
    for n in ('build','baseline','components','core','llvm','output'):ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();b=a.build;c=a.components;r=json.loads((b/'result.json').read_text());pp=ROOT/'profiles'/a.profile/'plan.json';p=json.loads(pp.read_text())
    assert r['profile_sha256']==sha(pp)
    if a.profile=='v111-spmi-gate':assert r['components_sha256']==sha(c/'result.json')
    for n in ('native','kernel'):assert sha(b/(n+'.macho'))==r[n+'_sha256'] and sha(a.baseline/(n+'.macho'))==p['baseline_'+n+'_sha256']
    assert (b/'link.filelist').read_bytes()==(a.baseline/'link.filelist').read_bytes()
    for path,h in r['inherited_inputs'].items():assert sha(Path(path))==h
    for name,ar in r.get('archives',{}).items():
        expected={'Spmi.o'} if name=='libEldenPmicClient.a' else {'BtPmic.o','WcnPowerNative.o'}
        assert {n for n,v in ar['members'].items() if v['changed']}==expected
        for n,v in ar['members'].items():
            data=subprocess.check_output(['/usr/bin/ar','p',ar['after'],n]);assert hashlib.sha256(data).hexdigest()==v['after']
            if n not in expected:assert v['before']==v['after']
    if a.profile in ('v112-uart-route','v113-uart-gic','v114-hci-length'):
        assert {n for n,v in r['members'].items() if v['before']!=v['after']}==set(n+'.o' for n in p.get('replacement_units',['UartPlatform','UartBackend']))
        for n,v in r['members'].items():
            data=subprocess.check_output(['/usr/bin/ar','p',str(b/'bluetooth/libEldenBluetooth.a'),n]);assert hashlib.sha256(data).hexdigest()==v['after']
        for n,h in p['sources'].items():assert sha(b/'source'/n)==sha(pp.parent/'sources'/n)==h
        if a.profile=='v113-uart-gic':
            assert '#define ELDEN_BT_GICD_BASE 0x16000000ULL' in (b/'source/EldenBluetoothGic.h').read_text()
            assert '0x01600000' not in (b/'source/UartBackend.cpp').read_text()
    native=(b/'native.macho').read_bytes();kernel=(b/'kernel.macho').read_bytes()
    nm=subprocess.check_output([str(a.llvm/'llvm-nm'),'-n',str(b/'native.macho')],text=True)
    syms=[(int(m[1],16),m[2]) for m in re.finditer(r'^([0-9a-f]+) \S (.+)$',nm,re.M)]
    gate_names=['_elden_spmi_transaction_acquire','_elden_spmi_transaction_release','_elden_spmi_transaction_state']
    addresses={n:symbol(native,n) for n in gate_names}
    assert len([n for _,n in syms if n=='_gate'])==1
    calls={}
    consumers=['_elden_adc_spmi_read','_elden_adc_spmi_write','_pb_spmi_read8']
    bt_helpers=[n for _,n in syms if 'bt_write8' in n]
    if not bt_helpers:bt_helpers=[n for _,n in syms if '__ZN12_GLOBAL__N_16bt_setEPvb'==n]
    assert len(bt_helpers)==1;consumers+=bt_helpers
    for n in consumers:
        va=symbol(native,n);end=min(v for v,_ in syms if v>va);code=at_va(native,va,end-va);assert code==at_va(kernel,va,end-va);targets=[]
        for off in range(0,len(code)-3,4):
            word=struct.unpack_from('<I',code,off)[0]
            if word&0x7c000000!=0x14000000:continue
            imm=word&0x3ffffff
            if imm&(1<<25):imm-=1<<26
            target=va+off+imm*4
            targets += [key for key,address in addresses.items() if address==target]
        assert '_elden_spmi_transaction_acquire' in targets and '_elden_spmi_transaction_release' in targets
        calls[n]=targets
    pre=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink'];assert verify_original_executables(b/'kernel.macho',json.loads((b/'kernel.json').read_text()),pre['original_executable_sha256'])==30
    old=entry((a.baseline/'kernel.macho').read_bytes());now=entry(kernel);assert old[2]==now[2] and old[1]-old[0]==now[1]-now[0]==7124
    contract=json.loads((ROOT/'profiles/v110-bt-fifo/entry-assembly.json').read_text());rec=json.loads((a.core/'build/results.json').read_text())['304'];obj=Path(rec['output'])
    assert rec['accepted'] and sha(obj)==rec['sha256'] and sections(obj)==contract['expected_sections']
    for folder in (b,a.baseline):assert (folder/'link.filelist').read_text().splitlines().count(str(obj))==1
    layout={'source_kernel_sha256':r['kernel_sha256'],'baseline_kernel_sha256':p['baseline_kernel_sha256'],'assembly_sha256':sha(obj),'assembly_sections_equal':True,'same_object_in_both_filelists':True,'entry_word':now[2],'return_target':hex(now[1]),'entry_address':hex(now[0]),'target_delta':7124,'hardware_tested':False}
    with (b/'entry-layout.json').open('x') as f:json.dump(layout,f,indent=2);f.write('\n')
    result={'accepted_static':True,'profile_sha256':sha(pp),'kernel_sha256':r['kernel_sha256'],'native_sha256':r['native_sha256'],'one_gate_state':True,'gate_consumer_calls':calls,'core_filelist_equal':True,'original_apple_executables_hashed':30,'entry_return_target':hex(now[1]),'hardware_tested':False}
    with a.output.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
