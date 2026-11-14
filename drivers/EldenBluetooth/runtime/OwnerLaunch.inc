/* SPDX-License-Identifier: MIT */
/* Same observed RAM-overlay volume failure as the existing IconServices
 * owner exception. Only our exact signed helper is admitted; MAC/signature
 * errors and subsequent executable-page checks are not changed. */
extern int elden_bt_runtime_ready(void);
static int elden_bt_owner_launch(proc_t p,struct image_params *imgp,int error,os_reason_t reason)
{
    if(!p || !imgp || strcmp(proc_best_name(p),"BlueTool") ||
       error!=EPERM || !reason || reason->osr_namespace!=OS_REASON_CODESIGNING ||
       reason->osr_code!=CODESIGNING_EXIT_REASON_LAUNCH_CONSTRAINT_VIOLATION ||
       imgp->ip_mac_return || !imgp->ip_vp || !imgp->ip_px_sa ||
       ((struct _posix_spawnattr *)imgp->ip_px_sa)->psa_launch_type!=1 ||
       (imgp->ip_csflags&(CS_VALID|CS_SIGNED))!=(CS_VALID|CS_SIGNED) ||
       !elden_bt_runtime_ready())return 0;
    int mode=0;PE_parse_boot_argn("elden-wcn-power",&mode,sizeof(mode));
    if(mode!=4)return 0;
    mount_t mount=vnode_mount(imgp->ip_vp);
    if(!mount || vnode_vtype(imgp->ip_vp)!=VREG ||
       strcmp(vfs_statfs(mount)->f_fstypename,"bindfs") ||
       (vfs_flags(mount)&(MNT_RDONLY|MNT_UNION))!=(MNT_RDONLY|MNT_UNION))return 0;
    char path[MAXPATHLEN];int length=sizeof(path);
    if(vn_getpath(imgp->ip_vp,path,&length) || strcmp(path,"/usr/sbin/BlueTool"))return 0;
    char *message=exit_reason_get_string_desc(reason);
    if(!message || strnlen(message,1024)>=1024 ||
       !strnstr(message,"c[8]p[1]m[1]e[6], (Constraint not matched)",1024))return 0;
    static const unsigned char expected[20]={0xe8,0x8b,0x9a,0xee,0x56,0x8d,0x04,0x3b,0xaf,0x7a,0x20,0xca,0xf4,0x69,0x6c,0x05,0x1b,0x2c,0xa8,0x02};
    unsigned char actual[20];
    if(proc_getcdhash(p,actual) || memcmp(actual,expected,sizeof(expected)))return 0;
    printf("ELDEN_BT_OWNER_LAUNCH accepted pid=%d exact-cdhash=1 RAM-overlay=1 category6-volume-only\n",proc_pid(p));
    return 1;
}
