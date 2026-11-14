"""Run the pinned upstream oracle plus Elden failure-boundary cases.

The original test and its hashlib blob/UUID oracle are retained verbatim on disk.
Generated sources, harness, executable and logs stay in the requested new output.
"""
from pathlib import Path
import argparse, hashlib, json, subprocess, sys
from SourcePatch import service, glue

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    original=Path(__file__).with_name('TestUpstream.py').read_bytes()
    assert hashlib.sha256(original).hexdigest()=='e49e8ef2ec7b14f96aa5f700b96a8f4152a4f72b0a1a8dc5202b9f449472e640'
    text=original.decode()
    def change(old,new):
        nonlocal text
        assert text.count(old)==1,old
        text=text.replace(old,new)
    change('with tempfile.TemporaryDirectory() as directory:', 'from contextlib import nullcontext\nwith nullcontext(str(root / "harness")) as directory:')
    change('    work = Path(directory)', '    work = Path(directory)\n    work.mkdir()')
    change('size_t length = 512;', 'size_t length = 512; bool shortWrite = false;')
    change('return n; }\n};', 'return shortWrite && n ? n-1 : n; }\n};')
    change('static bool enabled;', 'static bool enabled;\nstatic bool keys = true;\nextern "C" bool native_aks_key_operations_enabled() { return enabled && keys; }')
    change('static bool haveIdentity = true;', 'static bool haveIdentity = true;\nstatic bool oversizedIdentity = false, otherIdentity = false;')
    change('    return 8;\n}', '    if (otherIdentity) out[0] ^= 0x80;\n    return oversizedIdentity ? 33 : 8;\n}')
    change('if (b != 0x81 || i >= n) return false; l = p[i++]; return true; };', 'if (b != 0x81 || i >= n) { return false; } l = p[i++]; return true; };')
    marker='    assert(client->clientClose() == kIOReturnSuccess'
    extra=r'''
    // Elden mode1 must not advertise/perform key operations. Existing state queries remain.
    keys = false;
    for (uint32_t sel : {10u,11u,23u,40u}) {
        IOExternalMethodArguments a{};
        assert(client->externalMethod(sel,&a)==(IOReturn)0xe00002c7);
    }
    IOExternalMethodArguments state{in,1,outScalar,1,nullptr,0};
    assert(client->externalMethod(7,&state)==0);
    keys = true;
    uint64_t sin[2]={0,7}, sout[1]={123}; uint8_t plain[32]={}, blob[40]={}, recovered[32]={};
    IOExternalMethodArguments w{sin,2,sout,1,blob,sizeof blob,nullptr,0,plain,sizeof plain};
    w.scalarInput=nullptr;
    assert(client->externalMethod(10,&w)==kIOReturnBadArgument && sout[0]==123);
    w.scalarInput=sin;
    oversizedIdentity=true;
    assert(client->externalMethod(10,&w)==(IOReturn)0xe00002bc);
    oversizedIdentity=false;
    assert(client->externalMethod(10,&w)==0);
    IOExternalMethodArguments u{sin,2,nullptr,0,recovered,sizeof recovered,nullptr,0,blob,sizeof blob};
    otherIdentity=true;
    assert(client->externalMethod(11,&u)==(IOReturn)0xe00002bc);
    otherIdentity=false;
    assert(client->externalMethod(11,&u)==0);
    IOMemoryDescriptor desc; desc.shortWrite=true;
    w.structureOutputDescriptor=&desc; w.structureOutputDescriptorSize=77; sout[0]=123;
    assert(client->externalMethod(10,&w)==(IOReturn)0xe00002bc);
    assert(w.structureOutputDescriptorSize==77 && sout[0]==123);
    u.structureOutputDescriptor=&desc; u.structureOutputDescriptorSize=77;
    assert(client->externalMethod(11,&u)==(IOReturn)0xe00002bc && u.structureOutputDescriptorSize==77);
    enabled=false;
    w.structureOutputDescriptor=nullptr;
    assert(client->externalMethod(10,&w)==(IOReturn)0xe00002c7);
    enabled=true;
'''
    change(marker,extra+marker)
    change('["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror",','["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",')
    (out/'NativeKeyStoreEvents.cpp').write_bytes(service())
    (out/'EldenKeyEventsGlue.cpp').write_bytes(glue())
    (out/'test.py').write_text(text)
    r=subprocess.run([sys.executable,'-B',str(out/'test.py')],capture_output=True)
    (out/'test.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    report={'passed':True,'actual_service':True,'sanitizers':['ASan','UBSan'],
      'upstream_oracle_preserved':True,'additional_cases':['mode1 refusal','null scalar','oversized identity','identity mismatch','short descriptor write','disabled state'],
      'service_sha256':hashlib.sha256(service()).hexdigest(),'glue_sha256':hashlib.sha256(glue()).hexdigest(),
      'scope':'host service lifecycle and selectors; not IOKit/kernel registration or persistent keychain'}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
