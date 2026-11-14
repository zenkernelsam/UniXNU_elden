"""Exercise actual generated page rules and identity refusals under sanitizers."""
from pathlib import Path
import argparse, json, subprocess

def lp(p):
    p=p.resolve();return '/mnt/'+p.drive[0].lower()+p.as_posix()[2:]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runtime',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():raise ValueError('Fresh output required')
    a.output.mkdir(parents=True)
    source=a.output/'test.c'
    source.write_text('''#include "EldenBtTextGuard.h"
#include <assert.h>
#include <stdio.h>
int main(void){
unsigned checks=0;
for(unsigned i=0;i<sizeof(elden_bt_native_rules)/sizeof(elden_bt_native_rules[0]);i++){
 const struct elden_bt_native_rule *r=&elden_bt_native_rules[i];
 assert(elden_bt_stream_rule(r->offset,r->bytes,r->before,r->after));checks++;
 assert(!elden_bt_stream_rule(r->offset+1,r->bytes,r->before,r->after));checks++;
 assert(!elden_bt_stream_rule(r->offset,r->bytes-1,r->before,r->after));checks++;
 assert(!elden_bt_stream_rule(r->offset,17,r->before,r->after));checks++;
 assert(!elden_bt_stream_rule(r->offset,r->bytes,0,r->after));checks++;
 assert(!elden_bt_stream_rule(r->offset,r->bytes,r->before,0));checks++;
 for(unsigned j=0;j<r->bytes;j++)for(unsigned bit=0;bit<8;bit++){
  uint8_t x[16],y[16];memcpy(x,r->before,16);memcpy(y,r->after,16);
  x[j]^=1u<<bit;assert(!elden_bt_stream_rule(r->offset,r->bytes,x,y));checks++;
  x[j]^=1u<<bit;y[j]^=1u<<bit;assert(!elden_bt_stream_rule(r->offset,r->bytes,x,y));checks++;
 }
}
uint8_t id[20]={0xfe,0x7b,0xc1,0x82,0x38,0x6a,0x27,0x66,0xc7,0x57,0x4f,0x8b,0x48,0x41,0x2f,0xf9,0xdf,0x11,0x35,0x64};
assert(elden_bt_stream_identity(0x100000c,0,0,id));checks++;
assert(!elden_bt_stream_identity(0x100000c,2,0,id));checks++;
assert(!elden_bt_stream_identity(0x100000c,0,4096,id));checks++;
assert(!elden_bt_stream_identity(0x1000007,0,0,id));checks++;
assert(!elden_bt_stream_identity(0x100000c,0,0,0));checks++;
for(unsigned i=0;i<20;i++){id[i]^=1;assert(!elden_bt_stream_identity(0x100000c,0,0,id));id[i]^=1;checks++;}
printf("PASS %u exact rule/identity checks\\n",checks);
}
''',newline='\n')
    command=['wsl','-d','Ubuntu-22.04','-u','root','--','cc','-O1','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
             '-I',lp(a.runtime),lp(source),'-o',lp(a.output/'test')]
    r=subprocess.run(command,capture_output=True);(a.output/'build.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    run=['wsl','-d','Ubuntu-22.04','-u','root','--','env','UBSAN_OPTIONS=halt_on_error=1',command[-1]]
    r=subprocess.run(run,capture_output=True);(a.output/'run.log').write_bytes(r.stdout+r.stderr);r.check_returncode()
    (a.output/'result.json').write_text(json.dumps({'command':command,'run':run,'result':r.stdout.decode(),'hardware':False},indent=2)+'\n')
    print(r.stdout.decode())
if __name__=='__main__':main()
