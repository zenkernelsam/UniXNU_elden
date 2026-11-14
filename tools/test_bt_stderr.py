"""Compile the actual stderr observer with bounded uio/copyin test doubles."""
from pathlib import Path
import argparse,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
def lp(p):p=p.resolve();return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True)
    prefix='''#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
typedef uintptr_t user_addr_t;typedef uint64_t user_size_t;
struct proc {int bad;};typedef struct proc *proc_t;
struct U {int count,error;uint64_t sizes[12];};typedef struct U *uio_t;
static int copies,prints;static unsigned total;static int badcopy;
char *proc_best_name(proc_t p){return p->bad==1?"other":"bluetoothd";}
int proc_getcdhash(proc_t p,unsigned char *id){unsigned char want[20]={0xfe,0x7b,0xc1,0x82,0x38,0x6a,0x27,0x66,0xc7,0x57,0x4f,0x8b,0x48,0x41,0x2f,0xf9,0xdf,0x11,0x35,0x64};memcpy(id,want,20);if(p->bad==2)id[0]=0;return p->bad==3;}
int proc_pid(proc_t p){(void)p;return 79;}
int uio_iovcnt(uio_t u){return u->count;}
int uio_getiov(uio_t u,int i,user_addr_t *p,user_size_t *n){assert(i<8);*p=123;*n=u->sizes[i];return u->error;}
int copyin(user_addr_t p,void *b,unsigned n){assert(p==123&&n<=128);copies++;total+=n;memset(b,'x',n);return badcopy;}
int capture(const char *fmt,...){(void)fmt;prints++;return 0;}
#define printf capture
'''
    suffix='''
#undef printf
int main(void){struct proc p={0};struct U u={.count=12};for(int i=0;i<12;i++)u.sizes[i]=UINT64_MAX;struct U old=u;
for(int bad=1;bad<=3;bad++){p.bad=bad;elden_bt_stderr_observe(&p,2,&u);}p.bad=0;elden_bt_stderr_observe(&p,1,&u);elden_bt_stderr_observe(0,2,&u);assert(!copies&&!prints);
elden_bt_stderr_observe(&p,2,&u);assert(copies==4&&prints==4&&total==512&&!memcmp(&old,&u,sizeof(u)));
u.error=5;copies=prints=0;total=0;elden_bt_stderr_observe(&p,2,&u);assert(!copies&&prints==4);
u.error=0;u.count=1;u.sizes[0]=4;badcopy=1;elden_bt_stderr_observe(&p,2,&u);assert(copies==1);
for(int i=0;i<30;i++)elden_bt_stderr_observe(&p,2,&u);int n=copies;elden_bt_stderr_observe(&p,2,&u);assert(copies==n&&copies==14);
puts("PASS observer identity,512B/eight-iov bounds,copy failures,unchanged uio,16-call cap");}
'''
    source=a.output/'test.c';source.write_text(prefix+(ROOT/'drivers/EldenBluetooth/userland/StderrObserve.inc').read_text()+suffix,newline='\n')
    cmd=['wsl','-d','Ubuntu-22.04','-u','root','--','cc','-O1','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined',lp(source),'-o',lp(a.output/'test')]
    r=subprocess.run(cmd,capture_output=True);(a.output/'build.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    run=['wsl','-d','Ubuntu-22.04','-u','root','--','env','UBSAN_OPTIONS=halt_on_error=1',cmd[-1]]
    r=subprocess.run(run,capture_output=True);(a.output/'run.log').write_bytes(r.stdout+r.stderr);r.check_returncode();print(r.stdout.decode())
    (a.output/'result.json').write_text(json.dumps({'command':cmd,'run':run,'result':r.stdout.decode(),'hardware':False},indent=2)+'\n')
if __name__=='__main__':main()
