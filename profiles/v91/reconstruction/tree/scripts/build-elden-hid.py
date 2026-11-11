# SPDX-License-Identifier: BSD-2-Clause-Patent
# Elden adaptation of the original complete 21H461 HID ABI checks.
#!/usr/bin/env python3
"""Qualify an owned offline HID source against the unchanged target IOHIDDevice."""
import hashlib,json,struct,subprocess,sys,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/sheng-hid-input';OUT.mkdir(exist_ok=True)
sys.path.insert(0,str(ROOT/'kits/XnuPrelink'))
from native_prelink import macho,backed_offset,exports

def run(cmd,**kw):return subprocess.check_output([str(x) for x in cmd],timeout=90,**kw)
def require(value,why):
    if not value:raise SystemExit(why)
# The original reference candidate (v9) is the exact pinned carrier. If it is gone (build products are pruned), any later candidate
# built from the same original components is accepted: it must match its own plan and carry the same original IOHIDFamily
# instruction hash; every instruction/ABI check below still runs against it.
V9_SHA='df5d6ebda4d0cb2faea663e6bb393e6e12fcf9f52a197ffd58e6f75274a076ab'
HID_TEXT_SHA='7e4c5104814f2e773d5be13d092dec53ee44ba5a61a81264b229780406e86b72'
p=Path(os.environ['ELDEN_HID_CANDIDATE'])
b=p.read_bytes();plan=json.loads(Path(os.environ['ELDEN_HID_PLAN']).read_text());m=macho(b)
require(hashlib.sha256(b).hexdigest()==plan['candidate_sha256'],'Reference candidate matches its plan')
require(plan['candidate_sha256']==V9_SHA or plan['original_executable_sha256']['com.apple.iokit.IOHIDFamily']==HID_TEXT_SHA,'Exact original HID reference candidate')
def read(a,n):
    at=backed_offset(m['segments'],a,n,len(b));return b[at:at+n]
names={n:a for a,ns in exports(b,m).items() for n in ns}
e=next(e for e in plan['extents'] if e['module']=='com.apple.iokit.IOHIDFamily' and e['segment']=='__TEXT_EXEC')
require(hashlib.sha256(read(e['va'],e['size'])).hexdigest()==plan['original_executable_sha256']['com.apple.iokit.IOHIDFamily'],'Complete original HID instructions unchanged')
vptr=0xfffffff005d045c0;methods=struct.unpack('<207Q',read(vptr,207*8))
base=struct.unpack('<168Q',read(names['__ZTV9IOService']+16,168*8))
changed=[i for i in range(168) if methods[i]!=base[i]]
require(changed==[0,1,7,12,14,21,23,76,81,82,86,87,91,92,93,101,132,140],'Exact original HID native-base overrides')
require([i for i,a in enumerate(methods) if a==names['___cxa_pure_virtual']]==[180] and all(methods) and read(vptr+207*8,24)==bytes(24),'Exact original 207-slot abstract device')
# Original C2 calls the actual native IOService C2 through its existing thunk;
# its own vptr store and D2 tail-call remain unchanged.
require(read(0xfffffff004ad94f8,0x20)==bytes.fromhex('fd7bbfa9fd0300912bd90094489100f008011791080000f9fd7bc1a8c0035fd6'),'Original protected constructor and exact vptr store')
require(struct.unpack('<Q',read(0xfffffff005d00f10,8))[0]==names['__ZN9IOServiceC2EPK11OSMetaClass'],'Actual native protected constructor binding')
require(struct.unpack('<Q',read(0xfffffff005d00f18,8))[0]==names['__ZN9IOServiceD2Ev'],'Actual native protected destructor binding')
# Original start passes (self, provider) to slot 168, then (self, &descriptor)
# to pure slot 180 and checks the real returned status/descriptor.
require(read(0xfffffff004ad9aec,0x34)==bytes.fromhex('680240f908a142f9e00313aae10314aa00013fd6680240f9603d003608d142f9e1630091e00313aa00013fd6a02d0035e00f40f9'),'Original handleStart and report-descriptor ABI')
require(methods[181]==0xfffffff004add0c4 and read(0xfffffff004add110,28)==bytes.fromhex('680240f9081543f9e00313aae20316aae30315aae40314aa00013fd6'),'Original handleReport forwards timestamp, report, type and options')
header='''// Generated from the qualified original image; opaque private storage only.
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOMemoryDescriptor.h>
class IOHIDDevice : public IOService {
 OSDeclareAbstractStructors(IOHIDDevice)
 unsigned char originalPrivate[0xd0-sizeof(IOService)];
public:
 using IOService::setProperty;
 IOReturn Dispatch(IORPC) override;
 void free() override;
 bool init(OSDictionary *properties=nullptr) override;
 bool setProperty(const OSSymbol *,OSObject *) override;
 bool willTerminate(IOService *,IOOptionBits) override;
 IOReturn configureReport(IOReportChannelList *,IOReportConfigureAction,void *,void *) override;
 IOReturn updateReport(IOReportChannelList *,IOReportUpdateAction,void *,void *) override;
 bool start(IOService *) override;
 void stop(IOService *) override;
 bool handleOpen(IOService *,IOOptionBits,void *) override;
 void handleClose(IOService *,IOOptionBits) override;
 bool handleIsOpen(const IOService *) const override;
 bool matchPropertyTable(OSDictionary *,SInt32 *) override;
 IOReturn message(UInt32,IOService *,void *) override;
 IOReturn newUserClient(task_t,void *,UInt32,OSDictionary *,IOUserClient **) override;
'''
for i in range(168,207):
    if i==171:header+=' virtual OSString *newTransportString() const;\n'
    elif i==173:header+=' virtual OSString *newProductString() const;\n'
    elif i==180:header+=' virtual IOReturn newReportDescriptor(IOMemoryDescriptor **) const=0;\n'
    elif i==181:header+=' virtual IOReturn handleReport(IOMemoryDescriptor *,unsigned,IOOptionBits);\n'
    else:header+=f' virtual __attribute__((deprecated("Unqualified prototype: never call from source"))) void opaque{i}();\n'
