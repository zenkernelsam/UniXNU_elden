"""Derive touch route checks that accept only an immutable registered boot route."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]

def replace(text,old,new):
    if text.count(old)!=1:raise ValueError('Touch route anchor drift: '+old[:70])
    return text.replace(old,new,1)

def route():
    text=(ROOT/'drivers/EldenNvt36536/DmaRoute.c').read_text(encoding='utf-8')
    text=replace(text,'#include "DmaRoute.h"','#include "DmaRoute.h"\n#include "EldenSmmuBootRoute.h"')
    text=replace(text,'/* 0=boot baseline,', '''static int expected_read(const EldenTouchRouteIo *io,unsigned offset,uint32_t baseline){
    uint32_t expected=0;int rc=elden_smmu_boot_route_expected(offset,baseline,&expected);
    if(rc)return rc;
    return io->read(io->context,offset)==expected?0:-16;
}
/* 0=boot baseline,''')
    text=replace(text,'if(io->read(io->context,0x800+4*i)!=smr||io->read(io->context,0xc00+4*i)!=s2cr)return -16;',
                 'if(expected_read(io,0x800+4*i,smr)||expected_read(io,0xc00+4*i,s2cr))return -16;')
    text=replace(text,'''if(io->read(io->context,0x1000+4*i)!=(i<7?0x1f000u:0)||
           io->read(io->context,0x1800+4*i)!=(i<7?1u:0))return -16;''',
                 '''if(expected_read(io,0x1000+4*i,i<7?0x1f000u:0)||
           expected_read(io,0x1800+4*i,i<7?1u:0))return -16;''')
    text=replace(text,'p->claimed=true;rc=snapshot(p,false);',
                 'p->claimed=true;rc=elden_smmu_boot_route_seal();\n    if(!rc)rc=snapshot(p,false);')
    text+='''
int elden_touch_route_expected(const EldenTouchDmaRouteOwner *p,uint64_t epoch,unsigned off,uint32_t baseline,uint32_t *out){
    if(!p||!out||!epoch)return -22;
    if(off!=SLOT_SMR&&off!=SLOT_S2CR){*out=baseline;return 0;}
    if(!p->attempted){*out=baseline;return 0;}
    if(p->epoch!=epoch||p->poisoned||p->error)return -16;
    if(p->attached&&p->claimed){*out=off==SLOT_SMR?NEW_SMR:ELDEN_TOUCH_DMA_BANK;return 0;}
    if(p->detached&&!p->claimed){*out=off==SLOT_SMR?0:ELDEN_TOUCH_DMA_BANK;return 0;}
    return -16; /* in-progress/uncertain owner cannot fake idle expectation */
}
'''
    return text.encode()
