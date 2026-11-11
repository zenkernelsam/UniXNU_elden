"""Verify the normal-dependency RAM clone, exact payloads and constrained scope."""
import hashlib,json,plistlib,re,struct
from icon_cpu_trust import append

def entitlements(b):
 n,size=struct.unpack_from('<II',b,16);at=32
 for _ in range(n):
  cmd,cmdsize=struct.unpack_from('<II',b,at);assert cmdsize>=8 and at+cmdsize<=32+size
  if cmd==0x1d:
   off,count=struct.unpack_from('<II',b,at+8);blob=b[off:off+count];_,total,entries=struct.unpack_from('>III',blob)
   for i in range(entries):
    slot,pos=struct.unpack_from('>II',blob,12+i*8);magic,length=struct.unpack_from('>II',blob,pos);assert pos+length<=total
    if magic==0xfade7171:return plistlib.loads(blob[pos+8:pos+length])
  at+=cmdsize
 raise AssertionError('missing original entitlements')

def verify(root,version,d,kernel):
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 ref=root/'edl_work/icon-dependency-20261111'
 if (ref/f'v{version}').is_dir():ref=ref/f'v{version}'
 src=root/f'research/basic-bundle-20261108/source-compiled-v{version}'
 lb=json.loads((ref/'library-build.json').read_text());ab=json.loads((ref/'signed.json').read_text());prepare=json.loads((ref/'prepare.json').read_text())
 lib=(ref/'EldenIconCpuMode.dylib').read_bytes();agent=(ref/'iconservicesagent').read_bytes();original=(root/'research/loader-policy-20261111/OS-iconservicesagent').read_bytes()
 assert hashlib.sha256(original).hexdigest()==prepare['original_sha256']=='dd01b74e4413e0bb72de4a62f3f7675deb5026cad79d376fde6570abd0d012f8'
 assert hashlib.sha256(lib).hexdigest()==lb['dylib_sha256'] and hashlib.sha256(agent).hexdigest()==ab['signed_sha256']
 assert entitlements(agent)==entitlements(original)==prepare['entitlements']
 for v in prepare['code_sections'].values():assert hashlib.sha256(agent[v['offset']:v['offset']+v['bytes']]).hexdigest()==v['sha256']
 assert agent[prepare['uuid_offset']:prepare['uuid_offset']+16].hex()==prepare['new_uuid']==ab['new_uuid']
 assert prepare['dependency']==lb['install_name'] and prepare['dependency'].encode()+b'\0' in agent
 expected_parent='/System/Library/CoreServices/' if version>=67 else '/private/var/tmp/elden-icon-cpu/' if version>=66 else '/private/var/mobile/Library/Caches/com.apple.iconservicesagent/elden/'
 assert prepare['dependency'].startswith(expected_parent)
 assert sha(src/'package/EldenIconCpuMode.c')==lb['source_sha256'] and sha(src/'package/EldenIconCpuPaths.h')==lb['paths_sha256']
 if version in (90,91,92):
  assert lb['graphic_variant_cpu']
  for name,digest in lb['extra_sources'].items():assert sha(src/'package'/name)==digest
  glyph=json.loads((root/f'edl_work/basic-bundle-20261108/v{version}/glyph-codegen.json').read_text())
  assert glyph['accepted'] and glyph['library_sha256']==lb['dylib_sha256'] and glyph['kernel_sha256']==hashlib.sha256(kernel).hexdigest()
 header=(src/'xnu/EldenIconCpuPayload.h').read_text()
 for name,data in [('elden_icon_cpu_payload',lib),('elden_icon_agent_payload',agent)]:
  match=re.search(r'static const unsigned char '+name+r'\[\] = \{([^}]+)\}',header);assert match
  assert bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',match[1]))==data and kernel.count(data)==1
 base=(root/'edl_work/icon-cpu-20261111/original.trustcache').read_bytes();tc,lh=append(base,lib);tc,ah=append(tc,agent)
 assert tc==(ref/'kernel.macho.trustcache').read_bytes()
 env=(src/'xnu/EldenIconCpuEnv.inc').read_text();common=(src/'xnu/EldenIconCpuCommon.inc').read_text();stage=(src/'xnu/EldenIconCpuStage.inc').read_text();executive=(src/'xnu/kern_exec.c').read_text()
 assert 'EXECUTABLE_KEY ELDEN_ICON_ORIGINAL_PATH' in env and 'DYLD_INSERT_LIBRARIES=/' not in env
 assert '!strcmp(original,ELDEN_ICON_ORIGINAL_PATH)' in common
 if version<67:
  assert 'return ELDEN_ICON_PROGRAM_PATH' in common
  assert 'lookup_path = elden_icon_cpu_exec_path(excpath);' in executive and 'CAST_USER_ADDR_T(lookup_path)' in executive
 else:
  assert 'lookup_path = elden_icon_cpu_exec_path' not in executive and 'CAST_USER_ADDR_T(excpath)' in executive
  assert 'native_gpu_addon_mount(p,scratch,0xb00' in stage and 'elden_icon_cpu_visible' in stage
  assert 'MNT_RDONLY|MNT_UNION' in stage and 'unmount(p,&request,&result)' in stage
  kd=(src/'xnu/kdebug.c').read_text()
  if version>=74:
   from ui_dispatch_qualification import verify as verify_ui_dispatch
   verify_ui_dispatch(root,version,kernel)
   audit=json.loads((root/f'edl_work/basic-bundle-20261108/v{version}/ui-dispatch-codegen.json').read_text())
   before=root/'research/basic-bundle-20261108/source-compiled-v72/xnu/kdebug.c'
   assert sha(before)==audit['kdebug_before_sha256']
   # The UI audit proves the exact scoped delta in the actual compiled file.
   # Apply the original icon-only equality check to its validated predecessor.
   kd=before.read_text()
  declaration='#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\nextern void elden_icon_cpu_user_marker(struct proc *,uint32_t,uint64_t,uint64_t,uint64_t,uint64_t);\n#endif\n'
  call='#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\n\telden_icon_cpu_user_marker(p,uap->code,uap->arg1,uap->arg2,uap->arg3,uap->arg4);\n#endif\n'
  if version>=72:
   declaration=declaration.replace('XNU_OWNER_DATA_MOUNT','XNU_ELDEN_ICON_OBSERVER')
   call=call.replace('XNU_OWNER_DATA_MOUNT','XNU_ELDEN_ICON_OBSERVER')
  assert kd.count(declaration)==1 and kd.count(call)==1
  assert kd.replace(declaration,'').replace(call,'')==(root/'research/icon-overlay-20261111/kdebug-before.c').read_text()
  assert 'reported,1u' in common and '>64' in common and '!=276' in common
  if version>=69:
   assert 'proc_uniqueid(p)' in common and 'elden_icon_cpu_record(p);' in executive
   assert 'ELDEN_ICON_SIGNATURE' in executive
  if version>=70:
   assert 'elden_icon_cpu_owner_launch(p,imgp,error,launch_constraint_reason)' in executive
   assert 'mac_proc_check_launch_constraints(p, imgp, &launch_constraint_reason)' in executive
   assert 'os_reason_free(launch_constraint_reason)' in executive
   assert 'c[8]p[1]m[1]e[6], (Constraint not matched)' in common
   assert 'proc_getcdhash(p,actual)' in common and 'memcmp(actual,expected,sizeof(expected))' in common
   assert 'psa_launch_type!=1' in common and 'imgp->ip_vfs_context))!=276' in common
   expected=re.search(r'expected\[20\]=\{([^}]+)\}',common);assert expected
   assert bytes(int(x,16) for x in re.findall(r'0x([0-9a-f]{2})',expected[1])).hex()==ah
  if version>=68:
   exiting=(src/'xnu/kern_exit.c').read_text()
   assert 'elden_icon_cpu_exit_selected(p)' in exiting and 'text=%.768s' in exiting
   assert 'proc_handle_critical_exit(p, rv);' in exiting and 'exit_reason_get_string_desc(p->p_exit_reason)' in exiting
 assert 'O_CREAT|O_EXCL|O_NOFOLLOW' in stage and 'memcmp(check,bytes,length)' in stage
 assert 'native_owner_data_seed_size!=UINT64_C(536870912)' in stage
 if version<67:assert 'va.va_data_size>4096' in stage
 code=root/f'edl_work/basic-bundle-20261108/v{version}/icon-codegen.json';c=json.loads(code.read_text())
 assert c['accepted'] and c['kernel_sha256']==hashlib.sha256(kernel).hexdigest()
 assert c['marker_no_simd'] if version>=67 else c['status_worker_no_simd']
 if version>=72:assert c['live_entry_observers_verified']
 assert c['env_sha256']==sha(src/'xnu/EldenIconCpuEnv.inc') and c['stage_sha256']==sha(src/'xnu/EldenIconCpuStage.inc')
 if version<67:assert b'ELDEN_ICON_CPU redirect original service' in kernel
 assert b'normal-dependency=1' in kernel
 return dict(library_sha256=lb['dylib_sha256'],agent_sha256=ab['signed_sha256'],library_cdhash=lh,agent_cdhash=ah,trustcache_sha256=hashlib.sha256(tc).hexdigest(),codegen_sha256=sha(code),normal_dependency=True,readonly_system_overlay=version>=67,original_code_and_entitlements_preserved=True,hardware_tested=False)
