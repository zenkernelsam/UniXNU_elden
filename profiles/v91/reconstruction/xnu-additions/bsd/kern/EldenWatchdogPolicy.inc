// SPDX-License-Identifier: MIT
// Adapted from UniXNU's BootWatchdogPolicy.c: normal launchd overrides on RAM Data.
// Explicit board bring-up switch; no executable, trust, or critical-exit patches.
#include <sys/fcntl.h>
#include <sys/stat.h>
static volatile int elden_watchdog_policy_status = -1;
static int
elden_watchdog_policy(proc_t p, user_addr_t scratch)
{
    int enabled = 0;
    PE_parse_boot_argn("elden-disable-watchdogd", &enabled, sizeof(enabled));
    if (enabled != 1) return 0;
    if (native_owner_data_status != 0 || native_owner_data_readonly != 0 ||
        native_owner_data_seed_size != UINT64_C(536870912)) return EINVAL;
    static const char directory[] = "/private/var/db/com.apple.xpc.launchd";
    static const char path[] = "/private/var/db/com.apple.xpc.launchd/disabled.plist";
    static const char marker[] = "/private/var/db/com.apple.xpc.launchd/disabled.migrated";
    static const char policy_basic[] = "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
        "<key>com.apple.watchdogd</key><true/></dict></plist>\n";
    static const char policy_unported[] = "<?xml version=\"1.0\"?><plist version=\"1.0\"><dict>"
        "<key>com.apple.watchdogd</key><true/>"
        "<key>com.apple.locationd</key><true/>"
        "<key>com.apple.nehelper</key><true/></dict></plist>\n";
    int unported = 0;
    PE_parse_boot_argn("elden-disable-unported-services", &unported, sizeof(unported));
    if (unported != 0 && unported != 1) return EINVAL;
    const char *policy = unported ? policy_unported : policy_basic;
    size_t policy_bytes = strlen(policy);
    if (policy_bytes > 0x600) return EINVAL;

    int error = 0, fd = -1;
    int32_t result = 0;
    if ((error = copyout(directory, scratch, sizeof(directory)))) goto done;
    struct mkdir_args md = {.path=scratch, .mode=0755};
    error = mkdir(p, &md, &result);
    if (error && error != EEXIST) goto done;
    if ((error = copyout(path, scratch, sizeof(path)))) goto done;
    // A fresh RAM seed is required. Never replace a pre-existing policy or follow a link.
    struct open_args op = {.path=scratch, .flags=O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW, .mode=0644};
    if ((error = open(p, &op, &result))) goto done;
    fd = result;
    if ((error = copyout(policy, scratch+0x200, policy_bytes))) goto done;
    struct write_args wr = {.fd=fd, .cbuf=scratch+0x200, .nbyte=policy_bytes};
    user_ssize_t written = 0;
    if ((error = write(p, &wr, &written))) goto done;
    if (written != (user_ssize_t)(policy_bytes)) { error=EIO; goto done; }
    struct fchown_args owner = {.fd=fd, .uid=0, .gid=0};
    if ((error = fchown(p, &owner, &result))) goto done;
    struct fchmod_args mode = {.fd=fd, .mode=0644};
    if ((error = fchmod(p, &mode, &result))) goto done;
    struct fsync_args sync = {.fd=fd};
    if ((error = fsync(p, &sync, &result))) goto done;
    struct stat64 st = {0};
    struct fstat64_args sa = {.fd=fd, .ub=scratch+0x800};
    if ((error=sys_fstat64(p,&sa,&result)) || (error=copyin(sa.ub,&st,sizeof(st)))) goto done;
    if (st.st_uid || st.st_gid || !S_ISREG(st.st_mode) || (st.st_mode&0777)!=0644 ||
        st.st_size != (off_t)(policy_bytes)) error=EINVAL;
done:
    if (fd>=0) { struct close_args cl={.fd=fd}; int e=sys_close(p,&cl,&result); if(!error)error=e; }
    if (!error) {
        error=copyout(marker,scratch,sizeof(marker));
        if (!error) {
            struct open_args mark={.path=scratch,.flags=O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,.mode=0644};
            error=open(p,&mark,&result);
            if(!error){struct close_args cl={.fd=result};error=sys_close(p,&cl,&result);}
        }
    }
    elden_watchdog_policy_status=error;
    printf("ELDEN_BOOT watchdog launch policy status=%d; unported-services=%d (locationd/nehelper)\n",error,unported);
    return error;
}
