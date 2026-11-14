// SPDX-License-Identifier: MIT
#include "EldenIconCpuPaths.h"
static volatile unsigned elden_icon_cpu_ready;
extern char *proc_best_name(proc_t);
static uint64_t elden_icon_cpu_processes[64];
static unsigned elden_icon_cpu_process_count;
static void elden_icon_cpu_record(proc_t p)
{
    unsigned slot=__atomic_fetch_add(&elden_icon_cpu_process_count,1u,__ATOMIC_RELAXED);
    if(slot<64)__atomic_store_n(&elden_icon_cpu_processes[slot],proc_uniqueid(p),__ATOMIC_RELEASE);
    printf("ELDEN_ICON_CPU selected pid=%d unique=%llu name=%s\n",proc_pid(p),(unsigned long long)proc_uniqueid(p),proc_best_name(p));
}
static int elden_icon_cpu_process_selected(proc_t p)
{
    if(!p || !__atomic_load_n(&elden_icon_cpu_ready,__ATOMIC_ACQUIRE))return 0;
    uint64_t identity=proc_uniqueid(p);
    if(!identity)return 0;
    for(unsigned i=0;i<64;++i)if(__atomic_load_n(&elden_icon_cpu_processes[i],__ATOMIC_ACQUIRE)==identity)return 1;
    return 0;
}
int elden_icon_cpu_observe_process(proc_t p)
{
    return elden_icon_cpu_process_selected(p);
}
static void elden_icon_cpu_progress(proc_t p,const char *stage,int result,uint64_t value)
{
    static unsigned lines;
    if(!elden_icon_cpu_process_selected(p) || __atomic_add_fetch(&lines,1u,__ATOMIC_RELAXED)>128) return;
    printf("ELDEN_ICON_PROGRESS pid=%d unique=%llu stage=%s result=%d value=%llx flags=%llx\n",
        proc_pid(p),(unsigned long long)proc_uniqueid(p),stage,result,(unsigned long long)value,
        (unsigned long long)proc_getcsflags(p));
}
static int
elden_icon_cpu_selected(const char *original)
{
    return __atomic_load_n(&elden_icon_cpu_ready,__ATOMIC_ACQUIRE) && !strcmp(original,ELDEN_ICON_ORIGINAL_PATH);
}
int elden_icon_cpu_exit_selected(proc_t p)
{
    static unsigned count;
    /* Diagnostic only: expose the original exit reason before changing policy.
     * Bound independently from IconServices and require the exact native daemon. */
    static unsigned bt_count;
    if (p && !strcmp(proc_best_name(p), "bluetoothd")) {
        static const unsigned char bt_expected[20] = {0xfe,0x7b,0xc1,0x82,0x38,0x6a,0x27,0x66,0xc7,0x57,0x4f,0x8b,0x48,0x41,0x2f,0xf9,0xdf,0x11,0x35,0x64};
        unsigned char bt_actual[20];
        if (!proc_getcdhash(p, bt_actual) && !memcmp(bt_actual, bt_expected, sizeof(bt_expected)))
            return __atomic_add_fetch(&bt_count, 1u, __ATOMIC_RELAXED) <= 16;
    }

    if (!elden_icon_cpu_process_selected(p)) return 0;
    return __atomic_add_fetch(&count,1u,__ATOMIC_RELAXED)<=16;
}
/* The owner-built clone lives in the verified RAM bindfs, not an Apple sealed
 * volume. Admit only its observed category-6 volume failure. All other launch
 * errors and all subsequent code-signature checks keep their original result. */
static int elden_icon_cpu_owner_launch(proc_t p,struct image_params *imgp,int error,os_reason_t reason)
{
    if(error!=EPERM || !reason || reason->osr_namespace!=OS_REASON_CODESIGNING ||
        reason->osr_code!=CODESIGNING_EXIT_REASON_LAUNCH_CONSTRAINT_VIOLATION ||
        !elden_icon_cpu_process_selected(p) || imgp->ip_mac_return || !imgp->ip_vp ||
        !imgp->ip_px_sa || ((struct _posix_spawnattr *)imgp->ip_px_sa)->psa_launch_type!=1 ||
        kauth_cred_getuid(vfs_context_ucred(imgp->ip_vfs_context))!=276) return 0;
    mount_t mount=vnode_mount(imgp->ip_vp);
    if(vnode_vtype(imgp->ip_vp)!=VREG || strcmp(vfs_statfs(mount)->f_fstypename,"bindfs") ||
        (vfs_flags(mount)&(MNT_RDONLY|MNT_UNION))!=(MNT_RDONLY|MNT_UNION)) return 0;
    char *message=exit_reason_get_string_desc(reason);
    if(!message || strnlen(message,1024)>=1024 ||
        !strnstr(message,"c[8]p[1]m[1]e[6], (Constraint not matched)",1024)) return 0;
    static const unsigned char expected[20]={0x15,0x2b,0xdc,0xb5,0xa3,0xa3,0xef,0x6b,0x76,0xd1,0x16,0x15,0xb0,0x30,0x54,0x17,0x64,0xed,0x4b,0x01};
    unsigned char actual[20];
    if(proc_getcdhash(p,actual) || memcmp(actual,expected,sizeof(expected))) return 0;
    printf("ELDEN_ICON_OWNER_LAUNCH accepted pid=%d unique=%llu exact-cdhash=1 RAM-overlay=1 category6-volume-only\n",proc_pid(p),(unsigned long long)proc_uniqueid(p));
    return 1;
}
void elden_icon_cpu_user_marker(proc_t p,uint32_t code,uint64_t status,uint64_t mode,uint64_t version,uint64_t magic)
{
    if(code==ELDEN_ICON_GLYPH_TRACE_CODE){
        static unsigned glyph_reported;
        if(!elden_icon_cpu_process_selected(p)||magic!=ELDEN_ICON_TRACE_MAGIC||version!=1||status<1||status>4||
           kauth_cred_getuid(vfs_context_ucred(vfs_context_current()))!=276)return;
        if(__atomic_add_fetch(&glyph_reported,1u,__ATOMIC_RELAXED)>64)return;
        printf("ELDEN_GLYPH pid=%d stage=%llu data=%llx\n",proc_pid(p),(unsigned long long)status,(unsigned long long)mode);
        return;
    }
    static unsigned reported,rejected;
    if(code!=ELDEN_ICON_TRACE_CODE || !elden_icon_cpu_process_selected(p))return;
    unsigned uid=kauth_cred_getuid(vfs_context_ucred(vfs_context_current()));
    if(magic!=ELDEN_ICON_TRACE_MAGIC || mode!=0 || version!=1 || status<1 || status>10 || uid!=276){
        if(__atomic_add_fetch(&rejected,1u,__ATOMIC_RELAXED)<=8)
            printf("ELDEN_ICON_MARKER rejected pid=%d uid=%u status=%llu mode=%llu version=%llu magic=%llx\n",proc_pid(p),uid,(unsigned long long)status,(unsigned long long)mode,(unsigned long long)version,(unsigned long long)magic);
        return;
    }
    if (__atomic_add_fetch(&reported,1u,__ATOMIC_RELAXED)>64) return;
    static const char *const names[]={"loaded","installed","refused-process","refused-enable",
        "refused-load","refused-uuid","refused-abi","refused-implementation","refused-race","superseded"};
    printf("ELDEN_ICON_CPU %s pid=%d via=kdebug-marker\n",names[status-1],proc_pid(p));
}
