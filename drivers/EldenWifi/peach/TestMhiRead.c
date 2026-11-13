/* SPDX-License-Identifier: GPL-2.0-only */
/* Includes exact original or repaired MHI functions and the real shim wait. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
typedef uint32_t u32;typedef int rwlock_t;
#define __iomem
struct device{int unused;};struct mhi_device{struct device dev;};
struct mhi_controller{void *bhie,*bhi;struct mhi_device *mhi_dev;rwlock_t pm_lock;unsigned pm_state,state_event,timeout_ms;};
struct mhi_buf{uint64_t dma_addr;unsigned len;};
#define MHI_REG_ACCESS_VALID(s) ((s)==0)
#define MHI_PM_IN_ERROR_STATE(s) ((s)!=0)
#define MHI_RANDOM_U32_NONZERO(mask) 1u
#define BHIE_TXVECSTATUS_SEQNUM_BMSK 0x3fffffffu
#define BHI_TXDB_SEQNUM_BMSK 0x3fffffffu
#define BHIE_TXVECADDR_HIGH_OFFS 1
#define BHIE_TXVECADDR_LOW_OFFS 2
#define BHIE_TXVECSIZE_OFFS 3
#define BHIE_TXVECDB_OFFS 4
#define BHIE_TXVECDB_SEQNUM_BMSK 0x3fffffffu
#define BHIE_TXVECSTATUS_OFFS 5
#define BHIE_TXVECSTATUS_STATUS_BMSK 0xc0000000u
#define BHIE_TXVECSTATUS_STATUS_XFER_COMPL 2u
#define BHI_STATUS 6
#define BHI_IMGADDR_HIGH 7
#define BHI_IMGADDR_LOW 8
#define BHI_IMGSIZE 9
#define BHI_IMGTXDB 10
#define BHI_STATUS_MASK 0xc0000000u
#define BHI_STATUS_ERROR 3u
#define upper_32_bits(n) ((u32)((n)>>32))
#define lower_32_bits(n) ((u32)(n))
#define msecs_to_jiffies(n) (n)
#define read_lock_bh(p) ((void)(p))
#define read_unlock_bh(p) ((void)(p))
#define dev_dbg(...) ((void)0)
#define dev_err(...) ((void)0)
static unsigned ticks,reads,writes,dumps,error_at,pm_at,complete_at;
static int read_result,write_result;static unsigned completion;
static struct mhi_controller *active;
static unsigned long lx_now_ms(void){return ticks;}
static int lx_pump(void){return 0;}
static void lx_idle(unsigned ms){ticks+=ms;if(pm_at&&ticks>=pm_at)active->pm_state=1;}
#define time_before(a,b) ((long)((a)-(b))<0)
#define time_after_eq(a,b) (!time_before(a,b))
// @WAIT_MACRO@
static void mhi_write_reg(struct mhi_controller *c,void *base,u32 off,u32 value){(void)c;(void)base;(void)off;(void)value;++writes;}
static int mhi_write_reg_field(struct mhi_controller *c,void *base,u32 off,u32 mask,u32 value){(void)mask;mhi_write_reg(c,base,off,value);return write_result;}
static int mhi_read_reg_field(struct mhi_controller *c,void *base,u32 off,u32 mask,u32 *out){
    (void)c;(void)base;(void)off;(void)mask;++reads;
    if(error_at&&reads==error_at)return read_result; /* exact Linux error contract: out unchanged */
    *out=complete_at&&reads>=complete_at?completion:0;return 0;
}
static void mhi_fw_load_error_dump(struct mhi_controller *c){(void)c;++dumps;}
// @MHI_FUNCTIONS@
static void reset(struct mhi_controller *c){ticks=reads=writes=dumps=error_at=pm_at=complete_at=0;read_result=write_result=0;completion=2;c->pm_state=0;active=c;}
int main(void){
    struct mhi_device device={0};struct mhi_controller c={.mhi_dev=&device,.timeout_ms=4};struct mhi_buf image={0x12345000,4096};
    reset(&c);error_at=2;read_result=-77;
#if ORIGINAL
    assert(mhi_fw_load_bhi(&c,&image)==0&&reads==2); /* deterministic stale pending=0 */
    puts("Original reproduced: pending status followed by read error falsely returns BHI success");
#else
    assert(mhi_fw_load_bhi(&c,&image)==-77&&reads==2);
    for(unsigned which=0;which<2;++which){
        int (*load)(struct mhi_controller *,const struct mhi_buf *)=which?mhi_fw_load_bhie:mhi_fw_load_bhi;
        for(unsigned at=1;at<=3;++at){reset(&c);error_at=at;read_result=-77;assert(load(&c,&image)==-77&&reads==at&&dumps==0);}
        reset(&c);complete_at=1;assert(load(&c,&image)==0&&reads==1&&ticks==0);
        reset(&c);complete_at=3;assert(load(&c,&image)==0&&reads==3&&ticks==2);
        reset(&c);completion=3;complete_at=1;assert(load(&c,&image)==-EIO);assert(dumps==(which?0u:1u));
        reset(&c);assert(load(&c,&image)==(which?-EIO:-ETIMEDOUT)&&ticks==4);
        reset(&c);c.pm_state=1;assert(load(&c,&image)==-EIO&&reads==0&&writes==0);
        reset(&c);pm_at=1;assert(load(&c,&image)==-EIO&&reads==1);
    }
    reset(&c);write_result=-61;assert(mhi_fw_load_bhie(&c,&image)==-61&&reads==0);
    puts("Repaired BHI/BHIe: read errors preserved; completion, timeout, transfer error, PM loss and doorbell failure checked");
#endif
}
