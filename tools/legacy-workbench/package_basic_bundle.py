"""Package combined default-off drivers with explicit, separate diagnostic entries."""
import argparse,gzip,hashlib,json,re,struct,subprocess,sys,tomllib
from pathlib import Path
from avb_guard import check_image
from audit_boot_candidate import Fat,PORT
from adt_gestures import apply_gestures
sys.path.insert(0,str(PORT/'scripts'));import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel,adt_walk
ROOT=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def wsl(p):return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
PROFILES={
 'off':('iPadOS basic drivers - OFF',{}),
 'firmware':('ADSP firmware - read check',{'elden-modem-fw-check':1}),
 'survey':('ADSP state - survey',{'elden-adsp':1}),
 'power':('Power + two-port UCSI - TEST',{'elden-adsp':2,'elden-pmic':1,'elden-ucsi-query':1}),
 'power-init':('Power + UCSI v1 init - TEST',{'elden-adsp':2,'elden-pmic':1,'elden-ucsi-query':1,'elden-ucsi-init':1}),
 'power-notify':('Power + OEM notification - TEST',{'elden-adsp':2,'elden-pmic':1,'elden-ucsi-query':1,'elden-ucsi-init':1,'elden-pmic-notify':1}),
 'power-usb500':('Power + USB 500mA - TEST',{'elden-adsp':2,'elden-pmic':1,'elden-ucsi-query':1,'elden-ucsi-init':1,'elden-pmic-notify':1,'elden-usb-icl':1}),
 'power-votes':('Power + charge limits - TEST',{'elden-adsp':2,'elden-pmic':1,'elden-ucsi-query':1,'elden-ucsi-init':1,'elden-pmic-notify':1,'elden-usb-icl':1,'elden-power-votes':1}),
 'power-control':('Y700 charge modes - TEST',{'elden-adsp':2,'elden-pmic':1,'elden-ucsi-query':1,'elden-ucsi-init':1,'elden-pmic-notify':1,'elden-usb-icl':1,'elden-power-votes':1,'elden-charge-control':1}),
 'cpu-survey':('CPU performance levels - SURVEY',{'elden-adsp':2,'elden-pmic':1,'elden-ucsi-query':1,'elden-ucsi-init':1,'elden-pmic-notify':1,'elden-usb-icl':1,'elden-power-votes':1,'elden-charge-control':1,'elden-cpu-survey':1}),
 'wifi-fw':('Wi-Fi OEM firmware - read check',{'elden-wifi-fw-check':1}),
 'digitizer':('Touch HID + power - TEST',{'elden-adsp':2,'elden-pmic':1,'elden-ucsi-query':1,'elden-touch-hid':1}),
 'identify':('Touch chip ID + power - TEST',{'elden-adsp':2,'elden-pmic':1,'elden-ucsi-query':1,'elden-touch':1}),
 'touch-fw':('Touch SRAM + power - TEST',{'elden-adsp':2,'elden-pmic':1,'elden-ucsi-query':1,'elden-touch':2}),
 'touch':('Real touch + power - TEST',{'elden-adsp':2,'elden-pmic':1,'elden-ucsi-query':1,'elden-touch':3,'elden-touch-hid':1})}
for name,mode,title in [('backlight-inspect',1,'AW99706 identity - CHECK'),('backlight-roundtrip',2,'AW99706 brightness - RESTORE TEST'),('backlight-service',3,'iPadOS brightness service - TEST')]:
 PROFILES[name]=(title,dict(PROFILES['power-control'][1],**{'elden-backlight':mode}))
PROFILES['power-battery']=('iPadOS battery telemetry - TEST',dict(PROFILES['power-control'][1],**{
 'elden-battery':1,'elden-battery-profile':1,'elden-battery-current':-1}))
for name,mode,title in [('adc-inspect',1,'USB thermometers - SDAM INSPECT'),('adc-read',2,'USB temperatures - READ TEST'),('adc-monitor',3,'USB temperatures - MONITOR')]:
 PROFILES[name]=(title,dict(PROFILES['power-control'][1],**{'elden-adc':mode}))
for name,mode,title in [('thermal-inspect',1,'USB thermal GPIOs - INSPECT'),('thermal-guard',2,'USB thermal protection - TEST')]:
 PROFILES[name]=(title,dict(PROFILES['power-control'][1],**{'elden-thermal':mode}))
PROFILES['pd-survey']=('USB PD contracts - READ',dict(PROFILES['power-control'][1],**{'elden-ucsi-pdos':1}))
for name,mode,title in [('touch-identify-powered',1,'Touch controller identity - TEST'),('touch-firmware-powered',2,'Touch firmware readiness - TEST'),('touch-powered',3,'iPadOS physical touch - TEST')]:
 PROFILES[name]=(title,dict(PROFILES['power-control'][1],**{'elden-touch':mode,'elden-touch-hid':int(mode==3)}))
