# SPDX-License-Identifier: BSD-2-Clause-Patent
# Copyright (c) 2026, UniXNU contributors. Derived from sheng build-gop-legacy-display.py.
# Elden keeps original ABI/binding checks and supplies only its GOP adapter.
#!/usr/bin/env python3
"""Bind an owned concrete backend to the unchanged original Legacy lifecycle."""
import os,runpy,shutil,tomllib
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
# This pinned Elden tree predates kernel_target.py. Compile C++ with its real
# qualified kernel command; the assembly aliases contain no executable code.
TRIPLE='arm64-apple-ios17.0'
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'build/native-legacy-display';OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(ROOT/'kits/XnuPrelink'))
from native_prelink import macho,backed_offset,exports
sys.path.insert(0, str(Path(__file__).resolve().parent))
def run(cmd,**kw):
    try:return subprocess.check_output([str(x) for x in cmd],timeout=90,**kw)
    except subprocess.CalledProcessError as error:
        if error.output:print(error.output.decode(errors='replace') if isinstance(error.output,bytes) else error.output)
        raise
def require(v,why):
    if not v:raise SystemExit(why)
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
repository=Path(os.environ['ELDEN_GOP_REPOSITORY']);revision='cf3b42979a9c44219e7bb149543e6fc3388b58ed'
entry=next(e for e in tomllib.loads((ROOT/'components.toml').read_text())['component'] if e['id']=='GopFramebuffer')
require(entry['revision']==revision,'Review changed display source revision')
source=OUT/'src';source.mkdir(exist_ok=True)
for name in ('NativeLegacyDisplay.cpp','NativeLegacyRequest.h','LICENSE'):
    (source/name).write_bytes(run(['git','-C',repository,'show',revision+':'+name]))
compiler=runpy.run_path(str(ROOT/'scripts/build-sheng-ufs-probe.py'))['compile_command']
def compile_owned(src,obj):
    command,directory=compiler(src,obj)
    command+=['-I',str(OUT),'-I',str(source)]
    run(command,cwd=directory,stderr=subprocess.STDOUT)
print(run([sys.executable,ROOT/'scripts/audit-elden-gop.py','--candidate',os.environ['ELDEN_DISPLAY_CANDIDATE'],
    '--plan',os.environ['ELDEN_DISPLAY_PLAN'],'--output',OUT/'original-abi.json'],text=True),end='')
