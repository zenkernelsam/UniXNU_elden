"""Derive pen sampling hooks from the exact current touch worker, without
changing controller, SMMU, finger decoder or acknowledged finger delivery.
"""
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[2]
EXPECTED='2d9f97ce84fc24d693974d4d0e6c362c44d7a5bff8255fc850a17c3c427ed92b'
def derive():
    data=(ROOT/'drivers/EldenNvt36536/Runtime.cpp').read_bytes()
    if hashlib.sha256(data).hexdigest()!=EXPECTED:raise ValueError('Current touch worker changed; review integration')
    text=data.decode()
    def once(before,after):
        nonlocal text
        if text.count(before)!=1:raise ValueError('Touch worker anchor changed: '+before[:80])
        text=text.replace(before,after)
    once('#include "EldenTouchBoard.h"','#include "EldenTouchBoard.h"\n#include "EldenPenNative.h"')
    once('bool dma_mode=false,streaming=false;','bool dma_mode=false,streaming=false;unsigned penErrors=0;')
    once('    release_contacts(c);\n    if(c->dma_mode){','''    int penStop=elden_pen_native_stop();
    if(penStop)__atomic_store_n(&input_release_error,penStop,__ATOMIC_RELEASE);
    release_contacts(c);
    if(c->dma_mode){''')
    once('    c->streaming=true;','''    int penStart=elden_pen_native_start((uintptr_t)c);
    if(penStart)log("\\nELDEN_PEN start refused=%d; finger path retained\\n",penStart);
    c->streaming=true;''')
    once('''        error=elden_nvt_points(&c->device,packet);
        if(error){finish(c,error);return;}
        error=elden_nvt_decode''','''        error=elden_nvt_points(&c->device,packet);
        if(error){elden_pen_native_fault();finish(c,error);return;}
        // Every complete packet is considered, even when the finger set is
        // unchanged or its publisher is temporarily unavailable. The worker's
        // own Context is the real frame owner the guarded path rechecks.
        int penResult=elden_pen_native_packet(packet,sizeof(packet),(uintptr_t)c);
        if(penResult&&++c->penErrors<=4)log("\\nELDEN_PEN report refused=%d\\n",penResult);
        error=elden_nvt_decode''')
    return text.encode()
