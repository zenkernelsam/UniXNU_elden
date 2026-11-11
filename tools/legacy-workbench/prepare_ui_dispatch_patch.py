"""Generate exact patches against v72, preserving the owner's later working files."""
from pathlib import Path
import difflib,hashlib,json,zipfile
root=Path(__file__).resolve().parents[1];device=Path('D:/Desktop/Git/UniXNU_elden')
out=root/'research/ui-dispatch-20261111';out.mkdir(exist_ok=True)
baseline=root/'research/basic-bundle-20261108/source-compiled-v72/xnu/kdebug.c'
s=baseline.read_text();old=s
def replace(a,b):
    global s
    assert s.count(a)==1,a[:90]
    s=s.replace(a,b)
replace('static mach_port_t kdbg_typefilter_memory_entry;','''static mach_port_t kdbg_typefilter_memory_entry;
#if defined(XNU_ELDEN_UI_DISPATCH) && XNU_ELDEN_UI_DISPATCH
static bool elden_ui_active,elden_ui_filter_revoked;
static void elden_ui_filter_changed(typefilter_t tf) {
    if(tf==kdbg_typefilter && os_atomic_load(&elden_ui_active,acquire))
        os_atomic_store(&elden_ui_filter_revoked,true,release);
}
#endif''')
for signature,arg in [('typefilter_copy(typefilter_t dst, typefilter_t src)','dst'),
    ('typefilter_reject_all(typefilter_t tf)','tf'),('typefilter_allow_all(typefilter_t tf)','tf'),
    ('typefilter_allow_class(typefilter_t tf, uint8_t class)','tf'),('typefilter_allow_csc(typefilter_t tf, uint16_t csc)','tf')]:
    replace(signature+'\n{',signature+'\n{\n#if defined(XNU_ELDEN_UI_DISPATCH) && XNU_ELDEN_UI_DISPATCH\n\telden_ui_filter_changed('+arg+');\n#endif')
marker='int\nkdebug_trace64(__unused struct proc *p, struct kdebug_trace64_args *uap,'
replace(marker,'#if defined(XNU_ELDEN_UI_DISPATCH) && XNU_ELDEN_UI_DISPATCH\n#include "EldenUiDispatch.inc"\n/* Preserve scalar argument forwarding in the legacy syscall wrapper. */\n__attribute__((always_inline))\n#endif\n'+marker)
replace('\telden_icon_cpu_user_marker(p,uap->code,uap->arg1,uap->arg2,uap->arg3,uap->arg4);\n#endif',
'''\telden_icon_cpu_user_marker(p,uap->code,uap->arg1,uap->arg2,uap->arg3,uap->arg4);
#endif
#if defined(XNU_ELDEN_UI_DISPATCH) && XNU_ELDEN_UI_DISPATCH
\telden_ui_dispatch_note(p,uap->code,uap->arg1,uap->arg2);
#endif''')
replace('kdebug_commpage_state(void)\n{\n\tuint32_t state = 0;',
'''kdebug_commpage_state(void)
{
#if defined(XNU_ELDEN_UI_DISPATCH) && XNU_ELDEN_UI_DISPATCH
\tif(os_atomic_load(&elden_ui_active,acquire) && kdebug_enable)
\t\tos_atomic_store(&elden_ui_filter_revoked,true,release);
#endif
\tuint32_t state = 0;''')
replace('\treturn state;\n}\n\nstatic int\nkdbg_setreg',
'''#if defined(XNU_ELDEN_UI_DISPATCH) && XNU_ELDEN_UI_DISPATCH
\tif(!kdebug_enable && elden_ui_dispatch_gate())
\t\tstate=KDEBUG_COMMPAGE_ENABLE_TRACE|KDEBUG_COMMPAGE_ENABLE_TYPEFILTER;
#endif
\treturn state;
}

static int
kdbg_setreg''')
patch=''.join(difflib.unified_diff(old.splitlines(True),s.splitlines(True),fromfile='a/bsd/kern/kdebug.c',tofile='b/bsd/kern/kdebug.c'))
(device/'patches/xnu/elden-ui-dispatch.patch').write_text(patch,encoding='utf-8',newline='\n')
(out/'kdebug-dispatch.c').write_text(s,encoding='utf-8',newline='\n')
# Explicitly use the completed v72 diagnostic source, avoiding the failed v73
# user-commpage read. The input producer/report and power code stay frozen.
with zipfile.ZipFile(root/'edl_work/basic-bundle-20261108/v72/source-upload.zip') as z:
    manifest=json.loads(z.read('manifest.json'))
    for name in ('scripts/build-elden-hid.py','scripts/driver-patches/EldenUserWaitProbe.c','scripts/driver-patches/EldenHidGlue.cpp'):
        b=z.read(name);assert hashlib.sha256(b).hexdigest()==manifest[name]
        if name.endswith('EldenHidGlue.cpp'):
            text=b.decode();marker='static void settingsTapWorker(void *,wait_result_t){'
            assert text.count(marker)==1
            text=text.replace(marker,'extern "C" int elden_ui_dispatch_begin(void) __attribute__((weak_import));\n'+marker)
            marker='    int down=-1,up=-1;';assert text.count(marker)==1
            text=text.replace(marker,'''    int observation=elden_ui_dispatch_begin?elden_ui_dispatch_begin():0;
    char armLine[100];snprintf(armLine,sizeof(armLine),"\\nELDEN_UI_DISPATCH arm=%d window-seconds=30\\n",observation);say(armLine);
'''+marker)
            b=text.encode()
        f=out/'candidate-source'/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_bytes(b)
record={'baseline_kdebug_sha256':hashlib.sha256(baseline.read_bytes()).hexdigest(),
        'patch_sha256':hashlib.sha256(patch.encode()).hexdigest(),
        'candidate_files':{str(p.relative_to(out/'candidate-source')):hashlib.sha256(p.read_bytes()).hexdigest() for p in (out/'candidate-source').rglob('*') if p.is_file()},
        'hardware_tested':False,'basis':'v72; no contact-ID, clock, power or report algorithm changes'}
(out/'source-delta.json').write_text(json.dumps(record,indent=2)+'\n')
print(json.dumps(record,indent=2))