audit=json.loads((OUT/'original-abi.json').read_text())
pure=audit['abstract_slots'];methods=audit['methods']
header='''// SPDX-License-Identifier: BSD-2-Clause-Patent
// Opaque ABI storage for the exact original class, not source-level access
// to its private fields. The actual original metadata supplies its full
// Framebuffer/Service/IOService ancestry. Only qualified base prototypes may
// be called from source; opaque entries are original dispatch targets only.
#pragma once
#include <IOKit/IOService.h>
#include <libkern/libkern.h>
struct OriginalLegacySwapRequest;
// Original state request (skip/requested and skip/powered word pairs, then the changed flag) taken by the state setter.
struct OriginalLegacyStateRequest {uint32_t skipRequested,requested,skipPowered,powered;uint8_t changed;uint8_t pad[11];};
// Original field offsets: vsync client queue in the display; rectangles, layer masks (updated, disabled, background),
// options, dimensions and surfaces in a swap request.
static constexpr unsigned kLegacyVsyncClients=5592;
static constexpr unsigned kRequestRect0=17,kRequestRect1=81,kRequestMasks=792,kRequestOptions=804,kRequestDims=816,kRequestSurfaces=2624;
// Backend leaves: pure original slots the owned backend implements only as guards.
#define NATIVE_LEGACY_LEAVES(LEAF) LEAF(211) LEAF(245) LEAF(246) LEAF(306) LEAF(307) LEAF(338) LEAF(345)
class IOMobileFramebufferLegacy : public IOService {
 OSDeclareAbstractStructors(IOMobileFramebufferLegacy)
 unsigned char originalPrivate[0x18a0-sizeof(IOService)];
public:
 // Both fields are qualified by the complete original slot-249 getter.
 // This modifies this object's own mode state, never a vtable or executable.
 void installBootDimensions(uint32_t width,uint32_t height) {
  *reinterpret_cast<uint32_t *>(reinterpret_cast<unsigned char *>(this)+5808)=width;
  *reinterpret_cast<uint32_t *>(reinterpret_cast<unsigned char *>(this)+5812)=height;
 }
 const OriginalLegacySwapRequest *pendingSwapRequest() const {
  const OriginalLegacySwapRequest *request=nullptr;
  memcpy(&request,reinterpret_cast<const unsigned char *>(this)+5480,sizeof(request));return request;
 }
 // The real normal scanout backend removes its pending head before calling
 // slot 295. Slot 312 is a discard path (terminal state 8), not presentation.
 // The queue is a tail queue (head at +5480, tail pointer at +5488, each request's next at +2960 and previous-link at +2968): taking the head
 // moves the next request, if a client already queued one behind it (int-132: swap 3 arrived during swap 2's GPU copy and the old
 // single-entry form refused to retire swap 2, so every wait on it stayed busy), up to the head. The original queue's reference
 // moves to the current-swap slot for the original completion body.
 bool activateOnlyPendingSwap(const OriginalLegacySwapRequest *request) {
  auto self=reinterpret_cast<unsigned char *>(this);
  auto bytes=reinterpret_cast<const unsigned char *>(request);
  const void *current=nullptr,*head=nullptr,*tail=nullptr,*next=nullptr,*previous=nullptr;
  memcpy(&current,self+5472,8);memcpy(&head,self+5480,8);memcpy(&tail,self+5488,8);
  if(!request || current || head!=request)return false;
  memcpy(&next,bytes+2960,8);memcpy(&previous,bytes+2968,8);
  if(previous!=self+5480)return false;
  if(next) {
   const void *link=self+5480;
   memcpy(const_cast<unsigned char *>(reinterpret_cast<const unsigned char *>(next))+2968,&link,8);
  } else if(tail!=bytes+2960)return false;
  memcpy(self+5472,&request,8);
  if(next) {memcpy(self+5480,&next,8);}
  else {const void *empty=nullptr,*emptyTail=self+5480;memcpy(self+5480,&empty,8);memcpy(self+5488,&emptyTail,8);}
  return true;
 }
 void free() override;
 bool init(OSDictionary *properties=nullptr) override;
 IOReturn setProperties(OSObject *) override;
 void systemWillShutdown(IOOptionBits) override;
 IOService *probe(IOService *,SInt32 *) override;
 bool start(IOService *) override;
 void stop(IOService *) override;
 IOWorkLoop *getWorkLoop() const override;
 IOReturn newUserClient(task_t,void *,UInt32,IOUserClient **) override;
 IOReturn setPowerState(unsigned long,IOService *) override;
'''
for i in range(168,358):
    if i==184:header+=' virtual bool isMainDisplay();\n'
    elif i==188:header+=' virtual IOReturn getDotPitch(uint32_t *);\n'
    elif i==213:header+=' virtual IOReturn supportedFrameInfo(uint32_t,uint32_t *,char *,uint32_t *)=0;\n'
    elif i==249:header+=' virtual void getDisplayDimensions(uint32_t *,uint32_t *);\n'
    elif i==290:header+=' virtual void kickSwapQueue();\n'
    elif i==296:header+=' virtual bool validateSwap(const OriginalLegacySwapRequest *);\n'
    elif i==295:header+=' virtual void completeCurrentSwap();\n'
    elif i==337:header+=' virtual IOReturn collectCompletionReports(uint32_t,void *,uint32_t *,uint32_t)=0;\n'
    elif i==288:header+=' virtual bool startHardware();\n'
    elif i==354:header+=' virtual int applyStateRequest(OriginalLegacyStateRequest *,bool);\n'
    elif i==301:header+=' virtual bool getRefreshAttributes(uint64_t *,bool);\n'
    elif i in pure:header+=f' virtual __attribute__((noreturn)) void opaque{i}()=0;\n'
    else:header+=f' virtual __attribute__((deprecated("Unclassified ABI: never call from source"))) void opaque{i}();\n'
header+='};\nstatic_assert(sizeof(IOMobileFramebufferLegacy)==0x18a0,"Actual original instance extent");\n'
(OUT/'OriginalLegacyFramebufferABI.h').write_text(header)
compile_owned(source/'NativeLegacyDisplay.cpp',OUT/'NativeLegacyDisplay.o')
obj=OUT/'NativeLegacyDisplay.o';raw=obj.read_bytes();image=macho(raw)
st=next(p for c,p,_ in image['commands'] if c==2);sy,count,strings,_=struct.unpack_from('<4I',raw,st+8);symbols=[]
for i in range(count):
    ix,kind,section,_,va=struct.unpack_from('<IBBHQ',raw,sy+i*16)
    symbols.append((raw[strings+ix:raw.index(b'\0',strings+ix)].decode(),va,kind))
vtable=next(va for name,va,_ in symbols if name=='__ZTV19NativeLegacyDisplay');relocations={}
for segment in image['segments']:
    for sec in segment['sections']:
        if sec['va']<=vtable<sec['va']+sec['size']:
            ro,rc=struct.unpack_from('<2I',raw,sec['command_offset']+56)
            for i in range(rc):
                offset,bits=struct.unpack_from('<2I',raw,ro+i*8)
                if bits>>27&1 and bits>>28==0 and bits>>25&3==3:relocations[sec['va']+offset]=symbols[bits&0xffffff][0]
compiled=[relocations.get(vtable+16+i*8) for i in range(358)]
require(all(compiled) and vtable+16+358*8 not in relocations,'Exact owned 358-entry vtable')
candidate=Path(os.environ['ELDEN_DISPLAY_CANDIDATE'])
old=candidate.read_bytes();old_image=macho(old)
names={n:v for v,group in exports(old,old_image).items() for n in group}
def read(va,n):
    at=backed_offset(old_image['segments'],va,n,len(old));return old[at:at+n]
bindings={'__ZN25IOMobileFramebufferLegacyC2EPK11OSMetaClass':0xfffffff0057865cc,
          '__ZN25IOMobileFramebufferLegacyD2Ev':0xfffffff005786618,
          '__ZN25IOMobileFramebufferLegacy10gMetaClassE':0xfffffff006b35940,
          '__ZTV25IOMobileFramebufferLegacy':int(audit['legacy_vptr'],16)-16}
