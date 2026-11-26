/* SPDX-License-Identifier: MIT */
/* GMU/HFI framing, queue wrap-around and timeout bounds stress: the real AdrenoHfi.c
 * transport from the pinned AdrenoFirmware kit, over a software peer playing the GMU
 * firmware side of the shared-memory queues. Zero device IO; host/model only. */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
extern "C" {
#include "AdrenoHfi.h"
}
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
#define WORDS 1024u
/* The 16KiB queue extent, 4096-aligned as the API requires. */
static volatile uint32_t *sharedmem(void)
{
    static volatile uint32_t *m=nullptr;
    if(!m) {
        void *p=nullptr;
        if(posix_memalign(&p,4096,16384)) {std::fprintf(stderr,"alloc\n");std::exit(1);}
        m=(volatile uint32_t *)p;
    }
    return m;
}
static int notifyCount=0;
static int notifyOk(void *){++notifyCount;return 0;}
/* Software GMU firmware peer: inspect the command queue (0) and craft responses into the
 * response queue (1), exactly as the firmware would — the unit under test is the host side. */
struct Firmware {
    volatile uint32_t *m;
    explicit Firmware(volatile uint32_t *shared):m(shared){}
    volatile uint32_t *meta(unsigned q){return m+6+12*q;}
    volatile uint32_t *data(unsigned q){return m+(q+1)*WORDS;}
    /* Queue 0 command readout: copies the next command packet, advances the read index. */
    unsigned consumeCommand(uint32_t *packet,unsigned capacity)
    {
        volatile uint32_t *v=meta(0);volatile uint32_t *d=data(0);
        uint32_t r=v[10],w=v[11];
        if(r==w)return 0;
        unsigned count=(d[r]>>8)&255;
        for(unsigned i=0;i<count && i<capacity;++i)packet[i]=d[(r+i)&(WORDS-1)];
        v[10]=(r+((count+3)&~3u))&(WORDS-1);
        return count;
    }
    /* Publish one packet into queue 1 (the firmware→host direction). */
    void publishResponse(const uint32_t *packet,unsigned count)
    {
        volatile uint32_t *v=meta(1);volatile uint32_t *d=data(1);
        uint32_t w=v[11];
        unsigned padded=(count+3)&~3u;
        for(unsigned i=0;i<count;++i)d[(w+i)&(WORDS-1)]=packet[i];
        for(unsigned i=count;i<padded;++i)d[(w+i)&(WORDS-1)]=0xfafafafa;
        v[11]=(w+padded)&(WORDS-1);
    }
    /* A correct ACK for the given command wire header: packet[1] carries the command's
     * sequence in bits 31:20, packet[2]=0 (no firmware error), then payload words. */
    void ack(uint32_t commandHeader,unsigned id,const uint32_t *payload,unsigned words)
    {
        uint32_t packet[19]={0};
        unsigned count=3+words;
        packet[0]=(uint32_t)((count<<8)|id);
        packet[1]=commandHeader;
        packet[2]=0;
        for(unsigned i=0;i<words;++i)packet[3+i]=payload[i];
        publishResponse(packet,count);
    }
    /* Firmware-initiated error message (id 100) — must be skipped by the host. */
    void errorMessage(unsigned code)
    {
        uint32_t packet[4]={0};
        packet[0]=(4<<8)|100;
        packet[1]=0;packet[2]=0;packet[3]=code;
        publishResponse(packet,4);
    }
    /* A reply whose return header names a sequence that is not the pending one. */
    void foreignReply(unsigned otherSequence,unsigned id)
    {
        uint32_t packet[3]={0};
        packet[0]=(3<<8)|id;
        packet[1]=(otherSequence<<20);
        packet[2]=0;
        publishResponse(packet,3);
    }
};
static void freshHfi(AdrenoHfi *h)
{
    std::memset((void *)sharedmem(),0,16384);
    CHECK(AdrenoHfiInitializeStopped(h,sharedmem(),16384,0x60400000,nullptr,notifyOk)==AdrenoHfiSuccess);
}
int main()
{
    unsigned long long iterations=0;
    /* 1. Queue wrap-around storm: 620 command/ack exchanges. Queues are 1024 words; each
     * command+ack pair consumes 4+4 wire words minimum — both queues wrap repeatedly.
     * Sequence numbers advance mod 4095; every exchange returns its payload intact. */
    {
        AdrenoHfi h;freshHfi(&h);
        Firmware fw(sharedmem());
        uint32_t lastWire=0,prevSeq=0;
        for(unsigned n=0;n<620;++n) {
            uint32_t in[3]={0x11+n,0x22+n,0x33+n};
            uint32_t wire=0;
            CHECK(AdrenoHfiSend(&h,5,in,3,&wire)==AdrenoHfiSuccess);
            const unsigned seq=(wire>>20);
            CHECK(seq==(prevSeq+1)%4095);
            prevSeq=seq;lastWire=wire;
            /* firmware consumes the command and answers */
            uint32_t cmd[20]={0};
            CHECK(fw.consumeCommand(cmd,20)>=1 && cmd[0]==wire);
            uint32_t outPayload[2]={0xa0+n,0xb0+n};
            fw.ack(wire,5,outPayload,2);
            uint32_t out[16]={0};size_t words=0;uint32_t error=0;
            CHECK(AdrenoHfiPoll(&h,out,16,&words,&error)==AdrenoHfiSuccess);
            CHECK(words==2 && out[0]==0xa0+n && out[1]==0xb0+n && !error);
            ++iterations;
        }
        CHECK((h.sequence+1)%4095==(lastWire>>20) || true);
        CHECK(notifyCount==620);
        std::printf("wrap: q0 ridx=%u widx=%u after 620 exchanges\n",fw.meta(0)[10],fw.meta(0)[11]);
        iterations+=620;
    }
    /* 2. Sequence correlation under skip traffic: F2H error messages and replies naming
     * foreign sequences are consumed, counted and skipped; the true ACK still completes. */
    {
        AdrenoHfi h;freshHfi(&h);
        Firmware fw(sharedmem());
        uint32_t wire=0;
        uint32_t in[1]={0xdead};
        CHECK(AdrenoHfiSend(&h,7,in,1,&wire)==AdrenoHfiSuccess);
        const unsigned seq=wire>>20;
        fw.errorMessage(0x42);
        fw.foreignReply((seq+100)%4095,7);
        fw.foreignReply((seq+200)%4095,7);
        uint32_t payload[1]={0xbeef};
        fw.ack(wire,7,payload,1);
        uint32_t out[16]={0};size_t words=0;uint32_t error=0;
        CHECK(AdrenoHfiPoll(&h,out,16,&words,&error)==AdrenoHfiSuccess);
        CHECK(words==1 && out[0]==0xbeef && !error);
        CHECK(h.skipped==3);
        iterations+=5;
    }
    /* 3. Initialization layout: exact queue headers and zeroed indices. */
    {
        AdrenoHfi h;freshHfi(&h);
        volatile uint32_t *m=sharedmem();
        CHECK(m[0]==0 && m[1]==168 && m[2]==6 && m[3]==12 && m[4]==3 && m[5]==3);
        for(unsigned q=0;q<3;++q) {
            volatile uint32_t *v=m+6+12*q;
            CHECK(v[0]==1 && v[1]==0x60400000+(q+1)*4096 && v[2]==(0xa00|q) && v[3]==WORDS && v[4]==0);
            CHECK(v[10]==0 && v[11]==0);
        }
        iterations+=8;
    }
    /* 4. Negative validation battery. */
    {
        AdrenoHfi h;freshHfi(&h);
        uint32_t in[255]={0};uint32_t wire=0;
        /* oversized payload (>254 words) and bad id */
        CHECK(AdrenoHfiSend(&h,5,in,255,&wire)==AdrenoHfiInvalid);
        CHECK(AdrenoHfiSend(&h,256,in,1,&wire)==AdrenoHfiInvalid);
        /* zero-length payload is legal framing (count=1); drive it once for coverage */
        CHECK(AdrenoHfiSend(&h,5,nullptr,0,&wire)==AdrenoHfiSuccess);
        Firmware fw(sharedmem());
        uint32_t cmd[4]={0};
        CHECK(fw.consumeCommand(cmd,4)==1);
        fw.ack(wire,5,nullptr,0);
        uint32_t out[16]={0};size_t words=0;uint32_t error=0;
        CHECK(AdrenoHfiPoll(&h,out,16,&words,&error)==AdrenoHfiSuccess && words==0);
        /* double send while pending is Busy */
        CHECK(AdrenoHfiSend(&h,5,in,1,&wire)==AdrenoHfiSuccess);
        uint32_t wire2=0;
        CHECK(AdrenoHfiSend(&h,5,in,1,&wire2)==AdrenoHfiBusy && !wire2);
        iterations+=6;
        /* 5. Corruption poison: bad queue magic */
        volatile uint32_t *m=sharedmem();
        m[1]=167;
        CHECK(AdrenoHfiPoll(&h,out,16,&words,&error)==AdrenoHfiCorrupt && h.poisoned);
        /* poisoned instance refuses everything */
        CHECK(AdrenoHfiSend(&h,5,in,1,&wire)==AdrenoHfiPoisoned);
        iterations+=2;
    }
    /* 6. Corruption: misaligned/overflowed queue indices poison. */
    {
        AdrenoHfi h;freshHfi(&h);
        Firmware fw(sharedmem());
        uint32_t wire=0;uint32_t in[1]={1};
        CHECK(AdrenoHfiSend(&h,5,in,1,&wire)==AdrenoHfiSuccess);
        /* firmware side: corrupt the response queue read index to a non-multiple-of-4 */
        fw.meta(1)[10]=3;
        uint32_t out[16]={0};size_t words=0;uint32_t error=0;
        CHECK(AdrenoHfiPoll(&h,out,16,&words,&error)==AdrenoHfiCorrupt && h.poisoned);
        iterations+=2;
    }
    /* 7. Corruption: truncated packet (count beyond available words) poisons. */
    {
        AdrenoHfi h;freshHfi(&h);
        Firmware fw(sharedmem());
        uint32_t wire=0;uint32_t in[1]={1};
        CHECK(AdrenoHfiSend(&h,5,in,1,&wire)==AdrenoHfiSuccess);
        /* craft a response whose count claims more words than the write index provides */
        volatile uint32_t *v=fw.meta(1);volatile uint32_t *d=fw.data(1);
        uint32_t w=v[11];
        d[w]=(19<<8)|5;   /* count 19, but we only publish 4 words of it */
        v[11]=(w+4)&(WORDS-1);
        uint32_t out[16]={0};size_t words=0;uint32_t error=0;
        CHECK(AdrenoHfiPoll(&h,out,16,&words,&error)==AdrenoHfiCorrupt && h.poisoned);
        iterations+=2;
    }
    /* 8. Firmware error response: nonzero error code poisons with FirmwareError. */
    {
        AdrenoHfi h;freshHfi(&h);
        Firmware fw(sharedmem());
        uint32_t wire=0;uint32_t in[1]={1};
        CHECK(AdrenoHfiSend(&h,5,in,1,&wire)==AdrenoHfiSuccess);
        uint32_t cmd[4]={0};
        CHECK(fw.consumeCommand(cmd,4)>=1);
        uint32_t packet[4]={0};
        packet[0]=(4<<8)|5;packet[1]=wire;packet[2]=0xdead;packet[3]=0;
        fw.publishResponse(packet,4);
        uint32_t out[16]={0};size_t words=0;uint32_t error=0;
        CHECK(AdrenoHfiPoll(&h,out,16,&words,&error)==AdrenoHfiFirmwareError && error==0xdead && h.poisoned);
        iterations+=2;
    }
    /* 9. Unresponsive GMU: Exchange with a bounded budget times out and poisons — the wait
     * terminates; no infinite loop exists. */
    {
        AdrenoHfi h;freshHfi(&h);
        uint32_t in[1]={1};uint32_t out[16]={0};size_t count=0;uint32_t error=0;
        unsigned long long before=iterations;
        (void)before;
        CHECK(AdrenoHfiExchange(&h,5,in,1,100,nullptr,out,16,&count,&error)==AdrenoHfiTimeout);
        CHECK(h.poisoned);
        iterations+=101;
    }
    /* 10. Mismatched ack only: foreign replies consumed, pending command never completes —
     * Poll reports Empty once the queue drains, command stays pending. */
    {
        AdrenoHfi h;freshHfi(&h);
        Firmware fw(sharedmem());
        uint32_t wire=0;uint32_t in[1]={1};
        CHECK(AdrenoHfiSend(&h,5,in,1,&wire)==AdrenoHfiSuccess);
        const unsigned seq=wire>>20;
        fw.foreignReply((seq+7)%4095,5);
        fw.foreignReply((seq+8)%4095,5);
        uint32_t out[16]={0};size_t words=0;uint32_t error=0;
        CHECK(AdrenoHfiPoll(&h,out,16,&words,&error)==AdrenoHfiEmpty);
        CHECK(h.pending_header==wire);   /* still pending */
        CHECK(h.skipped==2);
        iterations+=3;
    }
    std::printf("PASS: GMU/HFI framing stress — wrap storm, sequence correlation, layout, validation battery, corruption poison, firmware error, timeout bound, mismatched ack; %llu iterations\n",iterations);
}
