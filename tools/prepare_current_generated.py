"""Regenerate current payload sources from owned BlueTool and original firmware.

No v99 build, old kernel, captured directory, or tablet is required.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def sha(data):return hashlib.sha256(data).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--library',type=Path);p.add_argument('--agent',type=Path)
    a=p.parse_args();out=a.output.resolve()
    if out.exists():raise ValueError('Fresh output required')
    out.mkdir(parents=True)
    subprocess.run([sys.executable,str(ROOT/'tools/build_elden_bluetool.py'),'--output',str(out/'helper')],check=True)
    helper=(out/'helper/BlueTool').read_bytes()
    if sha(helper)!='464432d5308fa0998f9b25ed77ff0c2b9a7751377dc0cc992679a32dc7cc571e':raise ValueError('Rebuilt helper differs from frozen runtime')
    providers={}
    def save(data,name):
        digest=sha(data);target=out/digest/name;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data);providers[digest]=target.relative_to(out).as_posix()
    for profile in ('v99-bt-runtime','v118-native17'):
        record=ROOT/'profiles'/profile/('runtime-report.json' if profile=='v118-native17' else 'runtime-input.json')
        if profile=='v99-bt-runtime':
            # Original table is reconstructed from the immutable base ADT.
            tree=json.loads((ROOT/'profiles/v91/boot-build/adt.json').read_text())['tree'];pending=[tree];tables=[]
            while pending:
                n=pending.pop();pending+=n['children'];tables += [bytes.fromhex(x['hex']) for x in n['properties'] if x['name']=='text-fixups']
            if len(tables)!=1:raise ValueError('Ambiguous base table')
            table=tables[0]+b'bluetoothd,ddd20,a0050034,1f2003d5\0'
        else:table=bytes.fromhex(json.loads(record.read_text())['text_fixups_after_hex'])
        header='#include <stdint.h>\nstatic const uint8_t elden_bt_runtime_payload[]={\n'+','.join(str(v) for v in helper)+'\n};\n'
        header+='static const uint8_t elden_bt_runtime_fixups[]={'+','.join(str(v) for v in table)+'};\n'
        save(header.encode(),'EldenBtRuntimePayload.h')
    plan=json.loads((ROOT/'profiles/v110-bt-fifo/plan.json').read_text())
    names={'brhbtfw20.tlv':'elden_wcn_bt_patch','brhbtnv20.bin':'elden_wcn_bt_nvm','brhperifw20.tlv':'elden_wcn_patch','brhperinv20.bin':'elden_wcn_nvm','tmel_peach_20.elf':'elden_wcn_tmel'}
    header='#include <stdint.h>\n#ifdef __cplusplus\nextern "C" {\n#endif\n';definitions='#include <stdint.h>\n'
    for row in plan['firmware']:
        file=a.firmware/row['file'];data=file.read_bytes()
        if len(data)!=row['bytes'] or sha(data)!=row['sha256']:raise ValueError('Original firmware differs: '+row['file'])
        name=names[file.name];header+=f'extern const uint8_t {name}[{len(data)}];\n'
        definitions+='const uint8_t '+name+'[]={\n'+','.join(str(v) for v in data)+'\n};\n'
    header+='#ifdef __cplusplus\n}\n#endif\n';save(header.encode(),'EldenWcnFirmware.h');save(definitions.encode(),'EldenWcnFirmware.c')
    expected={h for h,v in json.loads((ROOT/'profiles/current-build/plan.json').read_text())['providers'].items() if v['kind']=='generated'}
    if set(providers)!=expected:raise ValueError('Generated input set or fingerprints differ')
    (out/'providers.json').write_text(json.dumps(providers,indent=2)+'\n')
    # Build the existing runtime trust container directly from repo metadata and
    # freshly rebuilt owned files; no previous runtime directory is an input.
    runtime=out/'runtime';runtime.mkdir();(runtime/'BlueTool').write_bytes(helper)
    import struct
    sys.path.insert(0,str(ROOT/'tools/legacy-workbench'))
    from icon_cpu_trust import append
    metadata=json.loads((ROOT/'profiles/v91/boot-build/trust-metadata.json').read_text());parts=[]
    for row in metadata['parts']:parts.append(struct.pack('<I16sI',row['version'],bytes.fromhex(row['uuid_hex']),len(row['entries_hex']))+b''.join(bytes.fromhex(v) for v in row['entries_hex']))
    position=4*(len(parts)+1);offsets=[]
    for part in parts:offsets.append(position);position+=len(part)
    trust=struct.pack('<I',len(parts))+struct.pack('<'+'I'*len(parts),*offsets)+b''.join(parts)
    if sha(trust)!=metadata['base_sha256']:raise ValueError('Trust metadata differs')
    if bool(a.library)!=bool(a.agent):raise ValueError('Supply both source-built IconServices files')
    if a.library:
        trust,_=append(trust,a.library.read_bytes());trust,_=append(trust,a.agent.read_bytes())
        if sha(trust)!=metadata['expected_v91_sha256']:raise ValueError('Owned icon trust differs')
        trust,_=append(trust,helper)
        if sha(trust)!='a6758681cd8ce0f364411bf65fc1207514b3470b0c16699e2c1a0d12f22a2c65':raise ValueError('Runtime trust differs')
        (runtime/'runtime.trustcache').write_bytes(trust)
    print(json.dumps({'regenerated':len(providers),'historical_build_inputs':0,'device_operation':False}))
if __name__=='__main__':main()
