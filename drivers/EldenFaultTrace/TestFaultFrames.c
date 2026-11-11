#include "FaultFrames.h"
#include <assert.h>
#include <stdio.h>
typedef struct { uint64_t next,lr; int error; unsigned calls; } Test;
static int reader(void *arg,uint64_t fp,uint64_t *pair){Test *t=arg;t->calls++;pair[0]=t->next==1?fp+16:t->next;pair[1]=t->lr;return t->error;}
int main(void){
 EldenFaultFrames out;Test t={0,0x1976faac4ULL,0,0};elden_fault_frames(&out,0x10000,reader,&t);assert(out.count==1&&!out.reason&&t.calls==1);
 t.calls=0;elden_fault_frames(&out,3,reader,&t);assert(out.reason==1&&t.calls==0);
 elden_fault_frames(&out,(1ULL<<39)-16,reader,&t);assert(out.reason==1&&t.calls==0);
 t.error=14;elden_fault_frames(&out,0x10000,reader,&t);assert(out.reason==2&&out.read_error==14&&out.count==0);
 t.error=0;t.next=0x10000;elden_fault_frames(&out,0x10000,reader,&t);assert(out.reason==4&&out.count==1);
 t.next=0x200010;elden_fault_frames(&out,0x10000,reader,&t);assert(out.reason==4);
 t.next=0;t.lr=0xfffffff007000000ULL;elden_fault_frames(&out,0x10000,reader,&t);assert(out.reason==3&&out.count==0);
 t.lr=0x1976faac4ULL;t.next=1;t.calls=0;elden_fault_frames(&out,0x10000,reader,&t);assert(out.reason==5&&out.count==16&&t.calls==16);
 puts("PASS valid chain, invalid bounds/alignment, failed read, nonprogress, large gap, invalid return address, capacity");
}
