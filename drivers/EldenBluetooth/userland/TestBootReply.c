/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "BootReply.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
static unsigned cases;
static void check(const char *id,const char *script,const EldenBtBootStatus *s,size_t n,int read_error,int expected,bool identity){
    EldenBtBootReply r=elden_bt_boot_reply(id,script,NULL,s,n,read_error);
    assert(r.error==expected&&r.identity==identity);cases++;
}
int main(void){
    EldenBtBootStatus s={1,EldenBtBootReady,1,29,0x1234,0,{0xc2,1,2,3,4,5},0};
    check("RunBuiltinScript","boot",&s,sizeof(s),0,0,true);
    check("RunBuiltinScript","init",&s,sizeof(s),0,0,true);
    check("RunBuiltinScript","deepsleep",&s,sizeof(s),0,ENOTSUP,false);
    for(unsigned state=0;state<=EldenBtBootStopping;state++){
        s.state=state;
        const int errors[]={ENETDOWN,EAGAIN,0,ENETDOWN,EIO,EAGAIN};
        check("RunBuiltinScript","boot",&s,sizeof(s),0,errors[state],state==EldenBtBootReady);
        check("RunBuiltinScript","deepsleep",&s,sizeof(s),0,state==EldenBtBootStopped?0:ENOTSUP,state==EldenBtBootStopped);
    }
    s.state=EldenBtBootReady;
    for(unsigned n=0;n<=sizeof(s)+1;n++)if(n!=sizeof(s))check("RunBuiltinScript","init",&s,n,0,EPROTO,false);
    check("RunBuiltinScript","boot",NULL,sizeof(s),0,EPROTO,false);
    check("RunBuiltinScript","boot",&s,sizeof(s),EPERM,EPERM,false);
    check("RunBuiltinScript","boot",&s,sizeof(s),ENOENT,ENOENT,false);
    s.version=2;check("RunBuiltinScript","boot",&s,sizeof(s),0,EPROTO,false);s.version=1;
    s.epoch=0;check("RunBuiltinScript","boot",&s,sizeof(s),0,EPROTO,false);s.epoch=1;
    s.manufacturer=15;check("RunBuiltinScript","boot",&s,sizeof(s),0,EPROTO,false);s.manufacturer=29;
    s.reserved=1;check("RunBuiltinScript","boot",&s,sizeof(s),0,EPROTO,false);s.reserved=0;
    s.state=99;check("RunBuiltinScript","boot",&s,sizeof(s),0,EPROTO,false);s.state=EldenBtBootReady;
    check(NULL,"boot",&s,sizeof(s),0,ENOTSUP,false);
    check("RunCommand","boot",&s,sizeof(s),0,ENOTSUP,false);
    check("RunBuiltinScript",NULL,&s,sizeof(s),0,EINVAL,false);
    check("RunBuiltinScript","",&s,sizeof(s),0,EINVAL,false);
    check("RunBuiltinScript","arbitrary-command",&s,sizeof(s),0,EINVAL,false);
    s.state=EldenBtBootUnavailable;s.epoch=0;s.manufacturer=0;s.patch=0;
    check("RunBuiltinScript","deepsleep",&s,sizeof(s),0,ENOTSUP,false);
    s.state=EldenBtBootStopped;
    check("RunBuiltinScript","deepsleep",&s,sizeof(s),0,0,false);
    s=(EldenBtBootStatus){1,EldenBtBootReady,2,29,0x1234,0,{0xc2,1,2,3,4,5},0};
    const char *addresses[]={"C2:01:02:03:04:05","c2:01:02:03:04:05","C2:01:02:03:04:06","","C","C2:01","C2-01-02-03-04-05","C2:01:02:03:04:05x","G2:01:02:03:04:05"};
    const int errors[]={0,0,EADDRNOTAVAIL,EINVAL,EINVAL,EINVAL,EINVAL,EINVAL,EINVAL};
    for(unsigned i=0;i<sizeof(addresses)/sizeof(addresses[0]);i++){
        EldenBtBootReply r=elden_bt_boot_reply("RunBuiltinScript","boot",addresses[i],&s,sizeof(s),0);
        assert(r.error==errors[i]&&r.identity==(errors[i]==0));cases++;
    }
    s.reserved2=1;check("RunBuiltinScript","boot",&s,sizeof(s),0,EPROTO,false);
    s.reserved2=0;s.state=EldenBtBootStarting;s.epoch=0;s.manufacturer=0;
    assert(elden_bt_boot_reply("RunBuiltinScript","boot",addresses[0],&s,sizeof(s),0).error==EAGAIN);cases++;
    printf("BootReply: %u readiness/protocol cases passed\n",cases);
}
