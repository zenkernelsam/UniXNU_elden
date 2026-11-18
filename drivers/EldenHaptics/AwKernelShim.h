/* SPDX-License-Identifier: GPL-2.0-only */
/* Minimal explicit adapter for exact upstream function bodies, not Linux/XNU
 * success stubs. Registers delegate to the actual owning bus callbacks. */
#ifndef ELDEN_AW_KERNEL_SHIM_H
#define ELDEN_AW_KERNEL_SHIM_H
#include "AwChip.h"
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;
typedef uint16_t __be16;
typedef EldenAwChip regmap;
struct firmware {const uint8_t *data;size_t size;};
struct reg_sequence {unsigned reg,def;};
struct aw86927_data {EldenAwChip *regmap;void *dev;unsigned model,waveform_index;};
#define BIT(n) (1u<<(n))
#define GENMASK(hi,lo) ((UINT32_MAX<<(lo))&(UINT32_MAX>>(31-(hi))))
static inline unsigned aw_shift(unsigned mask){unsigned n=0;while(mask&&!(mask&1)){mask>>=1;++n;}return n;}
#define FIELD_PREP(mask,value) (((uint32_t)(value)<<aw_shift(mask))&(mask))
#define FIELD_GET(mask,value) (((uint32_t)(value)&(mask))>>aw_shift(mask))
#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))
#define min_t(type,a,b) ((type)(a)<(type)(b)?(type)(a):(type)(b))
#define EINVAL 22
#define EILSEQ 84
static inline uint16_t aw_be16(const void *p){const uint8_t *b=p;return (uint16_t)(((uint16_t)b[0]<<8)|b[1]);}
#define get_unaligned_be16(p) aw_be16(p)
#define be16_to_cpu(v) aw_be16(&(v))
static int aw_reg_read(EldenAwChip *,unsigned,unsigned *);
static int aw_reg_write(EldenAwChip *,unsigned,unsigned);
static int aw_reg_update(EldenAwChip *,unsigned,unsigned,unsigned);
static int aw_read_block(EldenAwChip *,unsigned,void *,size_t);
static int aw_write_block(EldenAwChip *,unsigned,const void *,size_t);
static int aw_many(EldenAwChip *,const struct reg_sequence *,size_t);
static int aw_wait(EldenAwChip *);
static void aw_sleep(EldenAwChip *,unsigned,unsigned);
static int aw_request(const struct firmware **,const char *,EldenAwChip *);
static void aw_release(const struct firmware *);
#define regmap_read(p,r,v) aw_reg_read(p,r,v)
#define regmap_write(p,r,v) aw_reg_write(p,r,v)
#define regmap_update_bits(p,r,m,v) aw_reg_update(p,r,m,v)
#define regmap_bulk_read(p,r,v,n) aw_read_block(p,r,v,n)
#define regmap_noinc_read(p,r,v,n) aw_read_block(p,r,v,n)
#define regmap_noinc_write(p,r,v,n) aw_write_block(p,r,v,n)
#define regmap_multi_reg_write(p,s,n) aw_many(p,s,n)
#define regmap_read_poll_timeout(p,r,value,test,step,total) ((void)(r),(void)(&(value)),aw_wait(p))
#define usleep_range(lo,hi) aw_sleep(haptics->regmap,lo,hi)
#define request_firmware(fw,name,dev) aw_request(fw,name,haptics->regmap)
#define release_firmware(fw) aw_release(fw)
/* Logging omitted in the freestanding core. Native caller reports phase/error;
 * no source error code is swallowed or changed by these macros. */
#define dev_err(...) ((void)0)
#define dev_err_probe(dev,err,...) (err)
#endif
