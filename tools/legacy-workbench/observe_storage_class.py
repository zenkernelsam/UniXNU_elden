"""Bounded QEMU raw-entry observer of the real original storage metaclass.

Never accesses the tablet. No guest code/data replacement; breakpoints and
normal instruction execution only. Reports timeout as incomplete evidence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time
import xml.etree.ElementTree as ET


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--fixture',type=Path,required=True)
    p.add_argument('--timeout',type=int,default=120)
    p.add_argument('--diagnostic-stop',type=lambda v:int(v,0))
    p.add_argument('--lifecycle',action='store_true',help='observe the real UFS class-registration gate, then stop before MMIO')
    p.add_argument('--state-word',action='append',default=[],help='capture an actual 32-bit state word as NAME=VA')
    a=p.parse_args()
    f=a.fixture.resolve()
    sys.path.insert(0,str(f))
    from gdb_packets import Packets
    state=json.loads((f/'entry.json').read_text())
    audit=json.loads((f/'static-audit.json').read_text())
    assert state['sha256']==audit['candidate_sha256']
    ctor=int(audit['osmeta_ctor']['__ZN11OSMetaClassC2EPKcPKS_j'],16)
    meta=int(audit['original_metaclass'],16)
    service=int(audit['source_ioservice_metaclass'],16)
    report={'kernel_sha256':state['sha256'],'raw_entry_fixture':True,'UEFI_handoff_tested':False,
            'kernel_flat_sha256':hashlib.sha256((f/'kernel-flat.bin').read_bytes()).hexdigest(),
            'bounded_timeout_interrupted':True,'memory':{},'system_registers':{}}
    flat=(f/'kernel-flat.bin').read_bytes()
    virtual=int(state['virtual_base'],16)
    physical=int(state['physical_base'],16)
    source_text=next(s for s in state['segments'] if s['name']=='__TEXT_EXEC')
    begin=int(source_text['va'],16)-virtual
    end=begin+source_text['size']
    # Observe the original source kernel's actual MSR SCTLR_EL1, x0. This
    # independently checks QEMU's legacy-named SCTLR register against the
    # instruction's input, rather than assigning an assumed EL1 alias.
    writes=[i for i in range(begin,end-3,4)
            if struct.unpack_from('<I',flat,i)[0]==0xd5181000]
    assert len(writes)==1, 'source kernel SCTLR write is not unique'
    mmu_write=physical+writes[0]
    with socket.socket() as reserve:
        reserve.bind(('127.0.0.1',0));port=reserve.getsockname()[1]
    command=['qemu-system-aarch64','-M','virt,virtualization=off,secure=off,gic-version=3',
        '-accel','tcg','-cpu',state['qemu_cpu'],'-m','1024','-smp','1','-display','none','-monitor','none',
        '-nic','none','-no-reboot','-serial',f'file:{f}/observer-uart.log','-gdb',f'tcp:127.0.0.1:{port}','-S',
        '-kernel',str(f/'entry.bin'),'-device',f'loader,file={f}/kernel-flat.bin,addr={state["physical_base"]},force-raw=on',
        '-device',f'loader,file={f}/boot-args.bin,addr={state["boot_args_pa"]},force-raw=on']
    deadline=time.monotonic()+a.timeout
    with (f/'observer-stderr.log').open('w') as errors:
        child=subprocess.Popen(command,stdout=errors,stderr=errors)
        (f/'observer-job.json').write_text(json.dumps({'pid':child.pid,'port':port,'command':command})+'\n')
        connection=None
        stopped=True
        try:
            for _ in range(100):
                try:
                    connection=socket.create_connection(('127.0.0.1',port),timeout=1);break
                except ConnectionRefusedError:
                    if child.poll() is not None:raise RuntimeError('QEMU exited before GDB connection')
                    time.sleep(.02)
            assert connection
            connection.setsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY,1)
            wire=Packets(connection)
            def request(text):
                connection.settimeout(max(.01,deadline-time.monotonic()))
                return wire.request(text)
            def feature(name):
                out='';offset=0
                while True:
                    part=request(f'qXfer:features:read:{name}:{offset:x},1000')
                    out+=part[1:];offset+=len(part)-1
                    if part[0]=='l':
                        # QEMU 6.x leaves xi undeclared; the upstream observer
                        # applies the same local namespace normalization.
                        if 'xi:include' in out and 'xmlns:xi=' not in out:
                            out=out.replace('<target','<target xmlns:xi="http://www.w3.org/2001/XInclude"',1)
                        return ET.fromstring(out)
            register_names={}
            register_descriptions={}
            next_regnum=0
            target=feature('target.xml')
            for include in target.findall('{http://www.w3.org/2001/XInclude}include'):
                document=feature(include.attrib['href'])
                for r in document.findall('reg'):
                    number=int(r.attrib.get('regnum',next_regnum))
                    register_names[r.attrib['name']]=number
                    register_descriptions[r.attrib['name']]=dict(r.attrib,
                        actual_regnum=number,feature=include.attrib['href'])
                    next_regnum=number+1
            (f/'register-names.json').write_text(json.dumps(register_names,indent=2)+'\n')
            (f/'register-descriptions.json').write_text(json.dumps(register_descriptions,indent=2)+'\n')
            def register(name):
                raw=request(f'p{register_names[name]:x}')
                return int.from_bytes(bytes.fromhex(raw),'little')
            def memory(address,size):
                raw=request(f'm{address:x},{size:x}')
                if raw.startswith('E'):raise RuntimeError('unmapped observed memory '+hex(address))
                return bytes.fromhex(raw)
            sctlr_name='SCTLR_EL1' if 'SCTLR_EL1' in register_names else 'SCTLR'
            assert int(register_descriptions[sctlr_name]['bitsize'])==64
            assert request(f'Z1,{mmu_write:x},4')=='OK'
            stopped=False;request('c');stopped=True
            assert register('pc')==mmu_write
            assert memory(mmu_write,4)==struct.pack('<I',0xd5181000)
            written=register('x0')
            before=register(sctlr_name)
            stopped=False;request('s');stopped=True
            after=register(sctlr_name)
            assert after==written and after&1
            report['sctlr_instruction_observation']={
                'instruction':'MSR SCTLR_EL1, x0','encoding':'0xd5181000',
                'instruction_pa':hex(mmu_write),'instruction_va':hex(virtual+writes[0]),
                'input_x0':hex(written),'rsp_before':hex(before),'rsp_after':hex(after),
                'rsp_register':sctlr_name,'rsp_description':register_descriptions[sctlr_name],
                'guest_code_modified':False}
            assert request(f'z1,{mmu_write:x},4')=='OK'
            assert request(f'Z1,{ctor:x},4')=='OK'
            if a.diagnostic_stop:
                assert request(f'Z1,{a.diagnostic_stop:x},4')=='OK'
            seen=0
            while time.monotonic()<deadline and seen<2048:
                stopped=False;request('c');stopped=True;seen+=1
                if register('pc') != ctor:
                    raise RuntimeError('diagnostic stop before selected metaclass constructor')
                if register('x0')==meta:
                    caller=register('x30')
                    report['constructor_return_pc']=hex(caller)
                    assert request(f'z1,{ctor:x},4')=='OK'
                    assert request(f'Z1,{caller:x},4')=='OK'
                    stopped=False;request('c');stopped=True
                    assert register('pc')==caller
                    report['observed_stage']='real source OSMetaClass constructor returned to original storage code'
                    report['memory'][f'{meta:x}:50']=memory(meta,0x50).hex()
                    report['memory'][f'{service:x}:50']=memory(service,0x50).hex()
                    for name in ('TCR_EL1','TTBR0_EL1','TTBR1_EL1','SCTLR_EL1','CNTFRQ_EL0','ESR_EL1','ELR_EL1','FAR_EL1'):
                        if name in register_names:report['system_registers'][name]=hex(register(name))
                    report['system_registers']['SCTLR_EL1']=hex(register(sctlr_name))
                    assert register(sctlr_name)==after
                    report['register_aliases']={'SCTLR_EL1':
                        sctlr_name+'; verified against original kernel MSR SCTLR_EL1, x0'}
                    if a.lifecycle:
                        print('Observed original storage constructor; waiting for class-registration gate',flush=True)
                        assert audit.get('storage_ready_function') and audit.get('concrete_ufs_metaclass')
                        gate=int(audit['storage_ready_function'],16)
                        concrete_meta=int(audit['concrete_ufs_metaclass'],16)
                        assert request(f'z1,{caller:x},4')=='OK'
                        assert request(f'Z1,{gate:x},4')=='OK'
                        stopped=False;request('c');stopped=True
                        assert register('pc')==gate, 'UFS lifecycle gate was not reached'
                        gate_caller=register('x30')
                        assert request(f'z1,{gate:x},4')=='OK'
                        assert request(f'Z1,{gate_caller:x},4')=='OK'
                        stopped=False;request('c');stopped=True
                        assert register('pc')==gate_caller
                        ready=register('x0')
                        report['storage_lifecycle_observation']={
                            'function_va':hex(gate),'return_pc':hex(gate_caller),
                            'function_return':ready,'stopped_before_ufs_mmio':True,
                            'guest_code_modified':False}
                        report['memory'][f'{concrete_meta:x}:50']=memory(concrete_meta,0x50).hex()
                        report['memory'][f'{meta:x}:50']=memory(meta,0x50).hex()
                        report['memory'][f'{service:x}:50']=memory(service,0x50).hex()
                        assert ready==1, 'original IOStorage class registration gate refused'
                        report['state_words']={}
                        for spec in a.state_word:
                            name,address=spec.split('=',1)
                            report['state_words'][name]=struct.unpack('<I',memory(int(address,0),4))[0]
                    report['bounded_timeout_interrupted']=False
                    break
                assert request(f'z1,{ctor:x},4')=='OK'
                stopped=False;request('s');stopped=True
                assert request(f'Z1,{ctor:x},4')=='OK'
            report['constructor_calls_seen']=seen
            if report['bounded_timeout_interrupted']:raise RuntimeError('selected storage constructor not observed')
        except Exception as error:
            report['error']=str(error)
            if connection:
                try:
                    connection.settimeout(2)
                    if not stopped:wire.request(None)
                    deadline=time.monotonic()+3
                    report['failure_pc']=hex(register('pc'))
                    for name in ('TCR_EL1','TTBR0_EL1','TTBR1_EL1','SCTLR_EL1','ESR_EL1','ELR_EL1','FAR_EL1','SP_EL1'):
                        if name in register_names:report['system_registers'][name]=hex(register(name))
                    if 'SCTLR_EL1' not in register_names and 'SCTLR' in register_names:
                        report['system_registers']['SCTLR_EL1']=hex(register('SCTLR'))
                    report['failure_registers']={n:hex(register(n)) for n in ('x0','x1','x2','x3','x29','x30','sp') if n in register_names}
                    try:
                        message=memory(register('x0'),256).split(b'\0',1)[0]
                        report['failure_message']=message.decode('utf-8',errors='replace')
                    except Exception:pass
                    try:
                        words=struct.unpack('<16Q',memory(register('sp'),128))
                        report['failure_stack_words']=[hex(word) for word in words]
                        report['failure_argument_strings']={}
                        for word in words:
                            if word>>48!=0xffff:continue
                            try:
                                value=memory(word,384).split(b'\0',1)[0]
                                if value and all(byte in (9,10,13) or 32<=byte<127 for byte in value):
                                    report['failure_argument_strings'][hex(word)]=value.decode('ascii')
                            except Exception:pass
                    except Exception:pass
                    report['state_words']={}
                    for spec in a.state_word:
                        name,address=spec.split('=',1)
                        report['state_words'][name]=struct.unpack('<I',memory(int(address,0),4))[0]
                    report['failure_stack']=[]
                    frame=register('x29')
                    for _ in range(16):
                        try:
                            previous,caller=struct.unpack('<QQ',memory(frame,16))
                            report['failure_stack'].append(dict(frame=hex(frame),return_pc=hex(caller)))
                            if previous<=frame:break
                            frame=previous
                        except Exception:break
                    report['failure_memory']={}
                    for address in (0xfffffff0073ea000,0xfffffff0073ea200,0xfffffff0071fd970,0xfffffff0079bb4e0):
                        try:report['failure_memory'][hex(address)]=memory(address,32).hex()
                        except Exception:pass
                except Exception:pass
        finally:
            if connection:connection.close()
            if child.poll() is None:
                child.terminate()
                try:child.wait(timeout=3)
                except subprocess.TimeoutExpired:child.kill();child.wait()
    (f/'storage-snapshot.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
    if report['bounded_timeout_interrupted']:raise SystemExit(1)


if __name__=='__main__':
    main()
