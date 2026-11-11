"""Independently verify combined sources, original ABI/assets, menu and stock AVB."""
import argparse,gzip,hashlib,json,struct,sys,tomllib
from pathlib import Path
from avb_guard import check_image,metadata
from audit_boot_candidate import Fat,PORT
from adt_gestures import audit_gestures
sys.path.insert(0,str(PORT/'scripts'));import make_entry_probe_kernel as maker
from darwinboot_preflight import parse_kernel,adt_walk
ROOT=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def unpack(path):
 b=path.read_bytes();return gzip.decompress(b[4096:4096+struct.unpack_from('<I',b,8)[0]])
def main():
 if sys.flags.optimize:raise SystemExit('Assertions must remain enabled')
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--variant',default='off');p.add_argument('--version',type=int,default=2);options=p.parse_args();assert 2<=options.version<=99
 out=ROOT/('edl_work/basic-bundle-20261108/v'+str(options.version)+'/final');kernel=(out/'kernel.macho').read_bytes();parse_kernel(kernel)
 names=maker.symbols(kernel);plan=json.loads((out/'kernel.json').read_text());old=bytearray(kernel)
 offset=maker.locate(kernel,0xfffffff004aaecfc)[1];assert struct.unpack_from('<I',kernel,offset)[0]==0x17ffffd4
 struct.pack_into('<I',old,offset,0xaa1503e0)
 for module,digest in plan['original_executable_sha256'].items():
  e=next(e for e in plan['extents'] if e['module']==module and e['segment']=='__TEXT_EXEC');a=maker.locate(kernel,e['va'])[1]
  assert sha(old[a:a+e['size']])==digest,module
 hid=json.loads((out/'hid-abi.json').read_text());assert hid['original_slots']==207 and hid['original_instance_size']==208
 for name,va in hid['bindings'].items():assert names[name]==va,name
 patch=json.loads((out/'touch-board-patch.json').read_text())
 assert patch['original_event_driver_slots']==220 and patch['original_event_slots']==37 and patch['private_vtable_runtime_identity_gate']
 fw=(ROOT/'research/twrp-device/recovery/root/vendor/firmware/novatek_ts_csot_3k_fw.bin').read_bytes()
 assert sha(fw)=='413216d0e8738078414fa681262439ab6d1c181f02974215e0394f102fcc08dc' and kernel.count(fw)==1
 for n in ['_elden_wifi_fw_check_start','_elden_wifi_file_open','_elden_wifi_file_read','_elden_touch_runtime_start',
           '_elden_pmic_client_start','_elden_battery_runtime_update','_native_ufs_part_find','_native_ufs_part_read']:
  assert n in names,n
 checks=json.loads((ROOT/'edl_work/wifi-modem-files-20261108/result.json').read_text());assert checks['accepted']
 for e in checks['files']:assert e['sha256'].encode() in kernel,e['path']
 source=ROOT/('research/basic-bundle-20261108/source-compiled-v'+str(options.version))
 assert 'sizeof(names) / sizeof(names[0])' in (source/'power/AdspFirmware.c').read_text()
 before=unpack(ROOT/'edl_work/hid-20261107/boot-signed.img');base=Fat(before[0x400080:]);d=out/options.variant
 a=check_image(d/'boot-signed.img','boot_a');assert a.index==1738713600 and a.location==0 and a.bytes==100663296
 assert metadata(d/'boot-signed.img')['props']==metadata(ROOT/'signing/phase5_resign/boot_signed.img')['props']
 raw=unpack(d/'boot-signed.img');assert raw[:0x400080]==before[:0x400080];fat=Fat(raw[0x400080:])
 assert fat.read('/EFI/UniXNU/Payloads/kernel.macho')==kernel
 packet=json.loads((d/'packet.json').read_text());cfg=tomllib.loads(fat.read('/EFI/UniXNU/unixnu.toml').decode())
 if packet.get('native_statusbar_preview'):
  path=out.parent/'preview-codegen.json';assert sha(path.read_bytes())==packet['preview_codegen_sha256']
  proof=json.loads(path.read_text());assert proof['accepted'] and proof['crop']==[1666,0,238,96] and not proof['fp_simd']
  assert sha((source/'tree/scripts/driver-patches/EldenDisplayGlue.cpp').read_bytes())==proof['source_sha256']
  if options.version in (88,89,90,91,92):
   assert proof['post_input_frames']==4 and proof['per_boot_frames']==10
   address=names['_elden_preview_after_input'];offset=maker.locate(kernel,address)[1]
   assert struct.unpack_from('<I',kernel,offset+8)[0]==0x52800089 # mov w9,#4
   assert '__atomic_store_n(&previewAfterInput,4,' in (source/'tree/scripts/driver-patches/EldenDisplayGlue.cpp').read_text()
   archive=out/'display-archives/libNativeLegacyDisplay.late.a'
   assert sha(archive.read_bytes())==proof['archive_sha256']
  assert b'ELDEN_BAR_BEGIN session=' in kernel and b'ELDEN_BAR_END id=' in kernel
 if packet['default'] in ('pd-power-ui','pd-path-ui','pd-ulog-ui','pd-ulog-pages','pd-gpio-ui','ucsi-events','pd-ucsi-events','dc-noop','pd-policy-trace','pd-handoff','pd-handoff-quiet','dc-noop-quiet','dc-native-trial'):
  omitted={'-enable_kprintf_spam','elden-user-wait=1'}
  if packet['default'] in ('pd-handoff-quiet','dc-noop-quiet','dc-native-trial'):omitted.add('elden-frame-preview=1');assert packet['preview_enabled'] is False
  assert set(packet['omitted_diagnostic_args'])==omitted
  original_args=tomllib.loads(base.read('/EFI/UniXNU/unixnu.toml').decode())['entry'][0]['args'].split()
  required=[w for w in original_args if w not in omitted and not w.startswith('elden-debug-session=')]
  actual=cfg['entry'][0]['args'].split()
  assert all(w in actual for w in required) and not (set(actual)&omitted)
  assert {'elden-gop-console=1','serial=3','elden-key-events=1','elden-key-state=1'}<=set(actual)
 if packet.get('gesture_identity'):
  adt=fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt')
  audit_gestures(base.read('/EFI/UniXNU/Payloads/kernel.macho.adt'),adt,packet['gesture_identity'].get('profile','inherit'))
  assert sha(adt)==packet['gesture_identity']['adt_sha256']
  assert packet['gesture_identity']['cache_check']=='verified-empty-seed'
  assert sha(fat.read('/EFI/UniXNU/Payloads/data.packed'))==packet['gesture_identity']['data_packed_sha256']
 if packet['default']=='icon-cpu':
  if packet.get('cpu_guard'):
   from cpu_guard_qualification import verify as verify_cpu_guard
   assert packet['cpu_guard']==verify_cpu_guard(ROOT,options.version,kernel)
   assert packet['cpu_guard_cpus'] in (2,8)
   if packet.get('cpu_hold_seconds'):
    from cpu_guard_qualification import verify_hold
    assert packet['cpu_hold_qualification']==verify_hold(ROOT,options.version,kernel)
    assert 60<=packet['cpu_hold_seconds']<=600 and packet['cpu_guard_cpus']==8
    assert 'smp-hold='+str(packet['cpu_hold_seconds']) in cfg['entry'][0]['args'].split() and packet.get('ui_dispatch')
   elif packet['cpu_guard_cpus']==8:
    from cpu_guard_qualification import verify_two_core
    assert packet['cpu_two_core_qualification']==verify_two_core(ROOT,kernel)
   assert {'vdog=2','cpus='+str(packet['cpu_guard_cpus']),'sheng-smp=1','armcpu-self-ipi=1'}<=set(cfg['entry'][0]['args'].split())
   assert all(not any(w.startswith(('vdog=','sheng-smp=','cpus=')) for w in e.get('args','').split()) for e in cfg['entry'][1:])
  if packet.get('vdog_trial'):
   from vdog_qualification import verify as verify_vdog
   assert packet['vdog_trial']==verify_vdog(ROOT,options.version,kernel)
   assert 'vdog=1' in cfg['entry'][0]['args'].split() and 'elden-ui=1' not in cfg['entry'][0]['args'].split()
   assert all(not any(x.startswith('vdog=') for x in e.get('args','').split()) for e in cfg['entry'][1:])
   assert not any('sheng-smp' in e.get('args','') for e in cfg['entry'])
  if packet.get('ui_dispatch'):
   from ui_dispatch_qualification import verify as verify_ui_dispatch
   assert packet['ui_dispatch']==verify_ui_dispatch(ROOT,options.version,kernel)
   assert 'elden-ui=1' in cfg['entry'][0]['args'].split()
   assert all('elden-ui=1' not in e.get('args','').split() for e in cfg['entry'][1:])
  from icon_cpu_qualification import verify
  assert packet['icon_cpu']==verify(ROOT,options.version,d,kernel)
  from icon_cpu_trust import append
  icon_dir=ROOT/('edl_work/icon-dependency-20261111' if options.version>=65 else 'edl_work/icon-cpu-20261111')
  if options.version>=65 and (icon_dir/f'v{options.version}').is_dir():icon_dir=icon_dir/f'v{options.version}'
  lib=(icon_dir/'EldenIconCpuMode.dylib').read_bytes()
  expected_tc,_=append(base.read('/EFI/UniXNU/Payloads/kernel.macho.trustcache'),lib)
  if options.version>=65:expected_tc,_=append(expected_tc,(icon_dir/'iconservicesagent').read_bytes())
  assert fat.read('/EFI/UniXNU/Payloads/kernel.macho.trustcache')==expected_tc
  omitted={'-enable_kprintf_spam','armcpu-diagnostics=1'}
  if packet.get('cpu_hold_seconds'):
   omitted|={'elden-user-wait=1','native-ufs-diagnostics=1','native-ufs-lun=0'}
   assert packet['root_lun_uses_verified_default']==0 and packet['preview_enabled']
  elif packet.get('vdog_quiet'):
   assert options.version>=80 and (packet.get('vdog_trial') or packet.get('cpu_guard')) and packet['preview_enabled'] is False
   omitted|={'elden-frame-preview=1','elden-user-wait=1'}
  assert set(packet['omitted_diagnostic_args'])==omitted
  original_args=tomllib.loads(base.read('/EFI/UniXNU/unixnu.toml').decode())['entry'][0]['args'].split()
  actual=cfg['entry'][0]['args'].split()
  assert all(w in actual for w in original_args if w not in omitted and not w.startswith('elden-debug-session='))
  assert not set(actual)&omitted and 'elden-icon-cpu=1' in actual
 if packet['charger_set_enabled']:
  budget=json.loads((out/'usb-budget.json').read_text());assert budget['usb_config_max_ma']==500 and budget['exact_descriptor_delta']
  core=(ROOT/'research/qcom-usb-core-20261107/usb/Dwc3.c').read_text()
  old='9, dtype, 67, 0, 2, 1, 0, 0x80, 50,';assert core.count(old)==1
  assert (source/'usb/Dwc3.c').read_text()==core.replace(old,old.replace('0x80, 50,','0x80, 250,'))
  assert '_elden_usb_power_budget_ua' in names and '_elden_usb_icl_next' in names
 if packet['profiles'][packet['default']].get('elden-usb-icl')==2:
  from usb_handoff_qualification import verify
  verify(ROOT,options.version,d)
  assert packet['usb_sdp_only'] and packet['default'] in ('pd-handoff','pd-handoff-quiet','dc-noop-quiet','dc-native-trial','icon-cpu') and packet['focused']
  assert packet['usb_handoff_qualification']==sha((d/'usb-handoff-qualification.json').read_bytes())
  assert '_elden_usb_icl_sdp_only' in names and b'ELDEN_USB_HANDOFF deferred' in kernel
 if packet.get('oem_charge_contract'):
  for name,digest in packet['oem_charge_contract'].items():assert sha((ROOT/'edl_work'/name/'result.json').read_bytes())==digest
  for name in ['_elden_charge_policy_next','_elden_charge_policy_observe','_elden_charge_policy_request','_elden_charge_policy_public_state']:assert name in names
 if packet['profiles'][packet['default']].get('elden-cpu-survey')==1:
  assert '_elden_cpu_perf_survey_start' in names
  proof=json.loads((ROOT/'research/cpu-power-20261109/source.json').read_text())
  assert sha((source/'power/EldenScmiPerf.h').read_bytes())==proof['guarded_sha256']
  assert 'elden_cpu_perf_query(protocol,message)' in (source/'power/CpuPerfSurvey.cpp').read_text()
  assert 'scmi::levelSet' not in (source/'power/CpuPerfSurvey.cpp').read_text()
 assert cfg['boot']['default']=='xnu' and cfg['boot']['timeout']==8 and len(cfg['entry'])==packet['menu_entries']
 for e in cfg['entry']:
  args=e.get('args','').split();flags={}
  for word in args:
   if '=' in word:
    key,value=word.split('=',1);assert key not in flags;flags[key]=value
  assert len(e.get('args','').encode())<1024
  if e['type']!='xnu':continue
  if int(flags.get('elden-autoreturn','0')):
   assert 90<=int(flags['elden-autoreturn'])<=900
   assert '_elden_recovery_deadline_remaining' in names and b'ELDEN_RECOVERY DEADLINE_FIRED' in kernel
   assert b'boot=%s configured=' in kernel
   assert (source/'usb/EldenRebootOwnership.h').is_file()
  if flags.get('elden-dc-noop','0')=='1':
   from dc_noop_qualification import verify
   verify(ROOT,options.version,d)
   assert e['id']=='xnu' and packet['default'] in ('dc-noop','dc-noop-quiet') and packet['dc_same_value_write']
   assert packet['dc_noop_write']=={'bus':0,'sid':7,'address':'0x2a52','raw':10,'ma':500,'max_attempts':1}
   assert sha((d/'dc-noop-qualification.json').read_bytes())==packet['dc_noop_qualification']
   assert flags['elden-dc-noop']=='1' and flags['elden-ucsi-init']=='2' and flags['elden-thermal']=='3'
   assert flags['elden-pd-bootstrap']=='2' and flags['elden-power-votes']=='1'
   assert flags.get('elden-ulog','0')=='0' and flags.get('elden-regs','0')=='0'
   assert flags['elden-usb-icl']==('2' if packet['default']=='dc-noop-quiet' else '1') and flags['elden-autoreturn']=='450'
   for n in ['_elden_dc_noop_next','_elden_dc_noop_request_stop','_elden_thermal_cold_now']:assert n in names
   assert b'ELDEN_DC_NOOP VERIFIED' in kernel and b'ELDEN_DC_NOOP stop=' in kernel
  if flags.get('elden-dc-noop','0')=='2':
   from dc_native_qualification import verify
   proof=verify(ROOT,options.version,d)
   assert e['id']=='xnu' and packet['default']=='dc-native-trial' and packet['focused'] and packet['dc_native_trial']
   assert packet['dc_native_target']==proof['target']
   assert sha((d/'dc-native-qualification.json').read_bytes())==packet['dc_native_qualification']
   assert flags['elden-ucsi-init']=='2' and flags['elden-thermal']=='3' and flags['elden-usb-icl']=='2'
   assert flags['elden-pd-bootstrap']=='2' and flags['elden-power-votes']=='1' and flags['elden-autoreturn']=='450'
   assert flags.get('elden-ulog','0')=='0' and flags.get('elden-regs','0')=='0'
   for n in ['_elden_dc_trial_step','_elden_dc_native_request_stop','_elden_dc_native_write_fence']:assert n in names
   assert b'ELDEN_DC_NATIVE write raw=' in kernel and b'real SPMI completion' in kernel
  assert flags.get('elden-dc-noop','0') in ('0','1','2')
  if int(flags.get('elden-icon-cpu','0')):
   assert packet['default']=='icon-cpu' and e['id']=='xnu' and flags['elden-icon-cpu']=='1'
   assert flags['elden-autoreturn']=='900' and flags['elden-thermal']=='2'
   assert flags.get('elden-dc-noop','0')=='0' and flags.get('elden-pd-bootstrap','0')=='0'
  if int(flags.get('elden-ulog','0')):
   assert flags['elden-ulog'] in ('1','2','3') and flags.get('elden-power-votes')=='1' and flags.get('elden-regs','0')=='0'
   path=d/'ulog-qualification.json';proof=json.loads(path.read_text())
   assert sha(path.read_bytes())==packet['ulog_qualification'] and proof['accepted']
   assert proof['fixed_reply_bytes']==8204 and proof['global_tx_message_bytes']==560 and proof['control_set']==(flags['elden-ulog']=='3')
   assert proof.get('mode',1)==int(flags['elden-ulog']) and proof['boot_request_cap']==(12 if flags['elden-ulog'] in ('2','3') else 2)
   if flags['elden-ulog']=='3':
    from ulog_filter_qualification import verify
    verify(ROOT,options.version,d)
    assert packet['default'] in ('pd-policy-trace','pd-handoff') and packet['focused'] and packet['diagnostic_log_filter']
    assert flags['elden-ucsi-init']=='2' and flags['elden-pd-bootstrap']=='2' and flags['elden-thermal']=='3'
    assert flags.get('elden-dc-noop','0')=='0' and flags['elden-autoreturn']=='450'
    for n in ('_elden_log_filter_next','_elden_ulog_filter_request_stop','_elden_ulog_enable_policy_trace'):assert n in names
   for name,digest in proof['sources'].items():assert sha((source/'power'/name).read_bytes())==digest,name
   for name in ('_elden_ulog_next','_elden_ulog_receive','_elden_ulog_crc'):assert name in names
  if flags.get('elden-ucsi-init')=='2':
   path=d/'ucsi-events-qualification.json';proof=json.loads(path.read_text())
   expected_control='0xda650005' if options.version>=59 and flags.get('elden-ucsi-pdos')=='1' else '0xda050005'
   assert proof['accepted'] and proof['checks']>=421 and proof['notification_control']==expected_control
   if expected_control=='0xda650005':
    from pdo_notification_qualification import verify
    verify(ROOT,options.version,d)
   assert sha(path.read_bytes())==packet['ucsi_events_qualification']
   for name,digest in proof['sources'].items():assert sha((source/'power'/name).read_bytes())==digest,name
   assert '_elden_ucsi_enable_events' in names and b'ELDEN_UCSI_EVENTS ready=' in kernel
  if int(flags.get('elden-regs','0')):
   assert flags['elden-regs'] in ('1','2') and flags.get('elden-power-votes')=='1'
   path=d/'power-path-qualification.json';assert sha(path.read_bytes())==packet['power_path_qualification']
   proof=json.loads(path.read_text());assert proof['accepted'] and proof['checks']>=932
   for name,digest in proof['sources'].items():assert sha((source/'power'/name).read_bytes())==digest,name
   if flags['elden-regs']=='2':assert proof['mode']==2 and proof['register_count']==19 and proof['gpio_checks']>=501 and b'ELDEN_GPIO_REG index=' in kernel
   assert '_elden_power_reg_next' in names and '_elden_power_reg_receive' in names
   assert b'ELDEN_POWER_PATH cycle=' in kernel
  for key in ['elden-wcn-power','pagani-wifi']:
   assert flags.get(key,'0')=='0',(e['id'],key)
  profile=packet['default'] if e['id']=='xnu' else e['id'][6:] if e['id'].startswith('basic-') else None
  if profile is not None:
   expected=packet['profiles'][profile]
   for key in ['elden-adsp','elden-pmic','elden-pmic-notify','elden-usb-icl','elden-power-votes','elden-charge-control','elden-cpu-survey','elden-backlight','elden-adc','elden-thermal','elden-battery','elden-battery-profile','elden-battery-current','elden-ucsi-query','elden-ucsi-init','elden-ucsi-pdos','elden-modem-fw-check','elden-wifi-fw-check','elden-touch','elden-touch-hid','elden-autoreturn','elden-pd-bootstrap','elden-regs','elden-ulog','elden-dc-noop']:
    assert int(flags.get(key,'0'))==expected.get(key,0),(profile,key)
   assert flags['elden-debug-session']==packet['session']
  if int(flags.get('elden-battery','0')):
   assert flags.get('elden-battery')=='1' and flags.get('elden-battery-profile')=='1' and flags.get('elden-battery-current')=='-1'
   contract_path=d/'battery-publication-contract.json';contract=json.loads(contract_path.read_text())
   assert sha(contract_path.read_bytes())==packet['battery_publication_contract'] and contract['accepted']
   assert contract['first_publication_cycle']==4 and contract['qualified_publication_cycles']>=50 and contract['qualified_net_charging_cycles']==0
   for name,digest in contract['source_sha256'].items():assert sha((source/'power'/name).read_bytes())==digest,name
   for n in ['_elden_battery_gate_update','_elden_battery_gate_reset','__ZTV12EldenBattery','__ZN15IOPMPowerSource12updateStatusEv']:assert n in names,n
   assert 'elden_battery_gate_update(&batteryReadiness' in (source/'power/BatteryRuntime.cpp').read_text()
  if int(flags.get('elden-adc','0')):
   assert int(flags['elden-adc']) in (1,2,3) and packet['adc_gpio_writes'] is False
   path=d/'adc-qualification.json';proof=json.loads(path.read_text())
   assert sha(path.read_bytes())==packet['adc_qualification'] and proof['accepted']
   for name,digest in proof['sources'].items():assert sha((source/'power'/name).read_bytes())==digest,name
   for n in ['_elden_adc_runtime_start','_elden_adc_spmi_find','_elden_adc_sample','_elden_adc_therm_scale','_elden_usb_thermal_decision']:assert n in names,n
   assert 'elden_adc_runtime_start();' in (source/'power/PmicClient.cpp').read_text()
  if int(flags.get('elden-thermal','0')):
   assert int(flags['elden-thermal']) in (1,2,3) and flags.get('elden-adc','0')=='0'
   if flags['elden-thermal']=='3':assert int(flags.get('elden-pd-bootstrap','0')) in (1,2)
   path=d/'thermal-qualification.json';proof=json.loads(path.read_text())
   assert sha(path.read_bytes())==packet['thermal_qualification'] and proof['accepted']
   for name,digest in proof['sources'].items():assert sha((source/'power'/name).read_bytes())==digest,name
   for n in ['_elden_thermal_runtime_start','_elden_thermal_request_stop','_elden_battery_temperature_get','_elden_thermal_control_step','_elden_thermal_pin_set']:assert n in names,n
   assert b'ELDEN_THERMAL RPC stop=' in kernel and 'elden_thermal_request_stop();' in (source/'usb/EldenSdamRecovery.inc').read_text()
  if int(flags.get('elden-ucsi-pdos','0')):
   assert flags.get('elden-ucsi-pdos')=='1'
   path=d/'pd-qualification.json';proof=json.loads(path.read_text())
   assert sha(path.read_bytes())==packet['pd_qualification'] and proof['accepted']
   for name,digest in proof['sources'].items():assert sha((source/'power'/name).read_bytes())==digest,name
   for n in ['_elden_ucsi_pdo_control','_elden_ucsi_enable_pdos','_elden_pd_contract']:assert n in names,n
   assert 'elden_ucsi_enable_pdos(&ucsi,pdos==1);' in (source/'power/PmicClient.cpp').read_text()
  if int(flags.get('elden-pd-bootstrap','0')):
   assert flags['elden-thermal']=='3' and int(flags['elden-autoreturn'])>=180 and flags['elden-ucsi-pdos']=='1'
   proof_path=d/'pd-bootstrap-qualification.json';proof=json.loads(proof_path.read_text())
   assert proof['accepted'] and sha(proof_path.read_bytes())==packet['pd_bootstrap_qualification']
   for name,digest in proof['sources'].items():assert sha((source/'power'/name).read_bytes())==digest,name
   assert '_elden_pd_bootstrap_request_stop' in names and b'ELDEN_PD_BOOT stop=' in kernel
  if flags.get('elden-touch')=='4':
   assert flags.get('elden-touch-hid','0')=='0'
   path=d/'touch-survey-qualification.json';proof=json.loads(path.read_text())
   assert sha(path.read_bytes())==packet['touch_survey_qualification'] and proof['accepted'] and proof['no_spi_or_dma_commands']
   for name,digest in proof['sources'].items():assert sha((source/'tree'/name).read_bytes())==digest,name
   runtime=(source/'tree/drivers/EldenNvt36536/Runtime.cpp').read_text()
   platform=(source/'tree/drivers/EldenNvt36536/Platform.c').read_text()
   assert 'c->platform.survey_only=mode==4;' in runtime and 'if(p->survey_only)return 0;' in platform
   assert 'ELDEN_TOUCH_DMA SMMU result=' in runtime and b'ELDEN_TOUCH_DMA SMMU result=' in kernel
 for path,digest in packet['unchanged'].items():
  b=fat.read('/EFI/UniXNU/'+path);assert b==base.read('/EFI/UniXNU/'+path) and sha(b)==digest
 assert adt_walk(fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt'))['window']==0x180000000
 if packet['profiles'][packet['default']].get('elden-backlight',0):
  if packet['default'] in ('backlight-ui','pd-power-ui','pd-path-ui','pd-ulog-ui','pd-ulog-pages','pd-gpio-ui','ucsi-events','pd-ucsi-events','dc-noop','pd-policy-trace','pd-handoff','pd-handoff-quiet','dc-noop-quiet','dc-native-trial'):
   first={w.split('=',1)[0]:w.split('=',1)[1] for w in cfg['entry'][0]['args'].split() if '=' in w}
   assert first.get('elden-backlight')=='6' and int(first.get('elden-autoreturn','0'))>=300
   path=d/'backlight-ui-qualification.json';assert sha(path.read_bytes())==packet['backlight_ui_qualification']
   review=json.loads(path.read_text());assert review['accepted'] and not review['service_ui_tested']
   for name,digest in review['sources'].items():assert sha((source/name).read_bytes())==digest,name
   evidence=ROOT/review['stop_report'];assert sha(evidence.read_bytes())==review['stop_report_sha256']
   assert '_elden_backlight_request_stop' in names and b'ELDEN_BACKLIGHT refused original-class conformance' in kernel
  if packet['default'] in ('backlight-power-trial','pd-backlight-power'):
   first={w.split('=',1)[0]:w.split('=',1)[1] for w in cfg['entry'][0]['args'].split() if '=' in w}
   assert first.get('elden-backlight')=='5' and first.get('elden-autoreturn')=='450'
   path=d/'backlight-power-qualification.json';assert sha(path.read_bytes())==packet['backlight_power_qualification']
   review=json.loads(path.read_text());assert review['accepted'] and review['lifecycle_races']>=1000
   for name,digest in review['sources'].items():assert sha((source/name).read_bytes())==digest,name
   evidence=ROOT/review['cfg7_live_report'];assert sha(evidence.read_bytes())==review['cfg7_live_report_sha256']
   proof=json.loads(evidence.read_text());assert proof['cfg7_i2c_confirmed'] and proof['autonomous_stop_confirmed'] and proof['zero_chip_writes']
   assert '_elden_backlight_request_stop' in names and b'ELDEN_BACKLIGHT RPC stop=' in kernel
  if packet['default']=='backlight-settled-trial':
   first={w.split('=',1)[0]:w.split('=',1)[1] for w in cfg['entry'][0]['args'].split() if '=' in w}
   assert first.get('elden-backlight')=='4' and first.get('elden-autoreturn')=='150'
   for suffix,key in [('observation','backlight_trial_observation'),('review','backlight_trial_review')]:
    path=d/f'backlight-trial-{suffix}.json';assert sha(path.read_bytes())==packet[key]
   proof=json.loads((d/'backlight-trial-observation.json').read_text())
   assert proof['all_later_flags_clear'] and proof['later_brightness_configuration_unchanged'] and proof['dropped_max']==0
   review=json.loads((d/'backlight-trial-review.json').read_text())
   for name,digest in review['sources'].items():assert sha((source/'backlight'/name).read_bytes())==digest,name
   assert b'ELDEN_BACKLIGHT trial-qualified=' in kernel and '_elden_aw99706_check_health' in names
  if packet['default']=='backlight-delayed':
   first={w.split('=',1)[0]:w.split('=',1)[1] for w in cfg['entry'][0]['args'].split() if '=' in w}
   assert first.get('elden-backlight')=='1' and first.get('elden-backlight-delay-ms')=='5000'
   assert first.get('elden-autoreturn')=='150'
   assert b'ELDEN_BACKLIGHT first-read delay-ms=' in kernel and b'ELDEN_BACKLIGHT protection-flags=' in kernel
  abi=json.loads((out/'backlight-abi.json').read_text())
  assert abi['original_slots']==169 and abi['original_instance_size']==0x370 and abi['class_initializer_deferred']
  for n,va in abi['bindings'].items():assert names[n]==va,n
  for n in ['_elden_backlight_runtime_start','_elden_bl_platform_acquire','_elden_aw99706_set_level','__ZTV14EldenBacklight']:assert n in names,n
  table=struct.unpack_from('<169Q',kernel,maker.locate(kernel,names['__ZTV14EldenBacklight']+16)[1])
  parent=struct.unpack_from('<168Q',kernel,maker.locate(kernel,names['__ZTV9IOService']+16)[1])
  own=set(abi['owned_slots'])
  owned_addresses={v for n,v in names.items() if n.startswith(('__ZN14EldenBacklight','__ZNK14EldenBacklight'))}
  for index,address in enumerate(table):
   assert address in owned_addresses if index in own else index<168 and address==parent[index],('backlight live slot',index)
  header=(source/'backlight-generated/OriginalBacklightABI.h').read_text()
  assert 'originalPrivate[0x370-sizeof(IOService)]' in header
  assert 'elden_backlight_runtime_start();' in (source/'backlight-generated/EldenPowerStartup.inc').read_text()
 r=dict(accepted=True,kernel_sha256=sha(kernel),image_sha256=a.sha256,session=packet['session'],stock_index=a.index,
  footer_location=a.location,original_modules=len(plan['original_executable_sha256']),hid_slots=207,hid_bindings=len(hid['bindings']),
  event_driver_slots=220,event_slots=37,firmware_paths=7,menu_entries=len(cfg['entry']),hardware_tested=False)
 if packet['profiles'][packet['default']].get('elden-backlight',0):r.update(backlight_slots=169,backlight_bindings=3,backlight_live_table_checked=True)
 (d/'independent-audit.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
if __name__=='__main__':main()
