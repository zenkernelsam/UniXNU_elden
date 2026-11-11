"""Bind guarded boot packaging to the audited CPU8 link and exact sources."""
from pathlib import Path
import hashlib,json
def verify_two_core(root,kernel):
 """Eight cores require actual admission of the exact same kernel on two cores."""
 path=root/'research/cpu-asm-fixed-20261111/two-core-qualification-v86.json'
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 proof=json.loads(path.read_text())
 assert proof['qualified'] and proof['cpus']==2 and proof['mask']=='3'
 assert proof['kernel_sha256']==hashlib.sha256(kernel).hexdigest()
 assert proof['stable_seconds']>=4.9 and proof['dropped']==0
 assert proof['fastboot_observed'] and not proof['pc_reset_requested']
 for name,digest in proof['evidence_sha256'].items():assert sha(root/name)==digest,name
 return {'qualification_sha256':sha(path),'kernel_sha256':proof['kernel_sha256'],'candidate_sha256':proof['candidate_sha256'],'session':proof['session']}

def verify_hold(root,version,kernel):
 assert version in (87,88,89,90,91,92)
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 path=root/'research/cpu-asm-fixed-20261111/eight-core-qualification-v86.json';proof=json.loads(path.read_text())
 assert proof['qualified'] and proof['cpus']==8 and proof['mask']=='ff' and proof['fastboot_observed'] and not proof['pc_reset_requested']
 for name,digest in proof['evidence_sha256'].items():assert sha(root/name)==digest,name
 src=root/f'research/basic-bundle-20261108/source-compiled-v{version}'
 old=root/'research/basic-bundle-20261108/source-compiled-v86'
 for name in ('ArmCpu.cpp','GuardPolicy.h','Deferred.py'):assert (src/'cpu'/name).read_bytes()==(old/'cpu'/name).read_bytes()
 code=root/f'edl_work/basic-bundle-20261108/v{version}/cpu-asm-audit.json';audit=json.loads(code.read_text())
 assert audit['accepted'] and audit['v86_cpu_source_preserved'] and audit['hold_seconds_max']==600
 assert audit['kernel_sha256']==hashlib.sha256(kernel).hexdigest()
 assert audit['source']['eight_core_qualification_sha256']==sha(path)
 host=root/'research/cpu-hold-20261111/host-policy.json';h=json.loads(host.read_text())
 assert h['passed'] and h['policy_sha256']==sha(src/'cpu/HoldPolicy.h') and h['test_sha256']==sha(src/'cpu/TestHoldPolicy.c')
 assert audit['host_hold_sha256']==sha(host)
 native=root/'research/cpu-hold-20261111/NativeUfs.cpp'
 build=json.loads((root/f'edl_work/basic-bundle-20261108/v{version}/final/cpu-irq-snapshot-result.json').read_text())
 assert sha(native)==build['objects']['ufs']['source_sha256']
 text=native.read_text();assert 'static unsigned gRootLun = 0;' in text and text.count('gRootLun =')==1
 assert 'PE_parse_boot_argn("native-ufs-lun", &gRootLun, sizeof(gRootLun))' in text
 return {'parent_eight_core_qualification_sha256':sha(path),'parent_kernel_sha256':proof['kernel_sha256'],'new_kernel_sha256':audit['kernel_sha256'],'hold_policy_sha256':h['policy_sha256'],'codegen_sha256':sha(code)}

