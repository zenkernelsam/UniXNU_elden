/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Adapted from the 21H461 Pagani typefilter/commpage/kdebug observation method.
 * One opt-in 30 second window, armed by the existing Settings replay worker.
 * Normal ktrace retains priority. No code patches or user-memory reads. */
#include <kern/thread_call.h>
#include <pexpert/pexpert.h>
#include "EldenUiDispatchCore.h"
extern int elden_usb_preview_line(const char *,unsigned);
static LCK_SPIN_DECLARE(elden_ui_lock,&kdebug_lck_grp);
static struct elden_ui_capture elden_ui_capture;
static uint8_t elden_ui_saved_filter[KDBG_TYPEFILTER_BITMAP_SIZE];
static uint8_t elden_ui_owned_filter[KDBG_TYPEFILTER_BITMAP_SIZE];
static bool elden_ui_attempted;
static uint64_t elden_ui_begin_time;
static thread_call_t elden_ui_stop_call;
/* Only the one-shot stop callback writes this buffer. A stack array would be
 * auto-zeroed with SIMD by the development kernel's compiler flags. */
static char elden_ui_line[250];

/* Called by kdebug_commpage_state while its caller owns normal trace control.
 * If another tracing client changed our filter, do not advertise an open gate. */
static bool elden_ui_dispatch_gate(void) {
    return os_atomic_load(&elden_ui_active,acquire) && !kdebug_enable &&
        !os_atomic_load(&elden_ui_filter_revoked,acquire) &&
        kdbg_typefilter && !memcmp(kdbg_typefilter,elden_ui_owned_filter,KDBG_TYPEFILTER_BITMAP_SIZE);
}
static void elden_ui_dispatch_stop(__unused thread_call_param_t a,
    __unused thread_call_param_t b) {
    ktrace_lock();
    bool owned=elden_ui_dispatch_gate();
    lck_spin_lock_grp(&elden_ui_lock,&kdebug_lck_grp);
    os_atomic_store(&elden_ui_active,false,release);
    lck_spin_unlock(&elden_ui_lock);
    if(owned)typefilter_copy(kdbg_typefilter,elden_ui_saved_filter);
    commpage_update_kdebug_state();
    ktrace_unlock();
    /* The one-shot capture is immutable now. Never emit under either lock. */
    uint32_t emit_fail=0;
    for(uint32_t i=0;i<elden_ui_capture.count;++i) {
        const struct elden_ui_event *e=&elden_ui_capture.rows[i];
        uint64_t ns=0;absolutetime_to_nanoseconds(e->time-elden_ui_begin_time,&ns);
        int n=snprintf(elden_ui_line,sizeof(elden_ui_line),"\nELDEN_UI_DISPATCH event=%u name=%s pid=%d unique=%llu tid=%llu us=%llu arg1=%llx arg2=%llx\n",
            i,e->target?"Preferences":"SpringBoard",e->pid,(unsigned long long)e->unique,
            (unsigned long long)e->tid,(unsigned long long)(ns/1000),
            (unsigned long long)e->arg1,(unsigned long long)e->arg2);
        if(n<=0 || (size_t)n>=sizeof(elden_ui_line) || !elden_usb_preview_line(elden_ui_line,(unsigned)n))++emit_fail;
    }
    int n=snprintf(elden_ui_line,sizeof(elden_ui_line),"\nELDEN_UI_DISPATCH done total=%u saved=%u overflow=%u springboard=%u preferences=%u filter-restored=%u revoked=%u emit-fail=%u\n",
        elden_ui_capture.total,elden_ui_capture.count,elden_ui_capture.overflow,
        elden_ui_capture.targets[0],elden_ui_capture.targets[1],owned,
        os_atomic_load(&elden_ui_filter_revoked,acquire),emit_fail);
    if(n>0 && (size_t)n<sizeof(elden_ui_line))elden_usb_preview_line(elden_ui_line,(unsigned)n);
}
/* Thread-context entry; no effect without the explicit diagnostic bootarg. */
int elden_ui_dispatch_begin(void) {
    unsigned enabled=0;
    if(!PE_parse_boot_argn("elden-ui",&enabled,sizeof(enabled)) || enabled!=1)return 0;
    ktrace_lock();
    if(elden_ui_attempted || kdebug_enable || !kdbg_typefilter) {ktrace_unlock();return -1;}
    elden_ui_stop_call=thread_call_allocate(elden_ui_dispatch_stop,NULL);
    if(!elden_ui_stop_call) {ktrace_unlock();return -2;}
    elden_ui_attempted=true;
    typefilter_copy(elden_ui_saved_filter,kdbg_typefilter);
    typefilter_reject_all(elden_ui_owned_filter);
    /* Exact DBG_TRACE class, scalar stores even under -O2. */
    volatile uint8_t *owned=elden_ui_owned_filter;
    for(unsigned i=0;i<32;++i)owned[DBG_TRACE*32+i]=255;
    typefilter_allow_csc(elden_ui_owned_filter,ELDEN_UI_CSC);
    typefilter_copy(kdbg_typefilter,elden_ui_owned_filter);
    elden_ui_begin_time=mach_absolute_time();
    os_atomic_store(&elden_ui_active,true,release);
    commpage_update_kdebug_state();
    uint64_t interval=0;nanoseconds_to_absolutetime(30ull*NSEC_PER_SEC,&interval);
    thread_call_enter_delayed(elden_ui_stop_call,elden_ui_begin_time+interval);
    ktrace_unlock();
    return 1;
}
static void elden_ui_dispatch_note(struct proc *p,uint32_t code,uint64_t arg1,uint64_t arg2) {
    if(!os_atomic_load(&elden_ui_active,acquire) || kdebug_enable ||
        os_atomic_load(&elden_ui_filter_revoked,acquire) ||
        !elden_ui_is_touch(code,arg1) || !p)return;
    const char *name=proc_best_name(p);
    unsigned target;
    if(!strcmp(name,"SpringBoard"))target=0;
    else if(!strcmp(name,"Preferences"))target=1;
    else return;
    struct elden_ui_event event={.time=mach_absolute_time(),.tid=thread_tid(current_thread()),
        .unique=proc_uniqueid(p),.arg1=arg1,.arg2=arg2,.pid=proc_pid(p),.target=target};
    lck_spin_lock_grp(&elden_ui_lock,&kdebug_lck_grp);
    if(os_atomic_load(&elden_ui_active,relaxed))elden_ui_record(&elden_ui_capture,&event);
    lck_spin_unlock(&elden_ui_lock);
}