PROFILES['touch-dma-survey']=('Touch engine and DMA routes - SURVEY',dict(PROFILES['power-control'][1],**{'elden-touch':4}))
PROFILES['touch-dma-identify']=('Touch SE-DMA chip identity - TEST',dict(PROFILES['power-control'][1],**{'elden-touch':5}))
PROFILES['touch-dma-firmware']=('Touch SE-DMA stock SRAM - TEST',dict(PROFILES['power-control'][1],**{'elden-touch':6}))
PROFILES['touch-dma-input']=('iPadOS physical touch - DMA TEST',dict(PROFILES['power-control'][1],**{'elden-touch':7,'elden-touch-hid':1}))
PROFILES['touch-dma-combined']=('iPadOS touch + keyboard - DMA TEST',dict(PROFILES['power-control'][1],**{'elden-touch':7,'elden-touch-hid':1,'sheng-touch-hid':1}))
PROFILES['touch-service-replay']=('iPadOS touch service - UI TEST',dict(PROFILES['touch-dma-combined'][1],**{'elden-ui-replay':1}))
PROFILES['touch-service-split-replay']=('iPadOS touch screen service - UI TEST',dict(PROFILES['touch-dma-input'][1],**{'elden-ui-replay':1}))
PROFILES['touch-service-helper-replay']=('iPadOS touch service match - UI TEST',dict(PROFILES['touch-service-split-replay'][1],**{'elden-touch-helper':1}))
PROFILES['touch-event-id-replay']=('iPadOS touch event identity - UI TEST',dict(PROFILES['touch-service-helper-replay'][1],**{'elden-touch-id-base':1}))
PROFILES['power-deadline']=('Power + autonomous fastboot - TEST',dict(PROFILES['power-control'][1],**{'elden-autoreturn':150}))
PROFILES['pd-pulse']=('PD pulse and USB return - TEST',dict(PROFILES['power-control'][1],**{'elden-autoreturn':300,'elden-ucsi-pdos':1,'elden-thermal':3,'elden-pd-bootstrap':1}))
PROFILES['pd-bootstrap']=('PD pulse and input restore - TEST',dict(PROFILES['pd-pulse'][1],**{'elden-pd-bootstrap':2}))
PROFILES['backlight-delayed']=('AW99706 delayed fault observation - CHECK',dict(PROFILES['backlight-inspect'][1],**{'elden-backlight-delay-ms':5000,'elden-autoreturn':150}))
PROFILES['backlight-settled-trial']=('AW99706 qualified dim and restore - TEST',dict(PROFILES['power-control'][1],**{'elden-backlight':4,'elden-autoreturn':150}))
PROFILES['backlight-power-trial']=('Backlight power and stop - TEST',dict(PROFILES['power-control'][1],**{'elden-backlight':5,'elden-autoreturn':450}))
PROFILES['pd-backlight-power']=('PD and backlight power - TEST',dict(PROFILES['pd-bootstrap'][1],**{'elden-backlight':5,'elden-autoreturn':450}))
PROFILES['backlight-ui']=('iPadOS system brightness - TEST',dict(PROFILES['power-control'][1],**{'elden-backlight':6,'elden-autoreturn':300}))
PROFILES['pd-power-ui']=('iPadOS brightness and battery - TEST',dict(PROFILES['pd-bootstrap'][1],**{'elden-backlight':6,'elden-autoreturn':450,'elden-battery':1,'elden-battery-profile':1,'elden-battery-current':-1}))
PROFILES['power-path']=('PMIH0104 input path - READ',dict(PROFILES['power-control'][1],**{'elden-regs':1,'elden-autoreturn':150}))
PROFILES['pd-path-ui']=('PD input path and iPadOS - READ',dict(PROFILES['pd-power-ui'][1],**{'elden-regs':1}))
PROFILES['pd-ulog-ui']=('PD firmware logs and iPadOS - READ',dict(PROFILES['pd-power-ui'][1],**{'elden-ulog':1}))
PROFILES['pd-ulog-pages']=('PD firmware log pages - READ',dict(PROFILES['pd-power-ui'][1],**{'elden-ulog':2}))
PROFILES['pd-gpio-ui']=('PD GPIO and suspend - READ',dict(PROFILES['pd-power-ui'][1],**{'elden-regs':2}))
PROFILES['ucsi-events']=('UCSI notifications and power - TEST',dict(PROFILES['pd-ulog-pages'][1],**{'elden-ucsi-init':2,'elden-autoreturn':300,'elden-thermal':2}))
PROFILES['ucsi-events'][1].pop('elden-pd-bootstrap')
PROFILES['pd-ucsi-events']=('PD with complete UCSI - TEST',dict(PROFILES['pd-ulog-pages'][1],**{'elden-ucsi-init':2}))
PROFILES['dc-noop']=('DC 500mA same-value write - TEST',dict(PROFILES['pd-ucsi-events'][1],**{'elden-dc-noop':1}))
PROFILES['dc-noop'][1].pop('elden-ulog')
PROFILES['pd-policy-trace']=('PD firmware policy trace - TEST',dict(PROFILES['pd-ucsi-events'][1],**{'elden-ulog':3}))
PROFILES['pd-handoff']=('PD handoff with typed USB limit - TEST',dict(PROFILES['pd-policy-trace'][1],**{'elden-usb-icl':2}))
PROFILES['pd-handoff-quiet']=('PD handoff verification - TEST',dict(PROFILES['pd-handoff'][1]))
PROFILES['pd-handoff-quiet'][1].pop('elden-ulog')
PROFILES['dc-noop-quiet']=('DC 500mA verified write - TEST',dict(PROFILES['pd-handoff-quiet'][1],**{'elden-dc-noop':1}))
PROFILES['dc-native-trial']=('DC native 450/500mA restore - TEST',dict(PROFILES['pd-handoff-quiet'][1],**{'elden-dc-noop':2}))
PROFILES['icon-cpu']=('iPadOS touch + icon CPU path - TEST',dict(PROFILES['touch-event-id-replay'][1],**{
 'elden-usb-icl':2,'elden-ucsi-init':2,'elden-ucsi-pdos':1,'elden-thermal':2,'elden-autoreturn':900,'elden-icon-cpu':1}))
