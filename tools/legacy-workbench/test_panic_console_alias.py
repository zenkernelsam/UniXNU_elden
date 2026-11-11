"""Native host test of the actual patched panic dispatch functions; no emulator."""
from pathlib import Path
import hashlib,json,subprocess
root=Path.cwd();out=root/'out/ipados-console-20261107';out.mkdir(exist_ok=True)
source=root/'third_party/UniXNU/out/elden/ipados-17.7.11-21H461/elden-ufs_ipados/tree/.cache/xnu-work/osfmk/kern/printf.c'
text=source.read_text();start=text.index('void\nconsdebug_putc(char c)');end=text.index('void\nconsdebug_log(char c)',start);body=text[start:end]
assert body.count('PE_kputc != console_write_unbuffered')==2
prefix=r'''
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
static char display[100],panicbuf[100],uart[100];
static unsigned nd,np,nu;
static bool serial,disable_serial_output;
static bool console_is_serial(void){return serial;}
static void console_write_char(char c){display[nd++]=c;}
static void console_write_unbuffered(char c){display[nd++]=c;}
static void debug_putc(char c){panicbuf[np++]=c;}
static void uart_putc(char c){uart[nu++]=c;}
static void (*PE_kputc)(char);
'''
suffix=r'''
int main(void){
 const char *message="buffer 00\n";size_t n=strlen(message);unsigned cases=0;
 void (*writers[])(char)={consdebug_putc,consdebug_putc_unbuffered};
 for(int w=0;w<2;w++)for(int s=0;s<2;s++)for(int d=0;d<2;d++)for(int alias=0;alias<2;alias++){
  memset(display,0,sizeof(display));memset(panicbuf,0,sizeof(panicbuf));memset(uart,0,sizeof(uart));nd=np=nu=0;
  serial=s;disable_serial_output=d;PE_kputc=alias?console_write_unbuffered:uart_putc;
  for(size_t i=0;i<n;i++)writers[w](message[i]);
  assert(nd==n&&!memcmp(display,message,n));assert(np==n&&!memcmp(panicbuf,message,n));
  if(!s&&!d&&!alias)assert(nu==n&&!memcmp(uart,message,n));else assert(nu==0);
  cases++;
 }
 nd=0;PE_kputc=console_write_unbuffered;PE_kputc('X');assert(nd==1&&display[0]=='X');cases++;
 printf("%u dispatch checks passed; repeated characters and panic buffer preserved\n",cases);return 0;
}
'''
c=out/'panic-dispatch-test.c';c.write_text(prefix+body+suffix)
binary=out/'panic-dispatch-test'
subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(binary)],check=True)
r=subprocess.run([str(binary)],capture_output=True,text=True,check=True)
report=dict(functions_sha256=hashlib.sha256(body.encode()).hexdigest(),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),native_host_test=True,emulator_used=False,cases=17,result=r.stdout.strip(),device_verified=False)
(out/'panic-dispatch-test.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
