"""Generate a RAM-only normal-dependency exec patch and immutable payloads."""
from pathlib import Path
import hashlib,json,difflib,shutil
from icon_cpu_trust import append
r=Path(__file__).resolve().parents[1];port=Path('D:/Desktop/Git/UniXNU_elden');d=r/'edl_work/icon-dependency-20261111';sha=lambda b:hashlib.sha256(b).hexdigest()
lib=(d/'EldenIconCpuMode.dylib').read_bytes();agent=(d/'iconservicesagent').read_bytes()
lb=json.loads((d/'library-build.json').read_text());ab=json.loads((d/'signed.json').read_text())
assert sha(lib)==lb['dylib_sha256'] and sha(agent)==ab['signed_sha256'] and ab['original_text_sections_unchanged'] and ab['entitlements_unchanged']
assert sha((port/'package/EldenIconCpuMode.c').read_bytes())==lb['source_sha256']
assert sha((port/'package/EldenIconCpuPaths.h').read_bytes())==lb['paths_sha256']
header='#pragma once\n'
for name,data in [('elden_icon_cpu_payload',lib),('elden_icon_agent_payload',agent)]:
 header+='static const unsigned char '+name+'[] = {\n'+'\n'.join(','.join('0x%02x'%x for x in data[i:i+16])+',' for i in range(0,len(data),16))+'\n};\n'
