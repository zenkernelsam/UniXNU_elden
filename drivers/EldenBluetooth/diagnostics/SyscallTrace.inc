/* SPDX-License-Identifier: MIT */
/* Read-only observation, only for the original21H461 bluetoothd identity. */
#include <sys/codesign.h>
static unsigned elden_bt_syscall_trace_count;
static __attribute__((noinline)) unsigned elden_bt_syscall_trace(proc_t p)
{
    static const unsigned char expected[20]={0xfe,0x7b,0xc1,0x82,0x38,0x6a,0x27,0x66,0xc7,0x57,0x4f,0x8b,0x48,0x41,0x2f,0xf9,0xdf,0x11,0x35,0x64};
    /* The APIs fill these output buffers. Avoid compiler auto-zero SIMD in
     * the diagnostic itself; name remains a valid empty string on failure,
     * and actual is read only after proc_getcdhash reports success. */
    char name[32] __attribute__((uninitialized));
    unsigned char actual[20] __attribute__((uninitialized));
    if(!p)return 0;
    name[0]=0;name[sizeof(name)-1]=0;
    proc_name(proc_pid(p),name,sizeof(name));
    if(strcmp(name,"bluetoothd") || proc_getcdhash(p,actual) || memcmp(actual,expected,sizeof(expected)))return 0;
    unsigned n=__atomic_add_fetch(&elden_bt_syscall_trace_count,1u,__ATOMIC_RELAXED);
    return n<=32?n:0;
}
