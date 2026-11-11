/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/uio.h>
#include <sys/fcntl.h>
#include <sys/kauth.h>
#include <IOKit/IOLib.h>
#include "BluetoothFirmware.h"
extern volatile int native_owner_data_status;
// TB322FC WCN7861 firmware list from Armada8d85484e; exact names only.
static bool firmware_name(const char *name) {
    static const char *const names[]={
        "qca/brhbtfw20.tlv", "qca/lenovo/tb322fc/brhbtnv20.bin",
        "qca/brhperifw20.tlv", "qca/brhperinv20.bin", "qca/tmel_peach_20.elf"
    };
    if(!name)return false;
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);++i)if(!strcmp(name,names[i]))return true;
    return false;
}
int sheng_bt_firmware_read(const char *name,uint8_t **data,size_t *size) {
    if(!data || !size)return EINVAL;
    *data=NULL;*size=0;
    if(!firmware_name(name))return ENOENT;
    if(native_owner_data_status!=0)return EAGAIN;
    char path[160];snprintf(path,sizeof(path),"%s/%s",SHENG_BT_FIRMWARE_ROOT,name+4);
    vfs_context_t ctx=vfs_context_kernel();vnode_t vp=NULL;
    int error=vnode_open(path,FREAD,0,0,&vp,ctx);uint8_t *buffer=NULL;size_t length=0;
    if(!error) {
        struct vnode_attr va;VATTR_INIT(&va);VATTR_WANTED(&va,va_data_size);
        error=vnode_getattr(vp,&va,ctx);
        if(!error && (vnode_vtype(vp)!=VREG || !va.va_data_size || va.va_data_size>4u*1024*1024))error=EINVAL;
        if(!error) {
            length=(size_t)va.va_data_size;buffer=IOMallocData(length);
            if(!buffer)error=ENOMEM;
            else {
                int resid=0;
                error=vn_rdwr(UIO_READ,vp,(caddr_t)buffer,(int)length,0,UIO_SYSSPACE,IO_NOAUTH,
                    vfs_context_ucred(ctx),&resid,vfs_context_proc(ctx));
                if(!error && resid)error=EIO;
            }
        }
    }
    if(vp)vnode_close(vp,FREAD,ctx);
    if(error){if(buffer)IOFreeData(buffer,length);return error;}
    *data=buffer;*size=length;return 0;
}
void sheng_bt_firmware_release(uint8_t *data,size_t size) {if(data)IOFreeData(data,size);}