(port/'patches/xnu/EldenIconCpuPayload.h').write_text(header,newline='\n')
shutil.copyfile(port/'package/EldenIconCpuPaths.h',port/'patches/xnu/EldenIconCpuPaths.h')
before=(r/'research/iconservices-cpu-20261111/xnu-before/kern_exec.c').read_text();after=before
old='#define EXECUTABLE_KEY "executable_path="';assert after.count(old)==1
after=after.replace(old,old+'\n#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\n#include "EldenIconCpuCommon.inc"\n#endif')
begin=after.index('static int\nexec_activate_image(struct image_params *imgp)\n{')
end=after.index('\n/*\n * exec_validate_spawnattr_policy',begin)
body=after[begin:end]
body=body.replace('\tint itercount = 0;','\tint itercount = 0;\n\tint icon_selected = 0, icon_stage = 0;',1)
marker='\tNDINIT(ndp, LOOKUP, OP_LOOKUP, FOLLOW | LOCKLEAF | AUDITVNPATH1,'
assert body.count(marker)==1
body=body.replace(marker,'#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\n\ticon_selected = elden_icon_cpu_selected(excpath);\n\tif (icon_selected) { icon_stage = 1; elden_icon_cpu_record(p); }\n#endif\n'+marker,1)
marker='\timgp->ip_vp = ndp->ni_vp;       /* if set, need to vnode_put() at some point */'
assert marker in body;body=body.replace(marker,marker+'\n\tif (icon_selected) icon_stage = 2;',1)
marker='\terror = exec_check_permissions(imgp);';assert marker in body;body=body.replace(marker,'\tif (icon_selected) icon_stage = 3;\n'+marker,1)
marker='\t/* Copy; avoid invocation of an interpreter overwriting the original */';assert marker in body;body=body.replace(marker,'\tif (icon_selected) icon_stage = 4;\n\n'+marker,1)
body=body.replace('encapsulated_binary:\n','encapsulated_binary:\n\tif (icon_selected) icon_stage = 5;\n',1)
marker='\t\terror = (*execsw[i].ex_imgact)(imgp);';assert marker in body;body=body.replace(marker,'\t\tif (icon_selected) icon_stage = 6;\n'+marker,1)
body=body.replace('\t} else if (error == 0) {','\t} else if (error == 0) {\n\t\tif (icon_selected) icon_stage = 7;',1)
marker='bad_notrans:\n';assert body.count(marker)==1
body=body.replace(marker,marker+'\tif (icon_selected) printf("ELDEN_ICON_CPU exec result=%d stage=%d uid=%u mount=%llx\\n", error, icon_stage, (unsigned)kauth_cred_getuid(vfs_context_ucred(imgp->ip_vfs_context)), imgp->ip_vp ? (unsigned long long)vfs_flags(vnode_mount(imgp->ip_vp)) : 0ULL);\n')
after=after[:begin]+body+after[end:]
old='__attribute__((noinline))\nstatic int\nexec_extract_strings(struct image_params *imgp)';assert after.count(old)==1
after=after.replace(old,'#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\n#include "EldenIconCpuEnv.inc"\n#endif\n\n'+old)
old='\t/* Save space for envv[] NULL terminator */';assert after.count(old)==1
after=after.replace(old,'#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\n\terror = elden_icon_cpu_add_environment(imgp, new_ptr_size);\n\tif (error) goto bad;\n#endif\n\n'+old)
old='#include "EldenWatchdogPolicy.inc"';assert after.count(old)==1;after=after.replace(old,old+'\n#include "EldenIconCpuStage.inc"')
old='\tif (elden_policy_error) panic("ELDEN watchdog launch policy failed: %d", elden_policy_error);';assert after.count(old)==1;after=after.replace(old,old+'\n\telden_icon_cpu_prepare(p, (user_addr_t)scratch_addr);')
start=after.index('static int\nprocess_signature(proc_t p, struct image_params *imgp)')
end=after.index('\n/*',start)
body=after[start:end]
constraint='''\tif ((error = mac_proc_check_launch_constraints(p, imgp, &launch_constraint_reason)) != 0) {
\t\tsignature_failure_reason = launch_constraint_reason;
\t\tgoto done;
\t}'''
assert body.count(constraint)==1
body=body.replace(constraint,'''\tif ((error = mac_proc_check_launch_constraints(p, imgp, &launch_constraint_reason)) != 0) {
#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT
\t\tif (elden_icon_cpu_owner_launch(p,imgp,error,launch_constraint_reason)) {
\t\t\tos_reason_free(launch_constraint_reason);
\t\t\tlaunch_constraint_reason = OS_REASON_NULL;
\t\t\terror = 0;
\t\t} else
#endif
\t\t{
\t\t\tsignature_failure_reason = launch_constraint_reason;
\t\t\tgoto done;
\t\t}
\t}''')
point='\t} else {\n\t\tif (!unexpected_failure) {'
assert body.count(point)==1
body=body.replace(point,'\t} else {\n#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\n\t\tif (elden_icon_cpu_process_selected(p)) printf("ELDEN_ICON_SIGNATURE pid=%d unique=%llu error=%d mac=%d ns=%u code=%llx flags=%x\\n",proc_pid(p),(unsigned long long)proc_uniqueid(p),error,imgp->ip_mac_return,signature_failure_reason ? signature_failure_reason->osr_namespace : 0,signature_failure_reason ? signature_failure_reason->osr_code : 0ULL,proc_getcsflags(p));\n#endif\n\t\tif (!unexpected_failure) {')
def trace(stage,result='0',value='0'):
 return '#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\n\telden_icon_cpu_progress(p,"'+stage+'",'+result+','+value+');\n#endif\n'
for anchor,stage,result,value in [
 ('\t/* If the code signature came through the image activation path, we skip the','launch-checks-complete','error','imgp->ip_csflags'),
 ('\tkr = task_get_task_access_port(proc_task(p), &port);','access-port-enter','0','imgp->ip_csflags'),
 ('\tkr = __EXEC_WAITING_ON_TASKGATED_CODE_SIGNATURE_UPCALL__(port, proc_getpid(p));','taskgated-enter','0','imgp->ip_csflags'),
 ('\t\terror = proc_process_signature(p, &signature_failure_reason);','process-signature-enter','error','0'),
 ('\t\tkr = thread_process_signature(imgp->ip_new_thread, proc_get_task_raw(p));','thread-signature-enter','error','0'),
 ('\treturn error;','signature-return','error','imgp->ip_csflags')]:
 assert body.count(anchor)==1,(stage,body.count(anchor));body=body.replace(anchor,trace(stage,result,value)+anchor,1)
