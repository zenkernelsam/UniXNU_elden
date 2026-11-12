/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "BootReply.h"
#include <errno.h>
#include <string.h>
static int hex(char c){return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;}
static int matches(const char *text,const uint8_t address[6]){
    bool same=true;
    for(unsigned i=0;i<6;i++){
        if(!text[0]||!text[1])return EINVAL;
        int a=hex(text[0]),b=hex(text[1]);if(a<0||b<0)return EINVAL;
        if(((a<<4)|b)!=address[i])same=false;
        if(text[2]!=(i==5?'\0':':'))return EINVAL;
        if(i!=5)text+=3;
    }
    return same?0:EADDRNOTAVAIL;
}
EldenBtBootReply elden_bt_boot_reply(const char *message,const char *script,const char *requested_address,
    const EldenBtBootStatus *s,size_t bytes,int read_error){
    if(!message||strcmp(message,"RunBuiltinScript"))return (EldenBtBootReply){ENOTSUP,false};
    if(!script||(strcmp(script,"boot")&&strcmp(script,"init")&&strcmp(script,"deepsleep")))
        return (EldenBtBootReply){EINVAL,false};
    if(read_error)return (EldenBtBootReply){read_error,false};
    if(!s||bytes!=sizeof(*s)||s->version!=1||s->reserved||s->reserved2||s->state>EldenBtBootStopping)
        return (EldenBtBootReply){EPROTO,false};
    unsigned nonzero=0,not_ff=0;for(unsigned i=0;i<6;i++){nonzero|=s->address[i];not_ff|=s->address[i]^255u;}
    bool identity=s->epoch&&s->manufacturer==0x1d&&nonzero&&not_ff;
    if(requested_address&&(s->state==EldenBtBootReady||s->state==EldenBtBootStopped)){
        int error=identity?matches(requested_address,s->address):EPROTO;
        if(error)return (EldenBtBootReply){error,false};
    }
    if(!strcmp(script,"deepsleep")){
        /* A verified stop needs no further physical action. Active-controller sleep is
         * not implemented by the one-shot shutdown owner and must not pass. */
        if(s->state==EldenBtBootStopped)return (EldenBtBootReply){0,identity};
        return (EldenBtBootReply){ENOTSUP,false};
    }
    if(s->state==EldenBtBootReady)return (EldenBtBootReply){identity?0:EPROTO,identity};
    if(s->state==EldenBtBootFailed)return (EldenBtBootReply){EIO,false};
    if(s->state==EldenBtBootStarting||s->state==EldenBtBootStopping)return (EldenBtBootReply){EAGAIN,false};
    return (EldenBtBootReply){ENETDOWN,false};
}
