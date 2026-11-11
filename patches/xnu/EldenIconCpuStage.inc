// SPDX-License-Identifier: MIT
// Exact RAM files exposed through the existing readonly bindfs union mechanism.
#include "EldenIconCpuPayload.h"
static int
elden_icon_cpu_directory(proc_t p,user_addr_t scratch,const char *path,mount_t mount,bool fresh)
{
    int error=copyout(path,scratch,strlen(path)+1);int32_t result=0;
    if (error) return error;
    struct mkdir_args md={.path=scratch,.mode=0755};error=mkdir(p,&md,&result);
    if (error && (error!=EEXIST || fresh)) return error;
    vnode_t vp=NULLVP;vfs_context_t ctx=vfs_context_current();
    error=vnode_lookup(path,VNODE_LOOKUP_NOFOLLOW,&vp,ctx);
    if (!error && (vnode_vtype(vp)!=VDIR || vnode_mount(vp)!=mount)) error=EINVAL;
    if (vp) vnode_put(vp);
    return error;
}
static int
elden_icon_cpu_file(const char *path,const uint8_t *bytes,size_t length,uint32_t mode,mount_t mount)
{
    if (!length || length>131072) return EINVAL;
    vfs_context_t ctx=vfs_context_current();vnode_t vp=NULLVP;uint8_t *check=NULL;
    int error=vnode_open(path,O_CREAT|O_EXCL|O_NOFOLLOW|FREAD|FWRITE,0600,0,&vp,ctx),resid=0;
    if (error) return error;
    if (vnode_vtype(vp)!=VREG || vnode_mount(vp)!=mount) {error=EINVAL;goto done;}
    error=vn_rdwr(UIO_WRITE,vp,(caddr_t)(uintptr_t)bytes,(int)length,0,UIO_SYSSPACE,
        IO_NOAUTH|IO_UNIT,vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
    if (error || resid) {if(!error)error=EIO;goto done;}
    struct vnode_attr va;
    VATTR_INIT(&va);VATTR_SET(&va,va_mode,mode);VATTR_SET(&va,va_uid,0);VATTR_SET(&va,va_gid,0);
    if ((error=vnode_setattr(vp,&va,ctx)) || (error=VNOP_FSYNC(vp,MNT_WAIT,ctx))) goto done;
    VATTR_INIT(&va);VATTR_WANTED(&va,va_data_size);VATTR_WANTED(&va,va_mode);
    VATTR_WANTED(&va,va_uid);VATTR_WANTED(&va,va_gid);
    if ((error=vnode_getattr(vp,&va,ctx))) goto done;
    if (va.va_data_size!=length || (va.va_mode&07777)!=mode || va.va_uid || va.va_gid) {error=EINVAL;goto done;}
    check=kalloc_data(length,Z_WAITOK);if(!check){error=ENOMEM;goto done;}
    error=vn_rdwr(UIO_READ,vp,(caddr_t)check,(int)length,0,UIO_SYSSPACE,IO_NOAUTH,
        vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
    if (!error && (resid || memcmp(check,bytes,length))) error=EIO;
done:
    if (check) kfree_data(check,length);
    if (vp) {int e=vnode_close(vp,FREAD|FWRITE,ctx);if(!error)error=e;}
    return error;
}
static int
elden_icon_cpu_visible(const char *path,const uint8_t *bytes,size_t length)
{
    vfs_context_t ctx=vfs_context_current();vnode_t vp=NULLVP;
    int error=vnode_open(path,FREAD|O_NOFOLLOW,0,0,&vp,ctx);
    if (error) return error;
    mount_t mount=vnode_mount(vp);
    if (vnode_vtype(vp)!=VREG || strcmp(vfs_statfs(mount)->f_fstypename,"bindfs") ||
        (vfs_flags(mount)&(MNT_RDONLY|MNT_UNION))!=(MNT_RDONLY|MNT_UNION) ||
        !native_gpu_addon_equal(vp,bytes,length,0555,ctx)) error=EIO;
    if (!error) {
        struct vnode_attr va;VATTR_INIT(&va);VATTR_WANTED(&va,va_uid);VATTR_WANTED(&va,va_gid);
        error=vnode_getattr(vp,&va,ctx);if(!error&&(va.va_uid||va.va_gid))error=EINVAL;
    }
    int closed=vnode_close(vp,FREAD,ctx);return error?error:closed;
}
static void
elden_icon_cpu_prepare(proc_t p,user_addr_t scratch)
{
    int enabled=0;
    if (!PE_parse_boot_argn("elden-icon-cpu",&enabled,sizeof(enabled)) || enabled!=1) return;
    if (native_owner_data_status || native_owner_data_readonly || native_owner_data_seed_size!=UINT64_C(536870912)) {
        printf("ELDEN_ICON_CPU stage refused: RAM Data identity\n");return;
    }
    static const char source[]=ELDEN_ICON_SOURCE_DIR,point[]=ELDEN_ICON_MOUNT_DIR;
    static const char lower[]="/System/Library/CoreServices/SystemVersion.plist";
    vfs_context_t ctx=vfs_context_current();vnode_t data=NULLVP,probe=NULLVP;
    volatile int mount_stage=0,mount_status=-1;bool mounted=false;
    int error=vnode_lookup("/private/var",VNODE_LOOKUP_NOFOLLOW,&data,ctx);
    if (error) goto done;
    mount_t mount=vnode_mount(data);
    if (vnode_vtype(data)!=VDIR || (vfs_flags(mount)&MNT_RDONLY) || strcmp(vfs_statfs(mount)->f_fstypename,"apfs")) {error=EINVAL;goto done;}
    error=vnode_lookup(lower,VNODE_LOOKUP_NOFOLLOW,&probe,ctx);
    if (error) goto done;
    if (!(vfs_flags(vnode_mount(probe))&MNT_RDONLY)) error=EINVAL;
    vnode_put(probe);probe=NULLVP;if(error)goto done;
    if ((error=elden_icon_cpu_directory(p,scratch,ELDEN_ICON_ROOT_DIR,mount,true)) ||
        (error=elden_icon_cpu_directory(p,scratch,source,mount,true))) goto done;
    if ((error=elden_icon_cpu_file(ELDEN_ICON_LIBRARY_SOURCE,elden_icon_cpu_payload,sizeof(elden_icon_cpu_payload),0555,mount)) ||
        (error=elden_icon_cpu_file(ELDEN_ICON_PROGRAM_SOURCE,elden_icon_agent_payload,sizeof(elden_icon_agent_payload),0555,mount))) goto done;
    native_gpu_addon_mount(p,scratch,0xb00,source,sizeof(source),point,sizeof(point),
        lower,ELDEN_ICON_ORIGINAL_PATH,&mount_stage,&mount_status);
    mounted=mount_stage>=3;error=mount_status;
    if (!error && mount_stage!=5)error=EINVAL;
    if (!error)error=elden_icon_cpu_visible(ELDEN_ICON_ORIGINAL_PATH,elden_icon_agent_payload,sizeof(elden_icon_agent_payload));
    if (!error)error=elden_icon_cpu_visible(ELDEN_ICON_LIBRARY_PATH,elden_icon_cpu_payload,sizeof(elden_icon_cpu_payload));
    if (!error)__atomic_store_n(&elden_icon_cpu_ready,1u,__ATOMIC_RELEASE);
done:
    if (probe)vnode_put(probe);
    if (data)vnode_put(data);
    if (error && mounted) {
        int rollback=copyout(point,scratch,sizeof(point));int32_t result=0;
        struct unmount_args request={.path=scratch,.flags=0};
        if (!rollback)rollback=unmount(p,&request,&result);
        printf("ELDEN_ICON_CPU overlay rollback=%d\n",rollback);
    }
    printf("ELDEN_ICON_CPU stage=%s error=%d overlay-stage=%d library=%lu agent=%lu normal-dependency=1\n",
        error?"refused":"ready",error,mount_stage,(unsigned long)sizeof(elden_icon_cpu_payload),(unsigned long)sizeof(elden_icon_agent_payload));
}