def main():
 if sys.flags.optimize:raise SystemExit('Assertions must remain enabled')
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--default',choices=PROFILES,default='off');p.add_argument('--focused',action='store_true');p.add_argument('--version',type=int,default=2);p.add_argument('--gestures',action='store_true');p.add_argument('--ipad-pro',action='store_true');p.add_argument('--ui-dispatch',action='store_true');p.add_argument('--vdog-trial',action='store_true');p.add_argument('--cpu-guard-cpus',type=int,choices=[2,8]);p.add_argument('--cpu-hold-seconds',type=int,default=0);a=p.parse_args();assert 2<=a.version<=99
 if a.cpu_hold_seconds:assert a.version in (87,88,89,90,91,92) and a.cpu_guard_cpus==8 and 60<=a.cpu_hold_seconds<=600 and a.ui_dispatch
 if a.cpu_guard_cpus:
  assert a.default=='icon-cpu' and a.version in (81,82,83,84,85,86,87,88,89,90,91,92) and a.focused and not a.vdog_trial and (not a.ui_dispatch or a.cpu_hold_seconds)
  PROFILES['icon-cpu']=(f"CPU{a.cpu_guard_cpus} protected startup - TEST",dict(PROFILES['icon-cpu'][1],**{'vdog':2,'cpus':a.cpu_guard_cpus,'sheng-smp':1,'armcpu-self-ipi':1}))
 if a.cpu_hold_seconds:PROFILES['icon-cpu'][1]['smp-hold']=a.cpu_hold_seconds
 if a.vdog_trial:
  assert a.default=='icon-cpu' and a.version in (78,79,80) and a.focused and not a.ui_dispatch
  PROFILES['icon-cpu']=("Gunyah watchdog to fastboot - TEST",dict(PROFILES['icon-cpu'][1],vdog=1))
 if a.ui_dispatch:
  assert a.default=='icon-cpu' and a.version>=74 and a.focused
  PROFILES['icon-cpu'][1]['elden-ui']=1
 if a.version<29:
  assert a.default!='touch-dma-firmware','DMA firmware runtime requires v29 or later reviewed source'
  PROFILES.pop('touch-dma-firmware',None)
 if a.version<30:
  assert a.default!='touch-dma-input','Continuous DMA touch requires v30 or later'
  PROFILES.pop('touch-dma-input',None)
 if a.version<32:
  assert a.default!='touch-dma-combined','Combined touch routing requires v32 or later'
  PROFILES.pop('touch-dma-combined',None)
 if a.version<33:
  assert a.default not in ('touch-service-replay','touch-service-split-replay'),'UI service replay requires v33 or later'
  PROFILES.pop('touch-service-replay',None)
  PROFILES.pop('touch-service-split-replay',None)
 if a.version<36:
  assert a.default!='touch-service-helper-replay','Secondary matching helper requires v36 or later'
  PROFILES.pop('touch-service-helper-replay',None)
 if a.version<37:
  assert a.default!='touch-event-id-replay','Event identity comparison requires v37 or later'
  PROFILES.pop('touch-event-id-replay',None)
 if a.version<39:
  assert a.default!='power-deadline','Autonomous return requires v39 or later'
  PROFILES.pop('power-deadline',None)
 if a.version<41:
  assert a.default not in ('pd-pulse','pd-bootstrap'),'Native PD bootstrap requires v41 or later'
  PROFILES.pop('pd-pulse',None);PROFILES.pop('pd-bootstrap',None)
 if a.version>=43:
  # Bounded hardware observation/roundtrip tests return even without a host ACK.
  for name in ('backlight-inspect','backlight-roundtrip'):
   PROFILES[name][1]['elden-autoreturn']=150
 if a.version<44:
  assert a.default!='backlight-delayed','Delayed observation requires backlight9/v44 or later'
  PROFILES.pop('backlight-delayed',None)
 if a.version<45:
  assert a.default!='backlight-settled-trial','Settled trial requires backlight10/v45 or later'
  PROFILES.pop('backlight-settled-trial',None)
 if a.version<46:
  assert a.default not in ('backlight-power-trial','pd-backlight-power'),'Power trials require backlight11/v46 or later'
  PROFILES.pop('backlight-power-trial',None);PROFILES.pop('pd-backlight-power',None)
 if a.version<47:
  assert a.default not in ('backlight-ui','pd-power-ui'),'UI integration requires backlight13/v47 or later'
  PROFILES.pop('backlight-ui',None);PROFILES.pop('pd-power-ui',None)
 else:PROFILES['power-battery'][1]['elden-autoreturn']=300
 if a.version<49:
  assert a.default not in ('power-path','pd-path-ui'),'Register path observer requires power24/v49 or later'
  PROFILES.pop('power-path',None);PROFILES.pop('pd-path-ui',None)
 if a.version<51:
  assert a.default!='pd-ulog-ui','ULog requires power26/v51 or later'
  PROFILES.pop('pd-ulog-ui',None)
 if a.version<52:
  assert a.default!='pd-ulog-pages','ULog paging requires power27/v52 or later'
  PROFILES.pop('pd-ulog-pages',None)
 if a.version<53:
  assert a.default!='pd-gpio-ui','GPIO observation requires power28/v53'
  PROFILES.pop('pd-gpio-ui',None)
 if a.version<55:
  assert a.default not in ('ucsi-events','pd-ucsi-events'),'Full UCSI notifications require power30/v55'
  PROFILES.pop('ucsi-events',None);PROFILES.pop('pd-ucsi-events',None)
 if a.version<56:
  assert a.default!='dc-noop','Fixed DC same-value write requires reviewed power32/v56'
  PROFILES.pop('dc-noop',None)
 if a.default=='dc-noop':assert a.focused,'The one-write experiment requires its explicit focused test menu'
 else:PROFILES.pop('dc-noop',None)
 if a.default=='pd-policy-trace':assert a.version>=57 and a.focused,'Policy trace requires reviewed power33/v57 focused profile'
 else:PROFILES.pop('pd-policy-trace',None)
 if a.default=='pd-handoff':assert a.version>=58 and a.focused,'Typed SDP handoff requires power34/v58 focused profile'
 else:PROFILES.pop('pd-handoff',None)
 if a.default=='pd-handoff-quiet':assert a.version>=58 and a.focused,'Quiet typed SDP test requires power34/v58'
 else:PROFILES.pop('pd-handoff-quiet',None)
 if a.default=='dc-noop-quiet':assert a.version>=59 and a.focused,'Quiet DC test requires typed-SDP and PDO notifications'
 else:PROFILES.pop('dc-noop-quiet',None)
 if a.default=='dc-native-trial':assert a.version>=61 and a.focused,'Native DC requires v61 and an explicit focused trial'
 else:PROFILES.pop('dc-native-trial',None)
 if a.default=='icon-cpu':assert a.version>=62 and a.focused,'Icon CPU test requires reviewed v62 integration'
 else:PROFILES.pop('icon-cpu',None)
 assert not a.ipad_pro or a.gestures,'iPad Pro identity requires the no-Home-button configuration'
 base=ROOT/'edl_work/hid-20261107/boot-signed.img';assert check_image(base,'boot_a').sha256=='7282b272395a3c7a9a099bad3ab1f10b20a6951320129a96373f3410a868a363'
 compiled=ROOT/('edl_work/basic-bundle-20261108/v'+str(a.version)+'/final');out=compiled/(((a.default+'-focused') if a.focused else a.default)+('-gestures' if a.gestures else '')+('-ipad-pro' if a.ipad_pro else ''));out.mkdir(exist_ok=True)
 if a.cpu_guard_cpus:out=out.with_name(out.name+'-cpu'+str(a.cpu_guard_cpus));out.mkdir(exist_ok=True)
 b=base.read_bytes();raw=gzip.decompress(b[4096:4096+struct.unpack_from('<I',b,8)[0]]);f=Fat(raw[0x400080:])
 gesture_adt,gesture_report=(apply_gestures(f.read('/EFI/UniXNU/Payloads/kernel.macho.adt'),'ipad-pro-11' if a.ipad_pro else 'inherit') if a.gestures else (b'',None))
 if a.ipad_pro:
  identity=json.loads((ROOT/'research/ipad-gestures-20261109/identity-source.json').read_text())
  assert identity['identifier']=='iPad8,1' and any(b['boardconfig']=='J317AP' for b in identity['boards'])
  gesture_report['identity_source_sha256']=identity['sha256']
 if a.gestures:
  cache=json.loads((ROOT/'edl_work/ipad-gestures-20261108/seed-cache-audit.json').read_text())
  assert cache['read_only'] and not cache['seed_has_cached_identity'] and not cache['mobilegestalt_files']
  assert sha(f.read('/EFI/UniXNU/Payloads/data.packed'))==cache['packed_sha256']
  gesture_report['cache_check']='verified-empty-seed'
  gesture_report['data_packed_sha256']=cache['packed_sha256']
 kernel=(compiled/'kernel.macho').read_bytes();assert sha(kernel)==json.loads((compiled/'derivative.json').read_text())['derivative_sha256']
 parse_kernel(kernel);symbols=maker.symbols(kernel)
 if a.cpu_guard_cpus:
  from cpu_guard_qualification import verify as verify_cpu_guard
  cpu_guard_proof=verify_cpu_guard(ROOT,a.version,kernel)
  if a.cpu_hold_seconds:
   from cpu_guard_qualification import verify_hold
   cpu_hold_proof=verify_hold(ROOT,a.version,kernel)
  elif a.cpu_guard_cpus==8:
   from cpu_guard_qualification import verify_two_core
   cpu_two_core_proof=verify_two_core(ROOT,kernel)
 if a.vdog_trial:
  from vdog_qualification import verify as verify_vdog
  vdog_proof=verify_vdog(ROOT,a.version,kernel)
 if b'ELDEN_TOUCH_DMA attach=' not in kernel:
  assert a.default not in ('touch-dma-identify','touch-dma-firmware'),'Touch DMA owner was not linked'
  PROFILES.pop('touch-dma-identify',None)
  PROFILES.pop('touch-dma-firmware',None)
  PROFILES.pop('touch-dma-input',None)
  PROFILES.pop('touch-dma-combined',None)
 if b'ELDEN_TOUCH_DMA SMMU result=' not in kernel:
  assert a.default!='touch-dma-survey','Touch DMA survey was not linked'
  PROFILES.pop('touch-dma-survey',None)
 if '_elden_backlight_runtime_start' not in symbols:
  assert not a.default.startswith('backlight-'),'Backlight was not linked into this build'
  for n in list(PROFILES):
   if n.startswith('backlight-'):del PROFILES[n]
 if a.default=='backlight-delayed':
  assert b'ELDEN_BACKLIGHT first-read delay-ms=' in kernel and b'ELDEN_BACKLIGHT protection-flags=' in kernel
 if a.default=='backlight-settled-trial':
  assert b'ELDEN_BACKLIGHT trial-qualified=' in kernel and '_elden_aw99706_check_health' in symbols
 if a.default in ('backlight-power-trial','pd-backlight-power'):
  assert '_elden_backlight_request_stop' in symbols and b'ELDEN_BACKLIGHT power-finished=' in kernel
  assert b'ELDEN_BACKLIGHT cfg7=' in kernel and b'ELDEN_BACKLIGHT RPC stop=' in kernel
 if a.default in ('backlight-ui','pd-power-ui','pd-path-ui','pd-ulog-ui','pd-ulog-pages','pd-gpio-ui','ucsi-events','pd-ucsi-events','dc-noop','pd-policy-trace','pd-handoff','pd-handoff-quiet','dc-noop-quiet','dc-native-trial'):
  assert b'ELDEN_BACKLIGHT registering class; qualified mode=' in kernel
  assert b'ELDEN_BACKLIGHT refused original-class conformance' in kernel and '_elden_backlight_request_stop' in symbols
 if '_elden_battery_gate_update' not in symbols:
  assert a.default!='power-battery','Battery publication readiness gate was not linked'
  PROFILES.pop('power-battery',None)
 if '_elden_adc_runtime_start' not in symbols:
  assert not a.default.startswith('adc-'),'Native ADC runtime was not linked'
  for n in list(PROFILES):
   if n.startswith('adc-'):del PROFILES[n]
 if '_elden_thermal_runtime_start' not in symbols or b'ELDEN_THERMAL RPC stop=' not in kernel:
  assert not a.default.startswith('thermal-'),'Thermal runtime/stop hook was not linked'
  for n in list(PROFILES):
   if n.startswith('thermal-'):del PROFILES[n]
 if '_elden_ucsi_pdo_control' not in symbols or '_elden_pd_contract' not in symbols:
  assert a.default!='pd-survey','PD query/contract decoder was not linked'
  PROFILES.pop('pd-survey',None)
 for n in ['_elden_wifi_fw_check_start','_elden_wifi_file_read','_elden_touch_runtime_start','_elden_battery_runtime_update']:
  assert n in symbols,n
 original=f.read('/EFI/UniXNU/unixnu.toml').decode();cfg=tomllib.loads(original);args=cfg['entry'][0]['args']
 entry=original.split('[[entry]]',2)[1]
 assert entry.count('id = "xnu"')==1 and entry.count(args)==1
 keys={'elden-adsp','elden-pmic','elden-pmic-notify','elden-usb-icl','elden-power-votes','elden-charge-control','elden-cpu-survey','elden-backlight','elden-adc','elden-thermal','elden-ucsi-query','elden-ucsi-init','elden-ucsi-pdos','elden-modem-fw-check','elden-wifi-fw-check','elden-touch','elden-touch-hid','elden-touch-helper','elden-touch-id-base','sheng-touch-hid','elden-ui-replay','elden-battery','elden-battery-profile','elden-battery-current','elden-autoreturn'}
 keys.add('elden-pd-bootstrap')
 keys.add('elden-backlight-delay-ms')
 keys.add('elden-regs')
 keys.add('elden-ulog')
 keys.add('elden-dc-noop')
 keys.add('elden-icon-cpu')
 assert not any(w.split('=',1)[0] in keys for w in args.split())
 def section(name,id):
  title,flags=PROFILES[name];s=entry.replace('id = "xnu"','id = '+json.dumps(id),1)
  s=re.sub(r'^title = .*$', 'title = '+json.dumps(title),s,count=1,flags=re.M)
  # Keep the fixed kernel boot-args buffer. The integrated profile omits
  # optional user wait-stack sampling and a redundant kprintf override;
  # elden-gop-console already enables kprintf, and panic/USB routing remains.
  words=args.split()
  if name=='icon-cpu':
   omitted={'-enable_kprintf_spam','armcpu-diagnostics=1'};assert omitted<=set(words)
   if a.cpu_hold_seconds:
    omitted|={'elden-user-wait=1','native-ufs-diagnostics=1','native-ufs-lun=0'};assert omitted<=set(words)
   elif (a.vdog_trial or a.cpu_guard_cpus) and a.version>=80:
    omitted|={'elden-frame-preview=1','elden-user-wait=1'};assert omitted<=set(words)
   words=[w for w in words if w not in omitted]
  if name in ('pd-power-ui','pd-path-ui','pd-ulog-ui','pd-ulog-pages','pd-gpio-ui','ucsi-events','pd-ucsi-events','dc-noop','pd-policy-trace','pd-handoff','pd-handoff-quiet','dc-noop-quiet','dc-native-trial'):
   omitted={'-enable_kprintf_spam','elden-user-wait=1'}
   if name in ('pd-handoff-quiet','dc-noop-quiet','dc-native-trial'):omitted.add('elden-frame-preview=1')
   assert omitted<=set(words)
   words=[w for w in words if w not in omitted]
  new=' '.join(words)+' '+ ' '.join(k+'='+str(v) for k,v in flags.items())
  assert len(new.encode())<1024,(name,len(new.encode()))
  return '[[entry]]'+s.replace(args,new,1)
 # Keep legacy diagnostics/actions. The selected first entry retains the xnu ID.
 menu=original.split('[[entry]]',1)[0]+section(a.default,'xnu')+'[[entry]]'+original.split('[[entry]]',2)[2]
 if a.focused:
  menu=original.split('[[entry]]',1)[0]+section(a.default,'xnu')
  if a.default!='off':menu+=section('off','basic-off')
  blocks=original.split('[[entry]]')[1:]
  actions=[block for block in blocks if tomllib.loads('[[entry]]'+block)['entry'][0]['type']=='action']
  assert actions
  menu+='[[entry]]'+actions[0]
 else:
  for name in PROFILES:
   if name!=a.default:menu+=section(name,'basic-'+name)
 session=sha(kernel+menu.encode()+gesture_adt)[:16];menu=re.sub(r'elden-debug-session=[0-9a-f]{16}','elden-debug-session='+session,menu)
 new=tomllib.loads(menu);assert new['boot']['default']=='xnu' and new['boot']['timeout']==8
 assert len({e['id'] for e in new['entry']})==len(new['entry'])
 (out/'unixnu.toml').write_text(menu,encoding='utf-8',newline='\n');(out/'kernel.macho').write_bytes(kernel)
 payload=out/'payload.img';payload.write_bytes(raw[0x400080:])
 for source,dest in [('kernel.macho','Payloads/kernel.macho'),('unixnu.toml','unixnu.toml')]:
  subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','mcopy','-o','-i',wsl(payload),wsl(out/source),'::/EFI/UniXNU/'+dest],check=True)
 if a.gestures:
  (out/'kernel.macho.adt').write_bytes(gesture_adt)
  subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','mcopy','-o','-i',wsl(payload),wsl(out/'kernel.macho.adt'),'::/EFI/UniXNU/Payloads/kernel.macho.adt'],check=True)
 icon_proof=None
 if a.default=='icon-cpu':
  from icon_cpu_qualification import verify
  icon_proof=verify(ROOT,a.version,out,kernel)
  tc=ROOT/('edl_work/icon-dependency-20261111/kernel.macho.trustcache' if a.version>=65 else 'edl_work/icon-cpu-20261111/kernel.macho.trustcache')
  if a.version>=65 and (tc.parent/f'v{a.version}').is_dir():tc=tc.parent/f'v{a.version}'/tc.name
  subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','mcopy','-o','-i',wsl(payload),wsl(tc),'::/EFI/UniXNU/Payloads/kernel.macho.trustcache'],check=True)
 fat=Fat(payload.read_bytes());same={}
 for path in ['Payloads/kernel.macho.adt','Payloads/kernel.macho.trustcache','Payloads/data.packed','EldenPayloadLoader.efi']:
  if a.gestures and path=='Payloads/kernel.macho.adt':assert fat.read('/EFI/UniXNU/'+path)==gesture_adt;continue
  if icon_proof and path=='Payloads/kernel.macho.trustcache':
   assert fat.read('/EFI/UniXNU/'+path)==tc.read_bytes();continue
  data=fat.read('/EFI/UniXNU/'+path);assert data==f.read('/EFI/UniXNU/'+path);same[path]=sha(data)
 assert fat.read('/EFI/UniXNU/Payloads/kernel.macho')==kernel and adt_walk(fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt'))['window']==0x180000000
 compressed=gzip.compress(raw[:0x400080]+payload.read_bytes(),mtime=0);h=bytearray(b[:4096]);struct.pack_into('<I',h,8,len(compressed))
 image=bytes(h)+compressed;image+=bytes(-len(image)%4096);(out/'boot.img').write_bytes(image)
 packet=dict(default=a.default,session=session,kernel_sha256=sha(kernel),unchanged=same,
  profiles={n:v for n,(_,v) in PROFILES.items()},menu_entries=len(new['entry']),focused=a.focused,hardware_tested=False,
  charger_set_enabled=PROFILES[a.default][1].get('elden-usb-icl',0) in (1,2),
  power_publication_enabled=PROFILES[a.default][1].get('elden-battery',0)==1)
 if a.vdog_trial:
  packet['vdog_trial']=vdog_proof
  if a.version>=80:packet['vdog_quiet']=True;packet['preview_enabled']=False
 if a.cpu_guard_cpus:
  packet['cpu_guard']=cpu_guard_proof;packet['cpu_guard_cpus']=a.cpu_guard_cpus
  if a.cpu_hold_seconds:
   packet['cpu_hold_qualification']=cpu_hold_proof;packet['cpu_hold_seconds']=a.cpu_hold_seconds;packet['root_lun_uses_verified_default']=0
  elif a.cpu_guard_cpus==8:packet['cpu_two_core_qualification']=cpu_two_core_proof
  packet['vdog_quiet']=not bool(a.cpu_hold_seconds);packet['preview_enabled']=bool(a.cpu_hold_seconds)
 if a.ui_dispatch:
  from ui_dispatch_qualification import verify as verify_ui_dispatch
  packet['ui_dispatch']=verify_ui_dispatch(ROOT,a.version,kernel)
 if a.default in ('pd-power-ui','pd-path-ui','pd-ulog-ui','pd-ulog-pages','pd-gpio-ui','ucsi-events','pd-ucsi-events','dc-noop','pd-policy-trace','pd-handoff','pd-handoff-quiet','dc-noop-quiet','dc-native-trial'):packet['omitted_diagnostic_args']=['-enable_kprintf_spam','elden-user-wait=1']
 if b'ELDEN_BAR_BEGIN session=' in kernel:
  path=compiled.parent/'preview-codegen.json';proof=json.loads(path.read_text())
  assert proof['accepted'] and not proof['fp_simd'] and proof['crop']==[1666,0,238,96]
  frozen=ROOT/f'research/basic-bundle-20261108/source-compiled-v{a.version}/tree/scripts/driver-patches/EldenDisplayGlue.cpp'
  assert sha(frozen.read_bytes())==proof['source_sha256']
  packet['native_statusbar_preview']=True;packet['preview_codegen_sha256']=sha(path.read_bytes())
 if a.default in ('pd-handoff-quiet','dc-noop-quiet','dc-native-trial'):
  packet['omitted_diagnostic_args'].append('elden-frame-preview=1');packet['preview_enabled']=False
 if gesture_report:packet['gesture_identity']=gesture_report
 if icon_proof:
  packet['icon_cpu']=icon_proof;packet['omitted_diagnostic_args']=['-enable_kprintf_spam','armcpu-diagnostics=1']
  if a.cpu_hold_seconds:packet['omitted_diagnostic_args']+=['elden-user-wait=1','native-ufs-diagnostics=1','native-ufs-lun=0']
  elif (a.vdog_trial or a.cpu_guard_cpus) and a.version>=80:packet['omitted_diagnostic_args']+=['elden-frame-preview=1','elden-user-wait=1']
 if PROFILES[a.default][1].get('elden-autoreturn'):
  assert '_elden_recovery_deadline_remaining' in symbols and b'ELDEN_RECOVERY DEADLINE_FIRED' in kernel
  assert b'boot=%s configured=' in kernel
  packet['local_autoreturn_seconds']=PROFILES[a.default][1]['elden-autoreturn']
 if packet['charger_set_enabled']:
  assert json.loads((compiled/'usb-budget.json').read_text())['usb_config_max_ma']==500
 if PROFILES[a.default][1].get('elden-usb-icl')==2:
  from usb_handoff_qualification import verify
  verify(ROOT,a.version,out)
  assert '_elden_usb_icl_sdp_only' in symbols and b'ELDEN_USB_HANDOFF deferred' in kernel
  packet['usb_sdp_only']=True;packet['usb_handoff_qualification']=sha((out/'usb-handoff-qualification.json').read_bytes())
 if a.default in ('backlight-ui','pd-power-ui','pd-path-ui','pd-ulog-ui','pd-ulog-pages','pd-gpio-ui','ucsi-events','pd-ucsi-events','dc-noop','pd-policy-trace','pd-handoff','pd-handoff-quiet','dc-noop-quiet','dc-native-trial'):
  path=out/'backlight-ui-qualification.json';review=json.loads(path.read_text())
  assert review['accepted'] and review['lifecycle_races']>=1000 and not review['service_ui_tested']
  frozen=ROOT/f'research/basic-bundle-20261108/source-compiled-v{a.version}'
  for name,digest in review['sources'].items():assert sha((frozen/name).read_bytes())==digest,name
  evidence=ROOT/review['stop_report'];assert sha(evidence.read_bytes())==review['stop_report_sha256']
  stop=json.loads(evidence.read_text());assert stop['dropped']==0 and stop['reset_after_sync']
  assert 'ELDEN_BACKLIGHT stop=1 worker-done=1 held=0 restored=1' in stop['backlight_stop']
  packet['backlight_ui_qualification']=sha(path.read_bytes())
 if PROFILES[a.default][1].get('elden-ucsi-init')==2:
  path=out/'ucsi-events-qualification.json';proof=json.loads(path.read_text())
  expected_control='0xda650005' if a.version>=59 and PROFILES[a.default][1].get('elden-ucsi-pdos')==1 else '0xda050005'
  assert proof['accepted'] and proof['checks']>=421 and proof['notification_control']==expected_control
  if expected_control=='0xda650005':
   from pdo_notification_qualification import verify
   verify(ROOT,a.version,out)
  frozen=ROOT/f'research/basic-bundle-20261108/source-compiled-v{a.version}/power'
  for name,digest in proof['sources'].items():assert sha((frozen/name).read_bytes())==digest,name
  assert '_elden_ucsi_enable_events' in symbols and b'ELDEN_UCSI_EVENTS ready=' in kernel
  packet['ucsi_events_qualification']=sha(path.read_bytes())
 if PROFILES[a.default][1].get('elden-dc-noop')==1:
  from dc_noop_qualification import verify
  proof=verify(ROOT,a.version,out)
  for n in ['_elden_dc_noop_next','_elden_dc_noop_request_stop','_elden_thermal_cold_now']:assert n in symbols,n
  assert b'ELDEN_DC_NOOP VERIFIED' in kernel and b'ELDEN_DC_NOOP stop=' in kernel
  packet['dc_same_value_write']=True
  packet['dc_noop_qualification']=sha((out/'dc-noop-qualification.json').read_bytes())
  packet['dc_noop_write']={'bus':0,'sid':7,'address':'0x2a52','raw':10,'ma':500,'max_attempts':1}
 if PROFILES[a.default][1].get('elden-dc-noop')==2:
  from dc_native_qualification import verify
  proof=verify(ROOT,a.version,out)
  for n in ['_elden_dc_trial_step','_elden_dc_native_request_stop','_elden_dc_native_write_fence']:assert n in symbols,n
  assert b'ELDEN_DC_NATIVE write raw=' in kernel and b'real SPMI completion' in kernel
  packet['dc_native_trial']=True;packet['dc_native_target']=proof['target']
  packet['dc_native_qualification']=sha((out/'dc-native-qualification.json').read_bytes())
 if PROFILES[a.default][1].get('elden-ulog') in (1,2,3):
  path=out/'ulog-qualification.json';proof=json.loads(path.read_text())
  assert proof['accepted'] and proof['control_set']==(PROFILES[a.default][1]['elden-ulog']==3)
  assert proof.get('mode',1)==PROFILES[a.default][1]['elden-ulog'] and proof['boot_request_cap']==(12 if proof.get('mode',1)>=2 else 2)
  if proof.get('mode',1)>=2:assert proof['paging_checks']>=326
  if proof.get('mode',1)==3:
   from ulog_filter_qualification import verify
   verify(ROOT,a.version,out)
   for n in ('_elden_log_filter_next','_elden_ulog_filter_request_stop','_elden_ulog_enable_policy_trace'):assert n in symbols
   packet['diagnostic_log_filter']=True
  assert proof['fixed_reply_bytes']==8204 and proof['global_tx_message_bytes']==560
  assert proof['protocol_checks']==24643 and proof['transport_checks']==29142
  frozen=ROOT/f'research/basic-bundle-20261108/source-compiled-v{a.version}/power'
  for name,digest in proof['sources'].items():assert sha((frozen/name).read_bytes())==digest,name
  for name in ('_elden_ulog_next','_elden_ulog_receive','_elden_ulog_crc'):assert name in symbols
  packet['ulog_qualification']=sha(path.read_bytes())
 if PROFILES[a.default][1].get('elden-regs') in (1,2):
  path=out/'power-path-qualification.json';proof=json.loads(path.read_text());assert proof['accepted'] and proof['checks']>=932
  frozen=ROOT/f'research/basic-bundle-20261108/source-compiled-v{a.version}/power'
  for name,digest in proof['sources'].items():assert sha((frozen/name).read_bytes())==digest,name
  if PROFILES[a.default][1].get('elden-regs')==2:
   assert proof['mode']==2 and proof['gpio_checks']>=501 and proof['register_count']==19
   assert b'ELDEN_GPIO_REG index=' in kernel
  assert '_elden_power_reg_next' in symbols and '_elden_power_reg_receive' in symbols and b'ELDEN_POWER_PATH cycle=' in kernel
  packet['power_path_qualification']=sha(path.read_bytes())
 if a.default in ('backlight-power-trial','pd-backlight-power'):
  path=out/'backlight-power-qualification.json';review=json.loads(path.read_text())
  assert review['accepted'] and review['lifecycle_races']>=1000 and review['chip_checks']>=394
  frozen=ROOT/f'research/basic-bundle-20261108/source-compiled-v{a.version}'
  for name,digest in review['sources'].items():assert sha((frozen/name).read_bytes())==digest,name
  evidence=ROOT/review['cfg7_live_report'];raw=json.loads(evidence.read_text());assert sha(evidence.read_bytes())==review['cfg7_live_report_sha256']
  assert raw['cfg7_i2c_confirmed'] and raw['autonomous_stop_confirmed'] and raw['zero_chip_writes']
  packet['backlight_power_qualification']=sha(path.read_bytes())
 if a.default=='backlight-settled-trial':
  from assess_backlight_observation import assess
  proof=assess(ROOT/'edl_work/live-backlight-delay-v44-20261110','d7a5f9383bfbf860')
  assert proof['initial_delay_ms']==['5000'] and proof['all_later_flags_clear'] and proof['later_brightness_configuration_unchanged']
  assert proof['dropped_max']==0 and not proof['discontinuities'] and proof['target_deadline_fired'] and proof['reset_after_sync']
  assert any('id=07 mode=65 high=06 low=3d config=f8 open=3c short=00' in line for line in proof['backlight_lines'])
  assert 'ELDEN_BACKLIGHT protection-flags=08' in proof['backlight_lines']
  path=out/'backlight-trial-observation.json';path.write_text(json.dumps(proof,indent=2)+'\n')
  packet['backlight_trial_observation']=sha(path.read_bytes())
  review=json.loads((ROOT/'research/backlight-settled-20261110/source-review.json').read_text())
  assert review['accepted'] and review['settled_checks']>=231 and not review['production_brightness_service_admitted']
  frozen=ROOT/f'research/basic-bundle-20261108/source-compiled-v{a.version}/backlight'
  for name,digest in review['sources'].items():assert sha((frozen/name).read_bytes())==digest,name
  path=out/'backlight-trial-review.json';path.write_text(json.dumps(review,indent=2)+'\n');packet['backlight_trial_review']=sha(path.read_bytes())
 if PROFILES[a.default][1].get('elden-charge-control')==1:
  evidence={}
  for name in ['charge-disable-oem-20261109','charge-window-resume-oem-20261109']:
   path=ROOT/'edl_work'/name/'result.json';report=json.loads(path.read_text());assert report['restored'] and report['returncode']==0
   evidence[name]=sha(path.read_bytes())
  packet['oem_charge_contract']=evidence
 if any('elden-battery=1' in e.get('args','').split() for e in new['entry']):
  path=out/'battery-publication-contract.json'
  data=path.read_bytes() if path.exists() else (ROOT/'edl_work/battery-publication-20261109/contract.json').read_bytes()
  contract=json.loads(data)
  assert contract['accepted'] and contract['profile']==1 and contract['current_multiplier']==-1
  assert contract['qualified_publication_cycles']>=50 and contract['first_publication_cycle']==4
  assert contract['qualified_net_charging_cycles']==0
  frozen=ROOT/('research/basic-bundle-20261108/source-compiled-v'+str(a.version)+'/power')
  for name,digest in contract['source_sha256'].items():assert sha((frozen/name).read_bytes())==digest,name
  if not path.exists():path.write_bytes(data)
  packet['battery_publication_contract']=sha(data)
 if any(any(w.startswith('elden-adc=') and w!='elden-adc=0' for w in e.get('args','').split()) for e in new['entry']):
  path=out/'adc-qualification.json'
  data=path.read_bytes() if path.exists() else (ROOT/'edl_work/adc-bringup-20261109/qualification.json').read_bytes()
  proof=json.loads(data);assert proof['accepted'] and proof['trace_events']==112 and proof['overrun']==0
  frozen=ROOT/('research/basic-bundle-20261108/source-compiled-v'+str(a.version)+'/power')
  for name,digest in proof['sources'].items():assert sha((frozen/name).read_bytes())==digest,name
  if not path.exists():path.write_bytes(data)
  packet['adc_qualification']=sha(data)
  packet['adc_mode']=PROFILES[a.default][1].get('elden-adc',0)
  packet['adc_gpio_writes']=False
 if any(any(w.startswith('elden-thermal=') and w!='elden-thermal=0' for w in e.get('args','').split()) for e in new['entry']):
  path=out/'thermal-qualification.json'
  data=path.read_bytes() if path.exists() else (ROOT/'edl_work/thermal-protection-20261109/qualification.json').read_bytes()
  proof=json.loads(data);assert proof['accepted'] and proof['protection_checks']>=60
  frozen=ROOT/('research/basic-bundle-20261108/source-compiled-v'+str(a.version)+'/power')
  for name,digest in proof['sources'].items():assert sha((frozen/name).read_bytes())==digest,name
  assert b'ELDEN_THERMAL RPC stop=' in kernel and '_elden_battery_temperature_get' in symbols
  if not path.exists():path.write_bytes(data)
  packet['thermal_qualification']=sha(data)
  packet['thermal_gpio_control']=PROFILES[a.default][1].get('elden-thermal',0)>=2
 if any('elden-ucsi-pdos=1' in e.get('args','').split() for e in new['entry']):
  path=out/'pd-qualification.json';data=path.read_bytes() if path.exists() else (ROOT/'edl_work/charge-routing-20261109/pd-qualification.json').read_bytes()
  proof=json.loads(data);assert proof['accepted'] and proof['pd_checks']>=796
  frozen=ROOT/('research/basic-bundle-20261108/source-compiled-v'+str(a.version)+'/power')
  for name,digest in proof['sources'].items():assert sha((frozen/name).read_bytes())==digest,name
  if not path.exists():path.write_bytes(data)
  packet['pd_qualification']=sha(data)
 if PROFILES[a.default][1].get('elden-pd-bootstrap'):
  path=out/'pd-bootstrap-qualification.json';data=path.read_bytes();proof=json.loads(data)
  assert proof['accepted'] and proof['bootstrap_checks']>=250 and proof['icl_checks']>=65 and proof['lifecycle_races']>=1000
  frozen=ROOT/('research/basic-bundle-20261108/source-compiled-v'+str(a.version)+'/power')
  for name,digest in proof['sources'].items():assert sha((frozen/name).read_bytes())==digest,name
  for name in ['_elden_pd_boot_step','_elden_pd_boot_qualified','_elden_thermal_pulse_request','_elden_thermal_pulse_state','_elden_pd_bootstrap_request_stop']:
   assert name in symbols,name
  assert b'ELDEN_PD_BOOT stop=' in kernel and b'ELDEN_PD_PULSE high' in kernel
  packet['pd_bootstrap_mode']=PROFILES[a.default][1]['elden-pd-bootstrap'];packet['pd_bootstrap_qualification']=sha(data)
 if any('elden-touch=4' in e.get('args','').split() for e in new['entry']):
  path=out/'touch-survey-qualification.json'
  data=path.read_bytes() if path.exists() else (ROOT/'research/touch-dma-survey-20261109/qualification.json').read_bytes()
  proof=json.loads(data);assert proof['accepted'] and proof['no_spi_or_dma_commands']
  frozen=ROOT/('research/basic-bundle-20261108/source-compiled-v'+str(a.version)+'/tree')
  for name,digest in proof['sources'].items():assert sha((frozen/name).read_bytes())==digest,name
  if not path.exists():path.write_bytes(data)
  packet['touch_survey_qualification']=sha(data)
 if any(set(e.get('args','').split())&{'elden-touch=5','elden-touch=6','elden-touch=7'} for e in new['entry']):
  path=out/'touch-dma-qualification.json';data=path.read_bytes()
  proof=json.loads(data);assert proof['accepted'] and proof['scope'] in ('touch-single-sid-dma-identify','touch-single-sid-dma-input')
  if any(set(e.get('args','').split())&{'elden-touch=6','elden-touch=7'} for e in new['entry']):
   assert proof.get('stock_sram_upload_reviewed') is True
   packet['touch_sram_upload']=True
  if any('elden-touch=7' in e.get('args','').split() for e in new['entry']):
   assert proof.get('continuous_input_reviewed') is True
   assert '_elden_touch_request_stop' in symbols and b'ELDEN_TOUCH RPC stop=' in kernel
   packet['physical_touch_hid_enabled']=True
  if any('elden-ui-replay=1' in e.get('args','').split() for e in new['entry']):
   assert '_elden_hid_request_settings_tap' in symbols and b'ELDEN_TOUCH_HID service-publish=' in kernel
   assert b'ELDEN_CPU pass=' in kernel and b'ELDEN_UI_REPLAY settings' in kernel
   packet['software_settings_replay_enabled']=True
  if any('elden-touch-helper=1' in e.get('args','').split() for e in new['entry']):
   assert b'elden-touch-helper' in kernel,'Secondary matching helper not linked'
   packet['secondary_keyboard_collection_helper']=True
  if any('elden-touch-id-base=1' in e.get('args','').split() for e in new['entry']):
   assert b'elden-touch-id-base' in kernel and b'ELDEN_HID_EVENT n=' in kernel
   packet['one_based_hid_contact_identifiers']=True
  assert '_elden_touch_route_attach' in symbols and '_elden_touch_dma_attach' in symbols
  frozen=ROOT/('research/basic-bundle-20261108/source-compiled-v'+str(a.version)+'/tree')
  for name,digest in proof['sources'].items():assert sha((frozen/name).read_bytes())==digest,name
  packet['touch_dma_qualification']=sha(data)
 (out/'packet.json').write_text(json.dumps(packet,indent=2)+'\n');print(json.dumps(dict(default=a.default,session=session,menu_entries=len(new['entry']),kernel_sha256=sha(kernel)),indent=2))
if __name__=='__main__':main()
