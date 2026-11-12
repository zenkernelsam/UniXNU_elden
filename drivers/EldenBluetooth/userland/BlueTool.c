/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Adapted from QcaBluetooth userland/BlueTool.c: XPC bootstrap only. */
#include "BootReply.h"
#include <dispatch/dispatch.h>
#include <errno.h>
#include <os/log.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include <xpc/xpc.h>
extern xpc_connection_t elden_xpc_listener(const char *,dispatch_queue_t,uint64_t)
    __asm__("_xpc_connection_create_mach_service");
static os_log_t logger;
static void handle(xpc_object_t message){
    if(xpc_get_type(message)!=XPC_TYPE_DICTIONARY)return;
    xpc_object_t reply=xpc_dictionary_create_reply(message);if(!reply)return;
    const char *id=xpc_dictionary_get_string(message,"kMsgId");
    xpc_object_t args=xpc_dictionary_get_value(message,"kMsgArgs");
    const char *script=args&&xpc_get_type(args)==XPC_TYPE_DICTIONARY?xpc_dictionary_get_string(args,"script"):NULL;
    xpc_object_t requested=args&&xpc_get_type(args)==XPC_TYPE_DICTIONARY?xpc_dictionary_get_value(args,"btaddress"):NULL;
    const char *address=requested&&xpc_get_type(requested)==XPC_TYPE_STRING?xpc_string_get_string_ptr(requested):NULL;
    EldenBtBootStatus status={0};size_t bytes=sizeof(status);
    int error=sysctlbyname("kern.elden_bt_bootstrap",&status,&bytes,NULL,0)?errno:0;
    EldenBtBootReply result=elden_bt_boot_reply(id,script,address,&status,bytes,error);
    if(requested&&!address)result=(EldenBtBootReply){EINVAL,false};
    xpc_dictionary_set_int64(reply,"result",result.error?1:0);
    if(!result.error&&result.identity){
        char firmware[48];snprintf(firmware,sizeof(firmware),"WCN7861 patch %04x",status.patch);
        xpc_dictionary_set_int64(reply,"manufacturer",status.manufacturer);
        xpc_dictionary_set_string(reply,"firmware",firmware);
    }
    if(id&&!strcmp(id,"RunCommand"))xpc_dictionary_set_string(reply,"output","Unsupported controller command");
    os_log(logger,"script %{public}s state %u result %d",script?script:"missing",status.state,result.error);
    xpc_connection_t peer=xpc_dictionary_get_remote_connection(message);
    if(peer)xpc_connection_send_message(peer,reply);
    xpc_release(reply);
}
int main(void){
    logger=os_log_create("org.unixnu.elden.bluetooth","BlueTool");
    dispatch_queue_t queue=dispatch_queue_create("org.unixnu.elden.bluetooth.bootstrap",DISPATCH_QUEUE_SERIAL);
    xpc_connection_t listener=elden_xpc_listener("com.apple.BlueTool",queue,XPC_CONNECTION_MACH_SERVICE_LISTENER);
    if(!listener)return 1;
    xpc_connection_set_event_handler(listener,^(xpc_object_t peer){
        if(xpc_get_type(peer)!=XPC_TYPE_CONNECTION)return;
        xpc_connection_set_target_queue(peer,queue);
        xpc_connection_set_event_handler(peer,^(xpc_object_t event){handle(event);});
        xpc_connection_resume(peer);
    });
    xpc_connection_resume(listener);dispatch_main();
}
