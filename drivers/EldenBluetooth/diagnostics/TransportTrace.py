"""Bounded phase observation of the current native bootstrap and H4 callbacks."""
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[3]
HASHES={'BootstrapStatus.cpp':'d28f627fc522eace30b7bce8bc19caeee875a9dce35a1339aad73eb42f6bdeac',
        'NativeHost.cpp':'646849166bcd935a8c594cff2470a24ed56484f274c15bff1f8eeb9ca64496e3'}
def source(name):
    data=(ROOT/'drivers/EldenBluetooth'/name).read_bytes()
    if hashlib.sha256(data).hexdigest()!=HASHES[name]:raise ValueError('Native transport source changed')
    text=data.decode();edits=[]
    def change(old,new):
        nonlocal text
        if text.count(old)!=1:raise ValueError('Ambiguous trace boundary: '+old)
        text=text.replace(old,new);edits.append((old,new))
    if name=='BootstrapStatus.cpp':
        change('#include <sys/sysctl.h>','#include <sys/sysctl.h>\n#include <IOKit/IOLib.h>')
        change('static Published published;','static Published published;\nstatic unsigned trace_calls;')
        change('    if(req->newptr)return EPERM;','''    unsigned trace=__atomic_add_fetch(&trace_calls,1u,__ATOMIC_RELAXED);
    if(trace<=8)printf("ELDEN_BT_PHASE bootstrap-enter call=%u write=%u\\n",trace,(unsigned)(req->newptr!=0));
    if(req->newptr)return EPERM;''')
        change('        return SYSCTL_OUT(req,&value,sizeof(value));','''        if(trace<=8)printf("ELDEN_BT_PHASE bootstrap-snapshot call=%u state=%u\\n",trace,value.state);
        int result=SYSCTL_OUT(req,&value,sizeof(value));
        if(trace<=8)printf("ELDEN_BT_PHASE bootstrap-return call=%u result=%d\\n",trace,result);
        return result;''')
        change('    return EAGAIN;','    if(trace<=8)printf("ELDEN_BT_PHASE bootstrap-unstable call=%u\\n",trace);\n    return EAGAIN;')
    else:
        change('static unsigned host_public_state;','static unsigned host_public_state;\nstatic unsigned trace_connects,trace_sends;')
        change('static errno_t connect(kern_ctl_ref,struct sockaddr_ctl *address,void **info){','''static errno_t connect(kern_ctl_ref,struct sockaddr_ctl *address,void **info){
    unsigned trace=__atomic_add_fetch(&trace_connects,1u,__ATOMIC_RELAXED);
    if(trace<=8)printf("ELDEN_BT_PHASE connect-enter call=%u\\n",trace);''')
        change('    proc_t proc=proc_find(pid);if(proc==nullptr)return ESRCH;','''    if(trace<=8)printf("ELDEN_BT_PHASE connect-identity call=%u pid=%d\\n",trace,pid);
    proc_t proc=proc_find(pid);if(proc==nullptr)return ESRCH;
    if(trace<=8)printf("ELDEN_BT_PHASE connect-reference call=%u\\n",trace);''')
        change('    int rc=pb_bridge_connect(&h->bridge,address->sc_unit,pid);','''    if(trace<=8)printf("ELDEN_BT_PHASE connect-bridge call=%u\\n",trace);
    int rc=pb_bridge_connect(&h->bridge,address->sc_unit,pid);
    if(trace<=8)printf("ELDEN_BT_PHASE connect-result call=%u result=%d\\n",trace,rc);''')
        change('static errno_t send(kern_ctl_ref,u_int32_t unit,void *info,mbuf_t chain,int){','''static errno_t send(kern_ctl_ref,u_int32_t unit,void *info,mbuf_t chain,int){
    unsigned trace=__atomic_add_fetch(&trace_sends,1u,__ATOMIC_RELAXED);
    if(trace<=16)printf("ELDEN_BT_PHASE send-enter call=%u\\n",trace);''')
        change('    for(mbuf_t m=chain;m;m=mbuf_next(m)){','''    if(trace<=16)printf("ELDEN_BT_PHASE send-bridge call=%u\\n",trace);
    for(mbuf_t m=chain;m;m=mbuf_next(m)){''')
        change('    if(failed){pb_bridge_fail(&h->bridge);return EIO;}return 0;','''    if(trace<=16)printf("ELDEN_BT_PHASE send-result call=%u failed=%u\\n",trace,failed);
    if(failed){pb_bridge_fail(&h->bridge);return EIO;}return 0;''')
    # Each scoped edit must be reversible to the byte-locked implementation.
    original=text
    for before,after in reversed(edits):
        if original.count(after)!=1:raise ValueError('Trace delta overlaps')
        original=original.replace(after,before)
    if original.encode()!=data:raise ValueError('Trace changed an unlisted behavior')
    return text.encode()
