#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <cassert>
#include <cstdio>
static struct {
  volatile unsigned lock;
  unsigned head,tail,used;
  uint64_t dropped;
  uint8_t data[131072];
} gEldenUsbLog;
extern "C" void elden_usb_log_putc(char c) {
  if(!__sync_bool_compare_and_swap(&gEldenUsbLog.lock,0,1)) {
    __atomic_fetch_add(&gEldenUsbLog.dropped,1,__ATOMIC_RELAXED);return;
  }
  if(gEldenUsbLog.used==sizeof(gEldenUsbLog.data))__atomic_fetch_add(&gEldenUsbLog.dropped,1,__ATOMIC_RELAXED);
  else {
    gEldenUsbLog.data[gEldenUsbLog.head]=(uint8_t)c;
    gEldenUsbLog.head=(gEldenUsbLog.head+1)&(sizeof(gEldenUsbLog.data)-1);++gEldenUsbLog.used;
  }
  __sync_lock_release(&gEldenUsbLog.lock);
}
static size_t eldenUsbTakeLog(uint8_t *out,size_t limit) {
  if(!__sync_bool_compare_and_swap(&gEldenUsbLog.lock,0,1))return 0;
  size_t n=gEldenUsbLog.used<limit?gEldenUsbLog.used:limit;
  for(size_t i=0;i<n;i++){out[i]=gEldenUsbLog.data[gEldenUsbLog.tail];gEldenUsbLog.tail=(gEldenUsbLog.tail+1)&(sizeof(gEldenUsbLog.data)-1);}
  gEldenUsbLog.used-=(unsigned)n;__sync_lock_release(&gEldenUsbLog.lock);return n;
}

int main(){
 const char text[]="buffer 00\n";unsigned char out[131072];
 for(char c:text) if(c) elden_usb_log_putc(c);
 assert(eldenUsbTakeLog(out,sizeof(out))==sizeof(text)-1);
 assert(!memcmp(out,text,sizeof(text)-1)&&gEldenUsbLog.used==0);
 gEldenUsbLog.lock=1;elden_usb_log_putc('x');assert(gEldenUsbLog.used==0&&gEldenUsbLog.dropped==1);gEldenUsbLog.lock=0;
 for(unsigned i=0;i<131072;i++)elden_usb_log_putc((char)(i%251));
 for(unsigned i=0;i<20;i++)elden_usb_log_putc('x');assert(gEldenUsbLog.dropped==21);
 assert(eldenUsbTakeLog(out,0)==0&&gEldenUsbLog.used==131072);
 assert(eldenUsbTakeLog(out,sizeof(out))==sizeof(out));
 for(unsigned i=0;i<sizeof(out);i++)assert(out[i]==i%251);
 for(unsigned round=0;round<1000;round++){
  for(unsigned i=0;i<197;i++)elden_usb_log_putc((char)(i+round));
  assert(eldenUsbTakeLog(out,37)==37);
  for(unsigned i=0;i<37;i++)assert(out[i]==(unsigned char)(i+round));
  assert(eldenUsbTakeLog(out,160)==160);
  for(unsigned i=0;i<160;i++)assert(out[i]==(unsigned char)(i+37+round));
 }
 puts("PASS: exact bytes, repeated chars, full queue, dropped counter, nonblocking contention and 1000 wrap/interleave cases");
}