header+='};\nstatic_assert(sizeof(IOHIDDevice)==0xd0,"Original device instance size");\n'
(OUT/'OriginalHidDeviceABI.h').write_text(header)
repository=Path(os.environ['ELDEN_HID_REPOSITORY']);revision='5f3a37cec0c3fc4234b10726b25489a55f0d77ef'
import tomllib
entry=next(e for e in tomllib.loads((ROOT/'components.toml').read_text())['component'] if e['id']=='ShengHidInput')
require(entry['revision']==revision,'Review changed HID component pin')
source=OUT/'src';source.mkdir(exist_ok=True)
for name in ('NativeHidInput.cpp','LICENSE'):
    (source/name).write_bytes(run(['git','-C',repository,'show',revision+':'+name]))
if os.environ.get('ELDEN_HID_PHYSICAL_TOUCH')=='1':
    path=source/'NativeHidInput.cpp';text=path.read_text()
    require(hashlib.sha256(path.read_bytes()).hexdigest()=='0445e5f451ffcc827bdd603e822180444d998427fa3a6d63559b1a7b8f850998','Exact pinned HID source before board adaptation')
    # Private tables recovered from the original 21H461 metaclass initializers.
    # EventDriver is 220 slots: the upstream 256-entry copy overreads its table.
    driver_vptr=0xfffffff005d06a68;event_vptr=0xfffffff005d07318
    private_source=Path(os.environ['ELDEN_TOUCH_HID_ABI_SOURCE'])
    private=json.loads(private_source.read_text())
    driver_table=struct.unpack('<220Q',read(driver_vptr,220*8))
    event_table=struct.unpack('<37Q',read(event_vptr,37*8))
    object_table=struct.unpack('<14Q',read(names['__ZTV8OSObject']+16,14*8))
    # Native inherited pointers are legitimately rebound by prelink. Verify
    # them against this candidate's real base tables, while every original
    # HID-owned entry must retain its separately recovered exact address.
    for kind,table,parent in [('driver',driver_table,base),('event',event_table,object_table)]:
        owned_private={int(i):int(v,16) for i,v in private[kind].items()}
        require(len(owned_private)==(69 if kind=='driver' else 26),'Complete original private override inventory')
        for i,address in enumerate(table):
            require(address==owned_private[i] if i in owned_private else i<len(parent) and address==parent[i],
                    'Original/rebound private HID method '+kind+':'+str(i))
    require(read(driver_vptr+220*8,8)==bytes(8) and read(event_vptr+37*8,8)==bytes(8),
            'Original private vtable boundaries')
    private_methods={198:0xfffffff004aface8}
    require(struct.unpack('<Q',read(driver_vptr+198*8,8))[0]==private_methods[198],
            'Original event-driver dispatch slot 198')
    for i,address in {18:0xfffffff004afcf90,23:0xfffffff004afcfb8,24:0xfffffff004afefdc,25:0xfffffff004afe4c4}.items():
        require(struct.unpack('<Q',read(event_vptr+i*8,8))[0]==address,'Original event method '+str(i))
    (OUT/'OriginalTouchEventABI.h').write_text(
        '// Generated only after original 21H461 private-table qualification.\n#pragma once\n'
        f'#define ELDEN_TOUCH_EVENT_DRIVER_VPTR {driver_vptr:#x}ULL\n'
        f'#define ELDEN_TOUCH_EVENT_VPTR {event_vptr:#x}ULL\n')
    changes={
        '#include "OriginalHidDeviceABI.h"':'#include "OriginalHidDeviceABI.h"\n#include "OriginalTouchEventABI.h"',
        'enum {kSlots=256,kDispatch=198':'enum {kSlots=220,kDispatch=198',
        'kEdgeX=48*65536/3048,kEdgeY=48*65536/2032':'kEdgeX=48*65536/1904,kEdgeY=48*65536/3040',
        'transformCoordinate(xs[i],30480),y=transformCoordinate(ys[i],20320)':'transformCoordinate(xs[i],32768),y=transformCoordinate(ys[i],32768)',
        '"Offline touch digitizer"':'"Lenovo Y700 Touch Screen"',
        'setProperty("OfflineInputProbe",true);':'setProperty("OfflineInputProbe",!touchOnly);',
        '    IOWorkLoop *getWorkLoop() const override {return inputLoop;}':
            '    bool secondaryHelper() const {unsigned enabled=0;return touchOnly && PE_parse_boot_argn("elden-touch-helper",&enabled,sizeof(enabled)) && enabled==1;}\n    IOWorkLoop *getWorkLoop() const override {return inputLoop;}',
        'touchOnly?"SPI":"Virtual"':'touchOnly&&!secondaryHelper()?"SPI":"Virtual"',
        'if(touchOnly){size=sizeof(touchDescriptor);memcpy(combined,touchDescriptor,size);}':
            'if(touchOnly&&!secondaryHelper()){size=sizeof(touchDescriptor);memcpy(combined,touchDescriptor,size);}',
        'if(touchEnabled){memcpy(combined+size,touchDescriptor,sizeof(touchDescriptor));size+=sizeof(touchDescriptor);}':
            'if(touchEnabled||secondaryHelper()){memcpy(combined+size,touchDescriptor,sizeof(touchDescriptor));size+=sizeof(touchDescriptor);}',
        'static bool mark(void *child) {':'static bool mark(void *child) {\n    if(!child || *reinterpret_cast<uintptr_t **>(child)!=reinterpret_cast<uintptr_t *>(ELDEN_TOUCH_EVENT_VPTR))return false;',
        'if(event && call<uint32_t,IOOptionBits>(event,kGetType,0)==kDigitizer)':
            'if(event && event->metaCast("IOHIDEvent") && *reinterpret_cast<uintptr_t **>(event)==reinterpret_cast<uintptr_t *>(ELDEN_TOUCH_EVENT_VPTR) && call<uint32_t,IOOptionBits>(event,kGetType,0)==kDigitizer)',
        'uintptr_t *table=*reinterpret_cast<uintptr_t **>(driver);':
            'uintptr_t *table=*reinterpret_cast<uintptr_t **>(driver);\n    if(table!=reinterpret_cast<uintptr_t *>(ELDEN_TOUCH_EVENT_DRIVER_VPTR))return;'}
    for old,new in changes.items():
        require(text.count(old)==1,'Review changed pinned touch source: '+old)
        text=text.replace(old,new)
    # Observe the real original EventDriver output before forwarding it. This
    # only uses the already qualified IOHIDEvent virtuals, never user memory.
    # Preserve late down/up edges after a swipe without logging every move.
    # Twenty-four emitted collections per boot bound output and dispatch cost.
    trace=r'''
extern "C" int elden_usb_preview_line(const char *,unsigned);
static void traceEvent(void *event,unsigned sequence,int child) {
    if(!event || *reinterpret_cast<uintptr_t **>(event)!=reinterpret_cast<uintptr_t *>(ELDEN_TOUCH_EVENT_VPTR) ||
       call<uint32_t,IOOptionBits>(event,kGetType,0)!=kDigitizer)return;
    char line[280];
    int n=snprintf(line,sizeof(line),"\nELDEN_HID_EVENT n=%u child=%d index=%d identity=%d type=%d x=%d y=%d mask=%x touch=%d range=%d\n",
        sequence,child,call<int32_t,uint32_t,IOOptionBits>(event,kGetInteger,kIndex,0),
        call<int32_t,uint32_t,IOOptionBits>(event,kGetInteger,0xB0006,0),
        call<int32_t,uint32_t,IOOptionBits>(event,kGetInteger,0xB0004,0),
        call<int32_t,uint32_t,IOOptionBits>(event,kGetFixed,kX,0),
        call<int32_t,uint32_t,IOOptionBits>(event,kGetFixed,kY,0),
        (unsigned)call<int32_t,uint32_t,IOOptionBits>(event,kGetInteger,kMask,0),
        call<int32_t,uint32_t,IOOptionBits>(event,kGetInteger,kTouch,0),
        call<int32_t,uint32_t,IOOptionBits>(event,kGetInteger,0xB0008,0));
    if(n>0 && (unsigned)n<sizeof(line))elden_usb_preview_line(line,(unsigned)n);
}
static void traceCollection(OSObject *event) {
    static unsigned count,lastTouch=2,emitted;
    if(!event || !event->metaCast("IOHIDEvent") ||
       *reinterpret_cast<uintptr_t **>(event)!=reinterpret_cast<uintptr_t *>(ELDEN_TOUCH_EVENT_VPTR) ||
       call<uint32_t,IOOptionBits>(event,kGetType,0)!=kDigitizer)return;
    unsigned n=__atomic_add_fetch(&count,1u,__ATOMIC_RELAXED);
    unsigned touching=call<int32_t,uint32_t,IOOptionBits>(event,kGetInteger,kTouch,0)!=0;
    unsigned previous=__atomic_exchange_n(&lastTouch,touching,__ATOMIC_RELAXED);
    if(n>3 && previous==touching)return;
    if(__atomic_add_fetch(&emitted,1u,__ATOMIC_RELAXED)>24)return;
    traceEvent(event,n,-1);
    OSArray *children=*reinterpret_cast<OSArray **>(reinterpret_cast<uint8_t *>(event)+24);
    if(children && OSDynamicCast(OSArray,children)) {
        unsigned limit=children->getCount();if(limit>10)limit=10;
        for(unsigned i=0;i<limit;++i){OSObject *child=children->getObject(i);if(child && child->metaCast("IOHIDEvent"))traceEvent(child,n,(int)i);}
    }
}
'''
    marker='static void dispatch(IOService *self,OSObject *event,IOOptionBits options) {'
    require(text.count(marker)==1,'Qualified event dispatch trace insertion')
    text=text.replace(marker,trace+'\n'+marker+'\n    traceCollection(event);',1)
    path.write_text(text)
    (OUT/'touch-board-patch.json').write_text(json.dumps({'base_revision':revision,'normalized_coordinates':True,'edge_pixels':48,'panel':[1904,3040],
        'original_event_driver_slots':220,'original_event_slots':37,'private_vtable_runtime_identity_gate':True,
        'source_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'changes':changes},indent=2)+'\n')
args=json.loads((ROOT/'build/sheng-ufs/compile-command.json').read_text())
old=next(x for x in args if x.endswith('/src/NativeUfs.cpp'))
args=[str(source/'NativeHidInput.cpp') if x==old else x for x in args]
args[args.index('-o')+1]=str(OUT/'NativeHidInput.o');args+=['-I',str(OUT),'-mgeneral-regs-only']
record=json.loads((ROOT/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json').read_text().strip().rstrip(','))
print(run(args,cwd=record['directory'],stderr=subprocess.STDOUT).decode(errors='replace'),end='')
obj=OUT/'NativeHidInput.o';raw=obj.read_bytes();om=macho(raw)
st=next(p for c,p,_ in om['commands'] if c==2);sy,count,strings,_=struct.unpack_from('<4I',raw,st+8);symbols=[]
for i in range(count):
    ix,kind,section,_,va=struct.unpack_from('<IBBHQ',raw,sy+i*16)
    symbols.append((raw[strings+ix:raw.index(b'\0',strings+ix)].decode(),va,kind))
vt=next(va for name,va,_ in symbols if name=='__ZTV14NativeHidInput');relocations={}
for segment in om['segments']:
    for sec in segment['sections']:
        if sec['va']<=vt<sec['va']+sec['size']:
            ro,rc=struct.unpack_from('<2I',raw,sec['command_offset']+56)
            for i in range(rc):
                offset,bits=struct.unpack_from('<2I',raw,ro+i*8)
                if bits>>27&1 and bits>>28==0 and bits>>25&3==3:relocations[sec['va']+offset]=symbols[bits&0xffffff][0]
compiled=[relocations.get(vt+16+i*8) for i in range(207)]
require(all(compiled) and vt+16+207*8 not in relocations,'Compiled exact 207-slot vtable')
bindings={'__ZN11IOHIDDeviceC2EPK11OSMetaClass':0xfffffff004ad94f8,'__ZN11IOHIDDeviceD2Ev':0xfffffff004ad9518,'__ZN11IOHIDDevice10gMetaClassE':0xfffffff006ae6fa0,'__ZTV11IOHIDDevice':vptr-16}
owned=[]
for i,name in enumerate(compiled):
    if name.startswith(('__ZN14NativeHidInput','__ZNK14NativeHidInput')):owned.append(i)
    elif '11IOHIDDevice' in name:bindings[name]=methods[i]
    else:require(i<168 and names.get(name)==methods[i],'Actual inherited native method '+str(i))
require(owned==[0,1,7,14,86,87,111,171,173,180],'Only qualified HID methods overridden')
# This kernel compiler resolves qualified super calls through the original vtable.
undefined={n for n,_,k in symbols if k==1 and '11IOHIDDevice' in n}
require(undefined==set(bindings),'All and only actual original HID imports bound')
asm=OUT/'original-bindings.S';asm.write_text(''.join(f'.globl {n}\n.set {n}, {a:#x}\n' for n,a in sorted(bindings.items())))
sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
run(['xcrun','clang','-target','arm64-apple-ios17.0','-isysroot',sdk,'-c',asm,'-o',OUT/'hid-original-bindings.o'])
llvm=Path(os.environ['ELDEN_LLVM_BIN']);late=OUT/'NativeHidInput.late.o'
run([llvm/'llvm-objcopy','--remove-section=__DATA,__mod_init_func','--globalize-symbol=__GLOBAL__sub_I_NativeHidInput.cpp',obj,late])
def code(data,image):
    s=next(s for g in image['segments'] for s in g['sections'] if s['name']=='__text');return data[s['fileoff']:s['fileoff']+s['size']]
require(code(raw,om)==code(late.read_bytes(),macho(late.read_bytes())),'Owned code unchanged by deferred metaclass registration')
(OUT/'abi-audit.json').write_text(json.dumps({'reference_candidate_sha256':plan['candidate_sha256'],'original_text_sha256':plan['original_executable_sha256']['com.apple.iokit.IOHIDFamily'],'original_slots':207,'original_instance_size':208,'owned_slots':owned,'bindings':bindings,'runtime_qualified':False,'source_revision':revision,'source_sha256':hashlib.sha256((source/'NativeHidInput.cpp').read_bytes()).hexdigest()},indent=2)+'\n')
print('PASS: original HID instruction identity, base imports, call boundaries and exact owned vtable')