owned=[];native=[]
for i,name in enumerate(compiled):
    expected=int(methods[i]['address'],16)
    if name.startswith(('__ZN19NativeLegacyDisplay','__ZNK19NativeLegacyDisplay')):
        owned.append(i);require(i in (0,1,7,14,84,86,87,141,155,184,188,288,290,296,301) or i in pure,'Only qualified own methods and guarded leaves override')
    elif '25IOMobileFramebufferLegacy' in name:
        require(i not in pure,'No unresolved original abstract backend target');bindings[name]=expected
    else:
        require(i<168 and names.get(name)==expected,'Actual inherited native base symbol '+str(i));native.append(i)
require(owned==sorted([0,1,7,14,84,86,87,141,155,184,188,288,290,296,301]+pure),'Exact owned method set')
undefined={n for n,_,k in symbols if k==1 and 'IOMobileFramebufferLegacy' in n}
require(undefined==set(bindings),'All and only actual original-class imports qualified')
# Exact original IOSurface getters, independently observed through the real
# original swap request. These aliases call original code; no object layout
# is substituted and no original method/vtable is patched.
for name,address,field,wide in [
    ('descriptor',0xfffffff004b9ead0,48,True),('width',0xfffffff004b9ebbc,88,True),
    ('height',0xfffffff004b9ebc4,96,True),('stride',0xfffffff004b9ebd4,104,True),
    ('allocation',0xfffffff004b9ebfc,144,True),('format',0xfffffff004b9ebac,128,False),
    ('planes',0xfffffff004b9ebf4,152,False)]:
    expected=(0xf9400000 if wide else 0xb9400000)|((field//(8 if wide else 4))<<10)
    require(read(address,8)==struct.pack('<2I',expected,0xd65f03c0),'Exact original surface getter '+name)
    bindings['_native_legacy_surface_'+name]=address
# The original display-refresh notification sender (self, timestamp, continuous host time, frame interval): it sends the six-word
# vsync message (type 5) to every client entry queued at +5592, the tail queue that selector 72's registration (slot 208) fills at
# +5512 + type*16. The platform subclass that normally calls it on each panel vblank is not part of this port.
require(read(0xfffffff005796720,16)==bytes.fromhex('ff0302d1fd7b07a9fdc30191a0831ff8') and
        read(0xfffffff005796830,4)==bytes.fromhex('08ed4af9') and
        read(0xfffffff00579685c,16)==bytes.fromhex('e2230091c3008052240080521684ff97') and
        read(0xfffffff005795fbc,4)==bytes.fromhex('0ab182d2'),
        'Original vsync sender, its +5592 queue and six-word drop-allowed async send')
bindings['_native_legacy_vsync_notify']=0xfffffff005796720
# Slot 301 fills the six refresh words that setDisplayRefreshProperties publishes as IOMFBDisplayRefresh (minimum interval, its mach
# time, maximum interval, its mach time, step, its mach time; intervals in 32.32 seconds): the generic body fixes the minimum at
# 0x04444444 (1/60 s), and the render server paces its display timer by it.
require(int(methods[301]['address'],16)==0xfffffff00579ea5c and read(0xfffffff00579ea68,8)==bytes.fromhex('888888d28888a0f2') and
        read(0xfffffff00579ec54,4)==bytes.fromhex('08b544f9'),
        'Original slot-301 refresh words with the 1/60 s minimum, called by setDisplayRefreshProperties')
# setDisplayRefreshProperties itself (self): fills the six words through slot 301 and publishes them as IOMFBDisplayRefresh. Nothing in
# the family calls it (the platform subclass does), so without it the render server falls back to its own 1/60 s heartbeat.
require(read(0xfffffff00579ebb8,16)==bytes.fromhex('ff0302d1fd7b07a9fdc30191a0031ff8') and
        read(0xfffffff00579ed54,16)==bytes.fromhex('086540f90173ff90218c0c9100013fd6'),
        'Original setDisplayRefreshProperties publishing IOMFBDisplayRefresh through setProperty')
bindings['_native_legacy_set_refresh_properties']=0xfffffff00579ebb8
# Both protected destructors/constructors must follow the exact original chain.
def branch(pc):
    word=struct.unpack('<I',read(pc,4))[0];require(word&0xfc000000==0x94000000,'Actual protected base call')
    value=word&0x3ffffff;return pc+4*(value-(1<<26) if value&(1<<25) else value)
require(branch(0xfffffff0057865fc)==0xfffffff00577b510 and branch(0xfffffff005786630)==0xfffffff00577b55c,
        'Original protected Legacy C2/D2 really call their original Service bases')
# Actual caller passes only self at slot 288 and tests bit zero of w0.
require(read(0xfffffff005787ef8,16)==bytes.fromhex('080040f9088144f900013fd6e0000037'),
        'Original bool start_hardware() call and result check')
getter=int(methods[249]['address'],16)
for offset,field in [(0x30,5808),(0x58,5812)]:
    require(struct.unpack('<I',read(getter+offset,4))[0]==0xb9400000|((field//4)<<10)|(8<<5)|8,
            'Actual original width/height getter fields')
require(int(methods[295]['address'],16)==0xfffffff005795b88 and
        read(0xfffffff005795b98,16)==bytes.fromhex('e00740f9280080520101001272d3ff97'),
        'Actual original slot-295 passes true to the normal current completion body')
require(read(0xfffffff00578aa7c,24)==bytes.fromhex('080040f9084545f9030080d2e20303aa0400805200013fd6'),
        'Original completion calls optional report slot 337 with null outputs and zero limit')
require(read(0xfffffff005777e9c,16)==bytes.fromhex('080040f908e142f900013fd6c0000036'),
        'Original isMainDisplay dispatch uses only self and tests boolean bit zero')
asm=OUT/'original-bindings.S';asm.write_text(''.join(f'.globl {name}\n.set {name}, {va:#x}\n' for name,va in sorted(bindings.items())))
sdk=run(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()
run(['xcrun','clang','-target',TRIPLE,'-isysroot',sdk,'-c',asm,'-o',OUT/'original-bindings.o'])

llvm=Path(os.environ['ELDEN_LLVM_OBJCOPY']);late=OUT/'NativeLegacyDisplay.late.o'
run([llvm,'--remove-section=__DATA,__mod_init_func','--globalize-symbol=__GLOBAL__sub_I_NativeLegacyDisplay.cpp',obj,late])
def text_bytes(data):
    image=macho(data);sec=next(s for g in image['segments'] for s in g['sections'] if s['name']=='__text');return data[sec['fileoff']:sec['fileoff']+sec['size']]
require(text_bytes(obj.read_bytes())==text_bytes(late.read_bytes()),'Late initializer staging changed executable instructions')
glue=ROOT/'scripts/driver-patches/EldenDisplayGlue.cpp'
glue_command,glue_directory=compiler(glue,OUT/'EldenDisplayGlue.o')
glue_command+=['-I',str(OUT)]
owned_features=('ELDEN_HID_BUILD','ELDEN_TOUCH_HID_BUILD','ELDEN_TOUCH_RUNTIME_BUILD','ELDEN_DATA_INSPECT_BUILD','ELDEN_BLUETOOTH_BUILD','ELDEN_BLUETOOTH_HCI_BUILD','ELDEN_BLUETOOTH_UART_BUILD','ELDEN_WCN_POWER_BUILD','ELDEN_BT_NATIVE_BUILD','ELDEN_WIFI_FW_CHECK_BUILD',
                'ELDEN_TOUCH_INSPECT_BUILD','ELDEN_POWER_INSPECT_BUILD')
if os.environ.get('ELDEN_BLUETOOTH_HCI_BUILD')=='1':
    require(os.environ.get('ELDEN_BLUETOOTH_BUILD')=='1','HCI continuation requires the existing Bluetooth profile')
if os.environ.get('ELDEN_BLUETOOTH_UART_BUILD')=='1':
    require(os.environ.get('ELDEN_BLUETOOTH_HCI_BUILD')=='1','UART backend requires the full HCI profile')
if os.environ.get('ELDEN_WCN_POWER_BUILD')=='1':
    require(os.environ.get('ELDEN_BLUETOOTH_UART_BUILD')=='1','WCN power requires the complete UART/HCI profile')
if os.environ.get('ELDEN_BT_NATIVE_BUILD')=='1':
    require(os.environ.get('ELDEN_WCN_POWER_BUILD')=='1','Native BT requires actual power/UART/HCI ownership')
if os.environ.get('ELDEN_TOUCH_HID_BUILD')=='1':
    require(os.environ.get('ELDEN_HID_BUILD')=='1' and os.environ.get('ELDEN_HID_PHYSICAL_TOUCH')=='1',
            'Physical digitizer requires HID and the reviewed Elden coordinate adaptation')
if os.environ.get('ELDEN_TOUCH_RUNTIME_BUILD')=='1':
    require(os.environ.get('ELDEN_TOUCH_HID_BUILD')=='1','Touch runtime requires its separate digitizer')
for feature in owned_features:
    if os.environ.get(feature)=='1':glue_command+=['-D'+feature+'=1']
run(glue_command,cwd=glue_directory,stderr=subprocess.STDOUT)
process=ROOT/'scripts/driver-patches/EldenUserWaitProbe.c'
record=json.loads((ROOT/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json').read_text().strip().rstrip(','))
arguments=record['arguments'];command=[];i=0
while i<len(arguments):
    value=arguments[i]
    if value==record['file']:command.append(str(process))
    elif value in ('-o','-MF','-MT','-MJ'):
        if value=='-o':command+=['-o',str(OUT/'EldenUserWaitProbe.o')]
        i+=1
    elif value=='-Xclang' and arguments[i+1].startswith('-W'):i+=1
    elif not value.startswith('-W'):command.append(value)
    i+=1
command+=['-Wall','-Wextra','-Werror','-mgeneral-regs-only','-I',str(ROOT/'.cache/xnu-work/osfmk')]
run(command,cwd=record['directory'],stderr=subprocess.STDOUT)
owned_c_command=list(command);owned_c_directory=record['directory']
keys=Path(os.environ['ELDEN_KEYEVENTS_REPOSITORY']);key_revision='42ff83e08b2265fe741d4adaf084bebe003452ba'
key_entry=next(e for e in tomllib.loads((ROOT/'components.toml').read_text())['component'] if e['id']=='AppleKeyStoreState')
require(key_entry['revision']==key_revision,'Review changed AppleKeyStoreState revision')
key_source=source/'NativeKeyStoreEvents.cpp'
key_source.write_bytes(run(['git','-C',keys,'show',key_revision+':NativeKeyStoreEvents.cpp']))
(source/'KeyEvents.LICENSE').write_bytes(run(['git','-C',keys,'show',key_revision+':LICENSE']))
compile_owned(key_source,OUT/'NativeKeyStoreEvents.o')
key_late=OUT/'NativeKeyStoreEvents.late.o'
run([llvm,'--remove-section=__DATA,__mod_init_func','--globalize-symbol=__GLOBAL__sub_I_NativeKeyStoreEvents.cpp',OUT/'NativeKeyStoreEvents.o',key_late])
require(text_bytes((OUT/'NativeKeyStoreEvents.o').read_bytes())==text_bytes(key_late.read_bytes()),'Late key-event staging changed executable instructions')
key_glue=ROOT/'scripts/driver-patches/EldenKeyEventsGlue.cpp';compile_owned(key_glue,OUT/'EldenKeyEventsGlue.o')
archive=OUT/'libNativeLegacyDisplay.late.a';archive.unlink(missing_ok=True)
hid_objects=[];hid_sources=[]
if os.environ.get('ELDEN_HID_BUILD')=='1':
    run([sys.executable,ROOT/'scripts/build-elden-hid.py'],cwd=ROOT,stderr=subprocess.STDOUT)
    hid=ROOT/'build/sheng-hid-input'
    command,directory=compiler(ROOT/'scripts/driver-patches/EldenHidGlue.cpp',OUT/'EldenHidGlue.o')
    command+=['-I',str(hid),'-mgeneral-regs-only']
    if os.environ.get('ELDEN_TOUCH_HID_BUILD')=='1':
        point_header=Path(os.environ['ELDEN_TOUCH_DRIVER_SOURCE'])/'PointReport.h'
        require(point_header.is_file(),'Board point-report declaration is required')
        command+=['-DELDEN_TOUCH_HID_BUILD=1','-I',str(point_header.parent)]
        hid_sources.append(point_header)
    run(command,cwd=directory,stderr=subprocess.STDOUT)
    hid_objects=[hid/'NativeHidInput.late.o',hid/'hid-original-bindings.o',OUT/'EldenHidGlue.o']
    hid_sources+=[ROOT/'scripts/driver-patches/EldenHidGlue.cpp',hid/'src/NativeHidInput.cpp']
wifi_objects=[];wifi_sources=[]
if os.environ.get('ELDEN_WIFI_FW_CHECK_BUILD')=='1':
    require(os.environ.get('XNU_ELDEN_MODEM_READER')=='1' and os.environ.get('XNU_ELDEN_POWER_RUNTIME')=='1',
            'Wi-Fi firmware reads require the admitted native modem gateway and FAT runtime')
    wifi=Path(os.environ['ELDEN_WIFI_SOURCE'])
    resources=Path(os.environ['ELDEN_WIFI_RESOURCE_HEADER'])
    inputs=Path(os.environ['ELDEN_WIFI_INPUT_HEADER'])
    fat=Path(os.environ['ELDEN_MODEM_SOURCE'])
    includes=['-I',str(wifi),'-I',str(fat),'-I',str(resources.parent),'-I',str(inputs.parent)]
    obj=OUT/'EldenWifiModemFiles.o';cmd=list(owned_c_command)
    cmd[cmd.index(str(process))]=str(wifi/'ModemFiles.c');cmd[cmd.index('-o')+1]=str(obj);cmd+=includes
    run(cmd,cwd=owned_c_directory,stderr=subprocess.STDOUT);wifi_objects.append(obj)
    obj=OUT/'EldenWifiFirmwareCheck.o';cmd,directory=compiler(wifi/'FirmwareCheck.cpp',obj);cmd+=includes
    run(cmd,cwd=directory,stderr=subprocess.STDOUT);wifi_objects.append(obj)
    wifi_sources=[wifi/'ModemFiles.c',wifi/'ModemFiles.h',wifi/'FirmwareCheck.cpp',resources,inputs]
touch_objects=[];touch_sources=[]
if os.environ.get('ELDEN_TOUCH_RUNTIME_BUILD')=='1':
    touch_driver=Path(os.environ['ELDEN_TOUCH_DRIVER_SOURCE'])
    touch_geni=Path(os.environ['ELDEN_TOUCH_GENI_SOURCE'])
    touch_board=Path(os.environ['ELDEN_TOUCH_BOARD_HEADER'])
    touch_resources=Path(os.environ['ELDEN_TOUCH_RESOURCE_HEADER'])
    firmware_path=Path(os.environ['ELDEN_TOUCH_FIRMWARE_INPUT'])
    firmware=firmware_path.read_bytes()
    require(len(firmware)==253952 and hashlib.sha256(firmware).hexdigest()==
            '413216d0e8738078414fa681262439ab6d1c181f02974215e0394f102fcc08dc',
            'Exact stock Elden NT36536 firmware input')
    fw_header=source/'EldenNvtFirmware.h'
    fw_header.write_text('// Generated stock firmware; never commit this file.\n'
        'static const uint8_t elden_nvt_firmware[]={\n'+
        '\n'.join(','.join('0x%02x'%v for v in firmware[i:i+32])+',' for i in range(0,len(firmware),32))+'\n};\n')
    includes=['-I',str(touch_driver),'-I',str(touch_geni),'-I',str(touch_board.parent),
              '-I',str(touch_resources.parent),'-I',str(source)]
    for name,src in [('EldenNvtProtocol',touch_driver/'Protocol.c'),('EldenNvtPoints',touch_driver/'PointReport.c'),
                     ('EldenNvtPlatform',touch_driver/'Platform.c'),('EldenNvtGeni',touch_geni/'GeniSpi.c'),
                     ('EldenNvtDmaRoute',touch_driver/'DmaRoute.c'),('EldenNvtDmaTransfer',touch_driver/'DmaTransfer.c'),
                     ('EldenNvtDmaCache',touch_driver/'DmaCache.c')]:
        obj=OUT/(name+'.o');cmd=list(owned_c_command)
        cmd[cmd.index(str(process))]=str(src);cmd[cmd.index('-o')+1]=str(obj);cmd+=includes
        run(cmd,cwd=owned_c_directory,stderr=subprocess.STDOUT);touch_objects.append(obj);touch_sources.append(src)
    runtime=touch_driver/'Runtime.cpp';cmd,directory=compiler(runtime,OUT/'EldenNvtRuntime.o');cmd+=includes+['-mgeneral-regs-only']
    run(cmd,cwd=directory,stderr=subprocess.STDOUT);touch_objects.append(OUT/'EldenNvtRuntime.o')
    dma_native=touch_driver/'NativeDmaBuffer.cpp';obj=OUT/'EldenNvtDmaBuffer.o';cmd,directory=compiler(dma_native,obj);cmd+=includes+['-mgeneral-regs-only']
    run(cmd,cwd=directory,stderr=subprocess.STDOUT);touch_objects.append(obj)
    touch_sources += [runtime,touch_driver/'Platform.h',touch_driver/'Protocol.h',touch_driver/'PointReport.h',touch_driver/'DmaSurvey.h',
                      dma_native,touch_driver/'NativeDmaBuffer.h',touch_driver/'DmaRoute.h',touch_driver/'DmaTransfer.h',touch_driver/'DmaCache.h',touch_driver/'RunControl.h',
                      touch_geni/'GeniSpi.h',touch_board,touch_resources,fw_header]
    (OUT/'touch-firmware-input.json').write_text(json.dumps({'sha256':hashlib.sha256(firmware).hexdigest(),
        'bytes':len(firmware),'generated_header_sha256':sha(fw_header),'hardware_tested':False},indent=2)+'\n')
if os.environ.get('ELDEN_TOUCH_INSPECT_BUILD')=='1':
    touch_source=Path(os.environ['ELDEN_TOUCH_INSPECT_SOURCE'])
    touch_board=Path(os.environ['ELDEN_TOUCH_BOARD_HEADER'])
    touch_resources=Path(os.environ['ELDEN_TOUCH_RESOURCE_HEADER'])
    command,directory=compiler(touch_source,OUT/'EldenTouchInspect.o')
    command+=['-I',str(touch_board.parent),'-I',str(touch_resources.parent)]
    run(command,cwd=directory,stderr=subprocess.STDOUT)
    touch_objects.append(OUT/'EldenTouchInspect.o')
    touch_sources += [touch_source,touch_board,touch_resources]
bluetooth_objects=[];bluetooth_sources=[]
if os.environ.get('ELDEN_BLUETOOTH_BUILD')=='1':
    bt=Path(os.environ['ELDEN_BLUETOOTH_SOURCE']);reference=Path(os.environ['ELDEN_BLUETOOTH_PAGANI'])
    board=Path(os.environ['ELDEN_BLUETOOTH_BOARD']);resources=Path(os.environ['ELDEN_BLUETOOTH_RESOURCES'])
    inputs=Path(os.environ['ELDEN_BLUETOOTH_FIRMWARE']);manifest=json.loads((inputs/'manifest.json').read_text())
    selected={'qca/brhperifw20.tlv':'elden_wcn_patch','qca/brhperinv20.bin':'elden_wcn_nvm','qca/tmel_peach_20.elf':'elden_wcn_tmel'}
    hci_build=os.environ.get('ELDEN_BLUETOOTH_HCI_BUILD')=='1'
    wcn_build=os.environ.get('ELDEN_WCN_POWER_BUILD')=='1'
    if hci_build:
        selected.update({'qca/brhbtfw20.tlv':'elden_wcn_bt_patch',
                         'qca/lenovo/tb322fc/brhbtnv20.bin':'elden_wcn_bt_nvm'})
    fw_header=source/'EldenWcnFirmware.h';header='// Generated exact board inputs; never commit.\n';record={}
    fw_data=source/'EldenWcnFirmware.c';firmware_definitions='#include <stdint.h>\n'
    if wcn_build:header+='#ifdef __cplusplus\nextern "C" {\n#endif\n'
    for path,name in selected.items():
        item=next(x for x in manifest['files'] if x['file']==path);data=(inputs/path).read_bytes()
        require(len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256'],'Exact WCN7861 asset '+path)
        record[path]={'bytes':len(data),'sha256':item['sha256']}
        definition='const uint8_t '+name+'[]={\n'+'\n'.join(','.join('0x%02x'%v for v in data[i:i+32])+',' for i in range(0,len(data),32))+'\n};\n'
        if wcn_build:
            header+='extern const uint8_t '+name+'['+str(len(data))+'];\n'
            firmware_definitions+=definition
        else:header+='static '+definition
    if wcn_build:
        header+='#ifdef __cplusplus\n}\n#endif\n'
        fw_data.write_text(firmware_definitions)
    fw_header.write_text(header)
    includes=['-I',str(bt),'-I',str(reference),'-I',str(board.parent),'-I',str(resources.parent),'-I',str(source)]
    bt_compile=[('EldenWcnPeripheral',bt/'Wcn7861Peripheral.c'),('EldenWcnStartup',bt/'Wcn7861Startup.c'),
                ('EldenBtH4',reference/'BtH4.c'),('EldenBtGeni',reference/'BtGeni.c')]
    if hci_build:
        bt_compile += [('EldenWcnHci',bt/'Wcn7861Hci.c'),('EldenBtTlv',reference/'BtTlv.c')]
    uart_build=os.environ.get('ELDEN_BLUETOOTH_UART_BUILD')=='1'
    if uart_build:bt_compile += [('EldenWcnUartPlatform',bt/'UartPlatform.c')]
    if wcn_build:
        wcn_reference=Path(os.environ['ELDEN_WCN_POWER_REFERENCE'])
        includes += ['-I',str(wcn_reference),'-I',str(Path(os.environ['ELDEN_REBOOT_RESOURCE_HEADER']).parent)]
        rpm_source=source/'Sm8750Rpmh.wcn.c'
        text=(wcn_reference/'Sm8750Rpmh.c').read_text()
        original='    rc = transact(&ctx, cmds, n, wait_mask);\n    Sm8750RpmhDrv2Release();'
        require(text.count(original)==1,'Exact canonical RPMh fence location')
        text=text.replace(original,'    rc = transact(&ctx, cmds, n, wait_mask);\n    if (!rc || result->step < Sm8750RpmhStepTrigger) Sm8750RpmhDrv2Release();')
        rpm_source.write_text(text)
        bt_compile += [('EldenWcnPower',bt/'WcnPower.c'),('EldenWcnFirmware',fw_data),('EldenWcnRpmh',rpm_source),
                       ('EldenWcnQmp',wcn_reference/'Sm8750AossQmp.c'),('EldenWcnSpmi',wcn_reference/'BtPmic.c')]
    native_bt=os.environ.get('ELDEN_BT_NATIVE_BUILD')=='1'
    require(not wcn_build or (hci_build and uart_build),'WCN power build requires its real HCI and UART adapters')
    require(not native_bt or wcn_build,'Native Bluetooth host requires the real WCN power/stop provider')
    if native_bt:
        bridge=Path(os.environ['ELDEN_BT_BRIDGE_SOURCE'])
        includes+=['-I',str(bridge)]
        import importlib.util
        completion_script=bt/'BridgeCompletion.py'
        spec=importlib.util.spec_from_file_location('elden_bt_completion',completion_script)
        completion=importlib.util.module_from_spec(spec);spec.loader.exec_module(completion)
        checked_bridge=source/'EldenBtBridge.checked.c'
        checked_bridge.write_bytes(completion.prepare((bridge/'BtBridge.c').read_bytes()))
        bluetooth_sources += [completion_script,bridge/'BtBridge.c']
        bt_compile += [('EldenBtBridge',checked_bridge),('EldenBtStandIn',bridge/'BtStandIn.c'),
                       ('EldenBtIbs',reference/'BtIbs.c')]
        bluetooth_sources += [bridge/'BtBridge.h',bridge/'BtStandIn.h',reference/'BtIbs.h']
    for name,path in bt_compile:
        obj=OUT/(name+'.o');cmd=list(owned_c_command);cmd[cmd.index(str(process))]=str(path);cmd[cmd.index('-o')+1]=str(obj);cmd+=includes
        run(cmd,cwd=owned_c_directory,stderr=subprocess.STDOUT);bluetooth_objects.append(obj);bluetooth_sources.append(path)
    inspect=ROOT/'scripts/driver-patches/EldenBluetoothInspect.cpp';cmd,directory=compiler(inspect,OUT/'EldenBluetoothInspect.o');cmd+=includes
    if hci_build:cmd+=['-DELDEN_BLUETOOTH_HCI_BUILD=1']
    run(cmd,cwd=directory,stderr=subprocess.STDOUT);bluetooth_objects.append(OUT/'EldenBluetoothInspect.o')
    bluetooth_sources += [inspect,bt/'Wcn7861Peripheral.h',bt/'Wcn7861Startup.h',reference/'BtH4.h',reference/'BtGeni.h',reference/'BtTlv.h',board,resources,fw_header]
    if hci_build:
        bluetooth_sources += [bt/'Wcn7861Hci.h',bt/'Wcn7861Wire.h',reference/'BtUtil.h']
    if uart_build:
        backend=bt/'UartBackend.cpp';cmd,directory=compiler(backend,OUT/'EldenWcnUartBackend.o');cmd+=includes
        run(cmd,cwd=directory,stderr=subprocess.STDOUT);bluetooth_objects.append(OUT/'EldenWcnUartBackend.o')
        bluetooth_sources += [backend,bt/'UartBackend.h',bt/'UartPlatform.h']
    if wcn_build:
        power=bt/'WcnPowerNative.cpp';cmd,directory=compiler(power,OUT/'EldenWcnPowerNative.o');cmd+=includes
        if native_bt:cmd+=['-DELDEN_BT_NATIVE_BUILD=1']
        run(cmd,cwd=directory,stderr=subprocess.STDOUT);bluetooth_objects.append(OUT/'EldenWcnPowerNative.o')
        bluetooth_sources += [power,bt/'WcnPower.h',bt/'RunControl.h',wcn_reference/'Sm8750Rpmh.h',
                              wcn_reference/'Sm8750AossQmp.h',wcn_reference/'BtPmic.h',
                              wcn_reference/'PaganiBtBoard.h']
    if native_bt:
        native_host_source=bt/'NativeHost.cpp';cmd,directory=compiler(native_host_source,OUT/'EldenBtNativeHost.o');cmd+=includes
        run(cmd,cwd=directory,stderr=subprocess.STDOUT);bluetooth_objects.append(OUT/'EldenBtNativeHost.o')
        bluetooth_sources += [native_host_source,bt/'NativeHost.h']
    (OUT/'bluetooth-inputs.json').write_text(json.dumps({'firmware':record,'header_sha256':sha(fw_header),'hardware_tested':False},indent=2)+'\n')
power_objects=[];power_sources=[]
if os.environ.get('ELDEN_POWER_INSPECT_BUILD')=='1':
    power_source=Path(os.environ['ELDEN_POWER_INSPECT_SOURCE'])
    power_resources=Path(os.environ['ELDEN_POWER_RESOURCE_HEADER'])
    power_reference=Path(os.environ['ELDEN_POWER_SMEM_REFERENCE'])
    power_src=source/'power-inspect';power_src.mkdir(exist_ok=True)
    smem_header=power_src/'QcomSmem.h';smem_header.write_bytes((power_reference/'QcomSmem.h').read_bytes())
    smem=power_src/'QcomSmem.c';text=(power_reference/'QcomSmem.c').read_text()
    # This profile intentionally compiles only the original read functions.
    begin=text.index('void qcom_smem_write32(');end=text.index('static uint32_t rd(',begin)
    text=text[:begin]+text[end:];text=text[:text.index('int qcom_smem_alloc(')]
    require('qcom_smem_write32(' not in text and 'qcom_smem_alloc(' not in text,'Read-only SMEM subset')
    smem.write_text(text)
    (power_src/'LICENSE').write_bytes((power_reference/'LICENSE').read_bytes())
    ccmd=[str(smem) if v==str(process) else str(OUT/'QcomSmem.inspect.o') if v==str(OUT/'EldenUserWaitProbe.o') else v for v in owned_c_command]
    ccmd+=['-I',str(power_src)];run(ccmd,cwd=owned_c_directory,stderr=subprocess.STDOUT)
    command,directory=compiler(power_source,OUT/'EldenPowerInspect.o')
    command+=['-I',str(power_src),'-I',str(power_resources.parent)]
    run(command,cwd=directory,stderr=subprocess.STDOUT)
    power_objects=[OUT/'EldenPowerInspect.o',OUT/'QcomSmem.inspect.o']
    power_sources=[power_source,power_resources,smem,smem_header]
run(['xcrun','ar','rcs',archive,late,OUT/'original-bindings.o',OUT/'EldenDisplayGlue.o',OUT/'EldenUserWaitProbe.o',key_late,OUT/'EldenKeyEventsGlue.o',*hid_objects,*touch_objects,*bluetooth_objects,*power_objects,*wifi_objects])
sources=[source/'NativeLegacyDisplay.cpp',source/'NativeLegacyRequest.h',glue,process,key_source,key_glue]
sources+=hid_sources
sources+=touch_sources
sources+=power_sources
sources+=bluetooth_sources
sources+=wifi_sources
report=dict(source_revision=revision,keyevents_source_revision=key_revision,source_sha256={str(p.relative_to(ROOT)):sha(p) for p in sources},
    generated_abi_sha256=sha(OUT/'OriginalLegacyFramebufferABI.h'),archive_sha256=sha(archive),
    compiled_object_sha256=sha(obj),late_object_sha256=sha(late),original_candidate_sha256=sha(candidate),
    original_audit_sha256=sha(OUT/'original-abi.json'),bindings={n:hex(v) for n,v in bindings.items()},
    virtual_entries=358,owned_entries=owned,inherited_native_entries=native,
    original_instructions_modified=False,hardware_display_tested=False,
    physical_touch_linked=os.environ.get('ELDEN_TOUCH_RUNTIME_BUILD')=='1',
    other_board_peripherals_linked=os.environ.get('ELDEN_TOUCH_RUNTIME_BUILD')=='1',
    selected_owned_features=[feature for feature in owned_features if os.environ.get(feature)=='1'])
(OUT/'build-report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
