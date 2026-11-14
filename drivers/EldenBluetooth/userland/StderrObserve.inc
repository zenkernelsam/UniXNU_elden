/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Before the original write, copy at most512 bytes without advancing its uio.
 * Exact native daemon only. No fd, error, return, buffer or policy is changed. */
extern char *proc_best_name(proc_t);
static void elden_bt_stderr_observe(proc_t p,int fd,uio_t uio)
{
    static unsigned lines;
    static const unsigned char expected[20]={0xfe,0x7b,0xc1,0x82,0x38,0x6a,0x27,0x66,0xc7,0x57,0x4f,0x8b,0x48,0x41,0x2f,0xf9,0xdf,0x11,0x35,0x64};
    unsigned char actual[20];
    if(!p||fd!=2||!uio||strcmp(proc_best_name(p),"bluetoothd")||
       proc_getcdhash(p,actual)||memcmp(actual,expected,sizeof(expected)))return;
    unsigned line=__atomic_add_fetch(&lines,1u,__ATOMIC_RELAXED);
    if(line>16)return;
    unsigned remaining=512;int count=uio_iovcnt(uio);
    for(int i=0;i<count&&i<8&&remaining;i++){
        user_addr_t address=0;user_size_t size=0;
        int error=uio_getiov(uio,i,&address,&size);
        char buffer[129]={0};unsigned take=size>128?128:(unsigned)size;
        if(take>remaining)take=remaining;
        if(!error&&take)error=copyin(address,buffer,take);
        if(error)buffer[0]=0;
        printf("ELDEN_BT_STDERR pid=%d line=%u iov=%d bytes=%llu copied=%u error=%d text=%.128s\n",
               proc_pid(p),line,i,(unsigned long long)size,take,error,buffer);
        remaining-=take;
    }
}
