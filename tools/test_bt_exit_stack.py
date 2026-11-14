"""Exercise the actual bounded exit-frame reader with invalid-context tests."""
from pathlib import Path
import argparse,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
def lp(p):p=p.resolve();return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True)
    body=(ROOT/'drivers/EldenBluetooth/diagnostics/ExitStack.inc').read_text();body='\n'.join(l for l in body.splitlines() if not l.startswith('#include'))
    prefix='''#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdarg.h>
typedef void *proc_t;typedef uint64_t user_addr_t;
proc_t current_proc(void){return 0;}
typedef struct {uint64_t pc,lr,fp,r16;unsigned cpsr,is64;} arm_saved_state_t;
static arm_saved_state_t state;static int irq,enabled=1,absent,fail,cycle,reads,logs;static uint64_t addresses[8];
int ml_at_interrupt_context(void){return irq;}int ml_get_interrupts_enabled(void){return enabled;}
void *current_thread(void){return &state;}void *get_user_regs(void *p){(void)p;return absent?0:&state;}
int is_saved_state64(arm_saved_state_t *p){return p->is64;}arm_saved_state_t *saved_state64(arm_saved_state_t *p){return p;}
uint64_t get_saved_state_pc(arm_saved_state_t *p){return p->pc;}uint64_t get_saved_state_lr(arm_saved_state_t *p){return p->lr;}uint64_t get_saved_state_fp(arm_saved_state_t *p){return p->fp;}uint64_t get_saved_state_reg(arm_saved_state_t *p,int n){assert(n==16);return p->r16;}
int proc_pid(proc_t p){(void)p;return 79;}int logline(const char *f,...){(void)f;logs++;return 0;}
int copyin(user_addr_t p,void *out,unsigned size){assert(size==16&&reads<8&&p>=4096&&p<(UINT64_C(1)<<39)-16&&!(p&15));addresses[reads++]=p;uint64_t *v=out;v[0]=cycle?p:p+16;v[1]=0x100001000;return fail;}
#define printf logline
'''
    suffix='''
#undef printf
int main(void){state=(arm_saved_state_t){0x1000,0x2000,4096,1,0,1};elden_bt_exit_stack((proc_t)1);assert(!reads&&!logs);
for(int c=0;c<4;c++){irq=c==0;enabled=c!=1;absent=c==2;state.cpsr=c==3?4:0;elden_bt_exit_stack(0);}assert(!reads&&!logs);
irq=absent=0;enabled=1;state.cpsr=0;state.is64=0;elden_bt_exit_stack(0);assert(!reads&&!logs);state.is64=1;
state.fp=4097;elden_bt_exit_stack(0);assert(!reads&&logs==1);
state.fp=4096;fail=5;logs=0;elden_bt_exit_stack(0);assert(reads==1&&logs==2);
reads=logs=fail=0;cycle=1;elden_bt_exit_stack(0);assert(reads==1&&logs==2);
reads=logs=cycle=0;elden_bt_exit_stack(0);assert(reads==8&&logs==9&&addresses[7]==4096+7*16);
reads=logs=0;state.fp=(UINT64_C(1)<<39)-16;elden_bt_exit_stack(0);assert(reads==0&&logs==1);
puts("PASS context,alignment,user-bound,copy-error,cycle,eight-frame bound");}
'''
    source=a.output/'test.c';source.write_text(prefix+body+suffix)
    command=['wsl','-d','Ubuntu-22.04','-u','root','--','cc','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',lp(source),'-o',lp(a.output/'test')]
    r=subprocess.run(command,capture_output=True);(a.output/'compile.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    cmd=['wsl','-d','Ubuntu-22.04','-u','root','--','env','UBSAN_OPTIONS=halt_on_error=1',command[-1]];r=subprocess.run(cmd,capture_output=True);(a.output/'run.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    (a.output/'result.json').write_text(json.dumps({'result':r.stdout.decode(),'command':command,'run':cmd,'hardware':False},indent=2)+'\n');print(r.stdout.decode())
if __name__=='__main__':main()
