"""Insert bounded socket/control phases into exact reconstructed XNU17 sources."""
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[3]
HASHES={'uipc_syscalls.c':'d1bc22b223d51d428e88459bcdb7d70a8616ee0b211151a2317f4b3f201d5149',
        'kern_control.c':'d43862d5fe5a0006154ac139aa2e53880a5b065c12f9f83266d9462e7d09207a'}
def source(name,data):
    if hashlib.sha256(data).hexdigest()!=HASHES[name]:raise ValueError('Unexpected reconstructed syscall source')
    text=data.decode();changes=[]
    def replace(old,new):
        nonlocal text
        if text.count(old)!=1:raise ValueError('Ambiguous syscall trace boundary: '+old[:80])
        text=text.replace(old,new);changes.append((old,new))
    def function(marker,edits):
        a=text.index(marker);b=text.index('\n}\n',a)+2;old=text[a:b];new=old
        for before,after in edits:
            if new.count(before)!=1:raise ValueError('Ambiguous function trace: '+before[:80])
            new=new.replace(before,after)
        replace(old,new)
    header=(ROOT/'drivers/EldenBluetooth/diagnostics/SyscallTrace.inc').read_text()
    marker='static int\nsocket_common(' if name=='uipc_syscalls.c' else 'static int\nctl_ioctl('
    replace(marker,header+'\n'+marker)
    if name=='uipc_syscalls.c':
        function('static int\nsocket_common(', [
          ('\tint fd, error;', '\tint fd, error;\n\tunsigned trace=elden_bt_syscall_trace(p);\n\tif(trace)printf("ELDEN_BT_SYS socket-enter call=%u domain=%d type=%d protocol=%d\\n",trace,domain,type,protocol);'),
          ('\terror = falloc(p, &fp, &fd);', '\tif(trace)printf("ELDEN_BT_SYS socket-falloc call=%u\\n",trace);\n\terror = falloc(p, &fp, &fd);\n\tif(trace)printf("ELDEN_BT_SYS socket-falloc-result call=%u error=%d\\n",trace,error);'),
          ('\terror = socreate(domain, &so, type, protocol);', '\tif(trace)printf("ELDEN_BT_SYS socket-socreate call=%u\\n",trace);\n\t\terror = socreate(domain, &so, type, protocol);\n\t\tif(trace)printf("ELDEN_BT_SYS socket-socreate-result call=%u error=%d\\n",trace,error);'),
          ('\treturn error;\n}', '\tif(trace)printf("ELDEN_BT_SYS socket-return call=%u error=%d\\n",trace,error);\n\treturn error;\n}')])
        function('int\nconnect_nocancel(', [
          ('\tboolean_t dgram;', '\tboolean_t dgram;\n\tunsigned trace=elden_bt_syscall_trace(p);\n\tif(trace)printf("ELDEN_BT_SYS connect-enter call=%u fd=%d\\n",trace,fd);'),
          ('\terror = file_socket(fd, &so);','\terror = file_socket(fd, &so);\n\tif(trace)printf("ELDEN_BT_SYS connect-file-result call=%u error=%d\\n",trace,error);'),
          ('\terror = connectit(so, sa);','\tif(trace)printf("ELDEN_BT_SYS connectit-enter call=%u\\n",trace);\n\terror = connectit(so, sa);\n\tif(trace)printf("ELDEN_BT_SYS connectit-return call=%u error=%d\\n",trace,error);'),
          ('\tfile_drop(fd);\n\treturn error;','\tfile_drop(fd);\n\tif(trace)printf("ELDEN_BT_SYS connect-return call=%u error=%d\\n",trace,error);\n\treturn error;')])
    else:
        function('static int\nctl_ioctl(', [
          ('\tint     error = ENOTSUP;', '\tint     error = ENOTSUP;\n\tunsigned trace=elden_bt_syscall_trace(p);\n\tif(trace)printf("ELDEN_BT_SYS ctl-ioctl-enter call=%u cmd=%lx\\n",trace,cmd);'),
          ('\t\tlck_mtx_lock(&ctl_mtx);\n\t\tkctl = ctl_find_by_name(ctl_info.ctl_name);\n\t\tlck_mtx_unlock(&ctl_mtx);', '\t\tif(trace)printf("ELDEN_BT_SYS ctl-find-enter call=%u\\n",trace);\n\t\tlck_mtx_lock(&ctl_mtx);\n\t\tkctl = ctl_find_by_name(ctl_info.ctl_name);\n\t\tlck_mtx_unlock(&ctl_mtx);\n\t\tif(trace)printf("ELDEN_BT_SYS ctl-find-return call=%u found=%u\\n",trace,kctl!=NULL);'),
          ('\treturn error;\n}', '\tif(trace)printf("ELDEN_BT_SYS ctl-ioctl-return call=%u error=%d\\n",trace,error);\n\treturn error;\n}')])
        # The same helper is defined after ctl_connect; its forward declaration
        # is intentionally not needed because this trace targets the ioctl only.
    check=text
    for old,new in reversed(changes):
        if check.count(new)!=1:raise ValueError('Overlapping trace edits')
        check=check.replace(new,old)
    if check.encode()!=data:raise ValueError('Undeclared syscall change')
    return text.encode()