def verify(root,version,kernel):
 assert version in (81,82,83,84,85,86,87,88,89,90,91,92)
 assert not (root/f'edl_work/basic-bundle-20261108/v{version}/withdrawn.json').exists(),'Candidate withdrawn by architecture review'
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 p=root/'research/cpu-guard-20261111';src=root/f'research/basic-bundle-20261108/source-compiled-v{version}'
 code=json.loads((p/'linked/audit.json').read_text());result=json.loads((p/'linked/result.json').read_text())
 final=root/f'edl_work/basic-bundle-20261108/v{version}/final';der=json.loads((final/'derivative.json').read_text())
 assert code['accepted_cpu_guard_link']
 active_source=result['source'];link_proof=p/'linked/audit.json'
 if version==81:assert code['kernel_sha256']==result['kernel_sha256']==der['source_sha256']
 else:
  link_proof=final.parent/('cpu-asm-audit.json' if version in (86,87,88,89,90,91,92) else 'sgi-mode-audit.json' if version==85 else 'cpu-irq-snapshot-audit.json' if version in (83,84) else 'cpu-groups-audit.json');groups=json.loads(link_proof.read_text())
  assert groups['accepted'] and groups['prelink_sha256']==der['source_sha256'] and groups['kernel_sha256']==hashlib.sha256(kernel).hexdigest()
  assert groups['source_link_reproduced'] and groups['source']['parent_source_manifest_sha256']==sha(p/'source.json')
  active_source=groups['source']
  if version in (83,84):assert groups['hardware_writes_added'] is False
  if version==84:assert groups['functions']['_armcpu_gic_diag']['plain_mmio_loads']>=7
  if version==85:assert groups['capability_guarded']
  if version in (86,87,88,89,90,91,92):assert groups['actual_sgi_msr_present'] and groups['mode_control_removed']
 assert der['derivative_sha256']==hashlib.sha256(kernel).hexdigest()
 if version in (90,91,92):
  glyph=json.loads((final.parent/'glyph-codegen.json').read_text())
  assert glyph['accepted'] and glyph['kernel_sha256']==hashlib.sha256(kernel).hexdigest() and glyph['marker_no_simd']
  for name,digest in glyph['sources'].items():assert sha(src/name)==digest,name
 if version in (89,90,91,92):
  f=json.loads((final.parent/'fault-codegen.json').read_text())
  assert f['accepted'] and f['kernel_sha256']==hashlib.sha256(kernel).hexdigest() and f['user_memory_writes'] is False
  assert f['max_fault_attempts']==4 and f['max_frames']==16
  for name,digest in f['source']['files'].items():assert sha(src/'usb'/name)==digest,name
  assert sha(final/'cpu-archives/libShengUfs.a')==f['archive_sha256']
 assert code['cpu_array_bytes']=={'_CpuDataEntries':128,'_topology_cpu_array':960,'_topology_cluster_array':144} and code['clutch_single_pset']
 assert code['original_modules']==30 and der['display_driver_bytes_preserved']
 for label in ('pd','ucsi'):
  proof=json.loads((final.parent/(label+'-codegen')/'audit.json').read_text())
  build=json.loads((final/'archives/EldenPmicClient/build.json').read_text())
  assert proof['accepted'] and proof['archive_sha256']==build['archive_sha256']
  assert build['compiler_config']=={'MAX_CPUS':8,'MAX_CPU_CLUSTERS':2}
  assert sha(final/'archives/EldenPmicClient'/Path(build['archive']).name)==build['archive_sha256']
  for name,row in proof['objects'].items():
   assert row['archive_member_equal'] and not row['fp_simd']
   assert row['sha256']==build['objects'][name]==sha(final/'archives/EldenPmicClient'/name)
 touch=json.loads((final.parent/'touch-codegen/audit.json').read_text())
 display=json.loads((final/'display-build-report.json').read_text())
 assert touch['accepted'] and touch['library_sha256']==display['archive_sha256']
 assert all(x['archive_member_equal'] and not x['fp_simd_operands'] for x in touch['objects'].values())
 for name in ('ArmCpu.cpp','GuardPolicy.h','SmpGuard.inc','Deferred.py'):
  assert sha(src/'cpu'/name)==active_source['files'][name],name
 assert sha(src/'usb/EldenSdamRecovery.inc')==active_source['files']['EldenSdamRecovery.inc']
 host=json.loads((p/'host-policy.json').read_text());assert host['passed'] and host['policy_sha256']==sha(src/'cpu/GuardPolicy.h')
 wd=root/'research/cpu-activation-20261111/expiration-qualification-v80.json';qualified=json.loads(wd.read_text())
 assert qualified['qualified'] and qualified['bark_ms']==27000 and qualified['bite_ms']==30000
 assert sha(wd)==result['source']['watchdog_qualification_sha256']
 text=(src/'usb/EldenSdamRecovery.inc').read_text()
 assert 'if(mode==2){eldenStartSmpGuard();return;}' in text
 assert 'do{eldenSmpPanicPet();elden_usb_panic_drain();' in text
 assert 'cpus<2||cpus>8' in text and 'ml_get_boot_cpu_number()!=0' in text
 assert b'ELDEN_SMP QUALIFIED_RETURN stop-pet' in kernel and b'ELDEN_SMP FAILED_RETURN stop-pet' in kernel
 return {'source_link_sha256':sha(link_proof),'source_manifest_sha256':sha(p/'source.json'),
         'watchdog_qualification_sha256':sha(wd),'host_policy_sha256':sha(p/'host-policy.json'),
         'kernel_sha256':hashlib.sha256(kernel).hexdigest(),'max_cpus':8,'max_clusters':2,'bark_ms':27000,'bite_ms':30000,'hardware_tested':False}
