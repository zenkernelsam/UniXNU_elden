/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Included after the qualified IconServices staging helpers in kern_exec.c. */
#include "EldenBtRuntimePayload.h"
#include <pexpert/device_tree.h>
static volatile int elden_bt_runtime_status=-1;
int elden_bt_runtime_ready(void){return __atomic_load_n(&elden_bt_runtime_status,__ATOMIC_ACQUIRE)==0;}
static void elden_bt_runtime_prepare(proc_t p,user_addr_t scratch){
    int mode=0;PE_parse_boot_argn("elden-wcn-power",&mode,sizeof(mode));
    if(mode!=4)return;
    DTEntry chosen;const void *table=NULL;unsigned table_bytes=0;
    if(SecureDTLookupEntry(NULL,"/chosen",&chosen)!=kSuccess||
       SecureDTGetProperty(chosen,"text-fixups",&table,&table_bytes)!=kSuccess||
       table_bytes!=sizeof(elden_bt_runtime_fixups)||memcmp(table,elden_bt_runtime_fixups,table_bytes)){
        __atomic_store_n(&elden_bt_runtime_status,EINVAL,__ATOMIC_RELEASE);return;
    }
    if(native_owner_data_status||native_owner_data_readonly||native_owner_data_seed_size!=UINT64_C(536870912)){
        __atomic_store_n(&elden_bt_runtime_status,EINVAL,__ATOMIC_RELEASE);return;
    }
    static const char root[]="/private/var/root/elden-bt-runtime";
    static const char source[]="/private/var/root/elden-bt-runtime/sbin";
    static const char original[]="/usr/sbin/bluetoothd",point[]="/usr/sbin";
    static const char helper[]="/private/var/root/elden-bt-runtime/sbin/BlueTool";
    vfs_context_t ctx=vfs_context_current();vnode_t data=NULLVP,lower=NULLVP;
    mount_t data_mount=NULL;volatile int stage=0,status=-1;bool mounted=false;
    int error=vnode_lookup("/private/var",VNODE_LOOKUP_NOFOLLOW,&data,ctx);
    if(error)goto done;
    data_mount=vnode_mount(data);
    if(vnode_vtype(data)!=VDIR||(vfs_flags(data_mount)&MNT_RDONLY)||strcmp(vfs_statfs(data_mount)->f_fstypename,"apfs")){error=EINVAL;goto done;}
    error=vnode_lookup(original,VNODE_LOOKUP_NOFOLLOW,&lower,ctx);if(error)goto done;
    if(vnode_vtype(lower)!=VREG||!(vfs_flags(vnode_mount(lower))&MNT_RDONLY)){error=EINVAL;goto done;}
    vnode_put(lower);lower=NULLVP;
    if((error=elden_icon_cpu_directory(p,scratch,root,data_mount,true))||
       (error=elden_icon_cpu_directory(p,scratch,source,data_mount,true)))goto done;
    if((error=elden_icon_cpu_file(helper,elden_bt_runtime_payload,sizeof(elden_bt_runtime_payload),0555,data_mount)))goto done;
    native_gpu_addon_mount(p,scratch,0xb00,source,sizeof(source),point,sizeof(point),original,"/usr/sbin/BlueTool",&stage,&status);
    mounted=stage>=3;error=status;
    if(!error&&stage!=5)error=EINVAL;
    if(!error)error=elden_icon_cpu_visible("/usr/sbin/BlueTool",elden_bt_runtime_payload,sizeof(elden_bt_runtime_payload));
done:
    if(lower)vnode_put(lower);if(data)vnode_put(data);
    if(error&&mounted){
        int rollback=copyout(point,scratch,sizeof(point));int32_t result=0;
        struct unmount_args request={.path=scratch,.flags=0};if(!rollback)rollback=unmount(p,&request,&result);
        printf("ELDEN_BT_RUNTIME rollback=%d\n",rollback);
    }
    __atomic_store_n(&elden_bt_runtime_status,error,__ATOMIC_RELEASE);
    printf("ELDEN_BT_RUNTIME prepared=%u error=%d mount-stage=%d original-job=1\n",error==0,error,stage);
}
