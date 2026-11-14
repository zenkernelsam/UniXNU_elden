"""Prepare exact-original-signature page rules for the v118 routing trial."""
from pathlib import Path
import argparse, hashlib, json, sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'drivers/EldenBluetooth/userland'))
from Native17 import Image

def sha(data): return hashlib.sha256(data).hexdigest()
def array(data): return ','.join(str(b) for b in data)

def main():
    if sys.flags.optimize: raise ValueError('Assertions required')
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--trace-xpc',action='store_true',help='v120 only: preserve calls and record XPC results/errx')
    for n in ('original','runtime','output'): p.add_argument('--' + n, type=Path, required=True)
    a = p.parse_args()
    if a.output.exists(): raise ValueError('Fresh output required')
    image = Image(a.original).routing()
    if a.trace_xpc:
        from Native17Trace import apply
        apply(image)
    report = image.report()
    previous = json.loads((a.runtime / 'report.json').read_text())
    helper = (a.runtime / 'BlueTool').read_bytes(); trust = (a.runtime / 'runtime.trustcache').read_bytes()
    assert sha(helper) == previous['helper_sha256'] == '464432d5308fa0998f9b25ed77ff0c2b9a7751377dc0cc992679a32dc7cc571e'
    assert sha(trust) == previous['trust_sha256'] == 'a6758681cd8ce0f364411bf65fc1207514b3470b0c16699e2c1a0d12f22a2c65'
    baseline = bytes.fromhex(previous['text_fixups_before_hex'])
    rules = [{'offset': 0xddd20, 'before': 'a0050034', 'after': '1f2003d5'}]
    edits=report['edits']
    if a.trace_xpc:
        edits=[]
        for edit in sorted(report['edits'],key=lambda e:e['offset']):
            if edits and edits[-1]['offset']+len(bytes.fromhex(edits[-1]['before']))==edit['offset']:
                edits[-1]['before']+=edit['before'];edits[-1]['after']+=edit['after']
            else:edits.append(dict(edit))
    for edit in edits:
        before = bytes.fromhex(edit['before']); after = bytes.fromhex(edit['after'])
        for at in range(0, len(before), 16):
            offset = edit['offset'] + at; old = before[at:at+16]; new = after[at:at+16]
            assert old and len(old) == len(new) and offset // 4096 == (offset + len(old) - 1) // 4096
            rules.append({'offset': offset, 'before': old.hex(), 'after': new.hex()})
    table = baseline + b''.join(('bluetoothd,%x,%s,%s\0' % (r['offset'], r['before'], r['after'])).encode() for r in rules)
    assert len([r for r in table.split(b'\0') if r]) <= 32
    assert all(len(r) < 160 for r in table.split(b'\0'))
    guard = (ROOT / 'drivers/EldenBluetooth/runtime/TextGuard.h').read_text()
    first = guard.index('static inline bool elden_bt_stream_rule(')
    last = guard.index('static inline bool elden_bt_stream_identity(')
    replacement = 'struct elden_bt_native_rule { uint64_t offset; uint32_t bytes; uint8_t before[16],after[16]; };\n'
    replacement += 'static const struct elden_bt_native_rule elden_bt_native_rules[]={\n'
    for r in rules:
        old = bytes.fromhex(r['before']); new = bytes.fromhex(r['after'])
        replacement += '{UINT64_C(%d),%d,{%s},{%s}},\n' % (r['offset'], len(old), array(old), array(new))
    replacement += '''};
static inline bool elden_bt_stream_rule(uint64_t offset,uint32_t bytes,const uint8_t *before,const uint8_t *after){
    if(!before||!after||!bytes||bytes>16)return false;
    for(unsigned i=0;i<sizeof(elden_bt_native_rules)/sizeof(elden_bt_native_rules[0]);i++){
        const struct elden_bt_native_rule *r=&elden_bt_native_rules[i];
        if(offset==r->offset&&bytes==r->bytes&&!memcmp(before,r->before,bytes)&&!memcmp(after,r->after,bytes))return true;
    }
    return false;
}
'''
    guard = guard[:first] + replacement + guard[last:]
    header = '#include <stdint.h>\nstatic const uint8_t elden_bt_runtime_payload[]={\n' + array(helper) + '\n};\n'
    header += 'static const uint8_t elden_bt_runtime_fixups[]={' + array(table) + '};\n'
    a.output.mkdir(parents=True)
    (a.output/'BlueTool').write_bytes(helper); (a.output/'runtime.trustcache').write_bytes(trust)
    (a.output/'EldenBtRuntimePayload.h').write_text(header, newline='\n')
    (a.output/'EldenBtTextGuard.h').write_text(guard, newline='\n')
    result = {'profile':'v120-bt-xpc' if a.trace_xpc else 'v118-native17','helper_sha256':sha(helper),'trust_sha256':sha(trust),
              'payload_header_sha256':sha(header.encode()),'text_guard_sha256':sha(guard.encode()),
              'text_fixups_before_hex':baseline.hex(),'text_fixups_after_hex':table.hex(),
              'rules':rules,'native_patch':report,'table_count':len([r for r in table.split(b'\0') if r]),
              'original_bluetoothd_cdhash':previous['original_bluetoothd_cdhash'],
              'original_job_unchanged':True,'original_cache_unchanged':True,'original_jobs':561,
              'new_daemon_file':False,'new_trust_entry':False,'hardware_tested':False}
    (a.output/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('table_count','payload_header_sha256','text_guard_sha256')},indent=2))

if __name__=='__main__':main()