for anchor,stage,result,value in [
 ('\tproc_csflags_update(p, imgp->ip_csflags);\n\tproc_unlock(p);','flags-copied','0','imgp->ip_csflags'),
 ('\tkr = task_get_task_access_port(proc_task(p), &port);','access-port-return','kr','IPC_PORT_VALID(port)'),
 ('\tkr = __EXEC_WAITING_ON_TASKGATED_CODE_SIGNATURE_UPCALL__(port, proc_getpid(p));','taskgated-return','kr','0'),
 ('\t\terror = proc_process_signature(p, &signature_failure_reason);','process-signature-return','error','0'),
 ('\t\tkr = thread_process_signature(imgp->ip_new_thread, proc_get_task_raw(p));','thread-signature-return','kr','0')]:
 assert body.count(anchor)==1;body=body.replace(anchor,anchor+'\n'+trace(stage,result,value),1)
after=after[:start]+body+after[end:]
patch=''.join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile='a/bsd/kern/kern_exec.c',tofile='b/bsd/kern/kern_exec.c'))
kb=(r/'research/icon-overlay-20261111/kdebug-before.c').read_text();ka=kb
marker='int\nkdebug_trace64(__unused struct proc *p, struct kdebug_trace64_args *uap,'
assert ka.count(marker)==1
ka=ka.replace(marker,'#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\nextern void elden_icon_cpu_user_marker(struct proc *,uint32_t,uint64_t,uint64_t,uint64_t,uint64_t);\n#endif\n'+marker)
marker='    __unused int32_t *retval)\n{\n\tif (__probable(kdebug_enable == 0))'
assert ka.count(marker)==1
ka=ka.replace(marker,marker.replace('\tif (__probable', '#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\n\telden_icon_cpu_user_marker(p,uap->code,uap->arg1,uap->arg2,uap->arg3,uap->arg4);\n#endif\n\tif (__probable'))
ka=ka.replace('#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT','#if defined(XNU_ELDEN_ICON_OBSERVER) && XNU_ELDEN_ICON_OBSERVER')
patch+=''.join(difflib.unified_diff(kb.splitlines(True),ka.splitlines(True),fromfile='a/bsd/kern/kdebug.c',tofile='b/bsd/kern/kdebug.c'))
eb=(r/'research/icon-overlay-20261111/kern_exit-before.c').read_text();ea=eb
marker='void\nproc_prepareexit(proc_t p, int rv, boolean_t perf_notify)'
assert ea.count(marker)==1
ea=ea.replace(marker,'#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\nextern int elden_icon_cpu_exit_selected(proc_t);\n#endif\n'+marker)
marker='\tif (p->p_crash_behavior != 0 || p == initproc) {\n\t\tproc_handle_critical_exit(p, rv);'
assert ea.count(marker)==1
observer='#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT\n\tif (elden_icon_cpu_exit_selected(p)) {\n\t\tchar *message = exit_reason_get_string_desc(p->p_exit_reason);\n\t\tprintf("ELDEN_ICON_EXIT pid=%d rv=%x ns=%u code=%llx text=%.768s\\n", proc_getpid(p), rv, p->p_exit_reason ? p->p_exit_reason->osr_namespace : 0, p->p_exit_reason ? p->p_exit_reason->osr_code : 0ULL, message ? message : "none");\n\t}\n#endif\n\n'
ea=ea.replace(marker,observer+marker)
ea=ea.replace('#if defined(XNU_OWNER_DATA_MOUNT) && XNU_OWNER_DATA_MOUNT','#if defined(XNU_ELDEN_ICON_OBSERVER) && XNU_ELDEN_ICON_OBSERVER')
patch+=''.join(difflib.unified_diff(eb.splitlines(True),ea.splitlines(True),fromfile='a/bsd/kern/kern_exit.c',tofile='b/bsd/kern/kern_exit.c'))
(port/'patches/xnu/elden-icon-cpu.patch').write_text(patch,newline='\n')
original=(r/'edl_work/icon-cpu-20261111/original.trustcache').read_bytes();tc,lh=append(original,lib);tc,ah=append(tc,agent)
(d/'kernel.macho.trustcache').write_bytes(tc)
report=dict(library_sha256=sha(lib),agent_sha256=sha(agent),library_cdhash=lh,agent_cdhash=ah,original_tc_sha256=sha(original),new_tc_sha256=sha(tc),patch_sha256=sha(patch.encode()),header_sha256=sha(header.encode()),hardware_tested=False)
(d/'kernel-integration.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
