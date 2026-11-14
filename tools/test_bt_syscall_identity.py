"""Check the real trace identity/cap helper with failed-output API doubles."""
from pathlib import Path
import argparse,subprocess,json
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();o=a.output.resolve();o.mkdir(parents=True,exist_ok=False);(o/'sys').mkdir();(o/'sys/codesign.h').write_text('')
    prefix=r'''
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef void *proc_t;
static const char *name="bluetoothd";static int fail_name,fail_hash,bad_hash;
int proc_pid(proc_t p){(void)p;return 79;}
void proc_name(int p,char *out,int n){(void)p;if(!fail_name)snprintf(out,n,"%s",name);}
int proc_getcdhash(proc_t p,unsigned char *out){(void)p;if(fail_hash)return 1;const unsigned char h[20]={0xfe,0x7b,0xc1,0x82,0x38,0x6a,0x27,0x66,0xc7,0x57,0x4f,0x8b,0x48,0x41,0x2f,0xf9,0xdf,0x11,0x35,0x64};memcpy(out,h,20);out[0]^=bad_hash;return 0;}
'''
    suffix=r'''
int main(void){proc_t p=(void*)1;assert(!elden_bt_syscall_trace(0));
fail_name=1;assert(!elden_bt_syscall_trace(p));fail_name=0;
name="BlueTool";assert(!elden_bt_syscall_trace(p));name="bluetoothd-extra";assert(!elden_bt_syscall_trace(p));name="bluetoothd";
fail_hash=1;assert(!elden_bt_syscall_trace(p));fail_hash=0;bad_hash=1;assert(!elden_bt_syscall_trace(p));bad_hash=0;
assert(!elden_bt_syscall_trace_count);for(unsigned i=1;i<=32;i++)assert(elden_bt_syscall_trace(p)==i);for(unsigned i=0;i<4;i++)assert(!elden_bt_syscall_trace(p));
puts("PASS failed name/hash outputs, wrong identities, null process and32-event bound");}
'''
    code=o/'test.c';code.write_text(prefix+(ROOT/'drivers/EldenBluetooth/diagnostics/SyscallTrace.inc').read_text()+suffix)
    cmd=['cc','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I',str(o),str(code),'-o',str(o/'test')]
    for n,c in [('compile',cmd),('run',[str(o/'test')])]:
        r=subprocess.run(c,capture_output=True);(o/(n+'.log')).write_bytes(r.stdout+r.stderr);r.check_returncode()
    (o/'result.json').write_text(json.dumps({'passed':True,'actual_helper':True,'sanitizers':['ASan','UBSan'],'hardware':False},indent=2)+'\n');print('PASS syscall trace identity')
if __name__=='__main__':main()
