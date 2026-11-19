"""Extract exact TB322FC CCI constants/v2 timing and original transfer bodies.

readl/writel use the owned error-propagating provider. Three original byte
packing shifts are made explicitly u32: promoted signed int at the high byte
is undefined for values>=128; the intended little-endian command is unchanged.
Queue launch/validation/reset/wait stay in the separate bounded native adapter;
Linux I2C/framework/PM code is not emulated or copied as a successful backend.
"""
from pathlib import Path
import hashlib,json,re
HERE=Path(__file__).resolve().parent
def function(t,signature):
    at=t.index(signature);end=t.index('{',at)+1;depth=1
    while depth:depth+=(t[end]=='{')-(t[end]=='}');end+=1
    return t[at:end]
def replace_io(text,name):
    pattern=re.compile(r'\b'+name+r'\(');out='';at=0
    while (m:=pattern.search(text,at)):
        end=m.end();depth=1
        while depth:depth+=(text[end]=='(')-(text[end]==')');end+=1
        inner=text[m.end():end-1]
        if name=='readl':
            if not inner.startswith('cci->base + '):raise ValueError('Original CCI read accessor differs')
            new='cci_read_reg(cci, '+inner[len('cci->base + '):]+')'
        else:
            depth=0;split=None
            for i,c in enumerate(inner):
                depth+=(c=='(')-(c==')')
                if c==',' and not depth:split=i
            if split is None:raise ValueError('Original CCI write shape differs')
            value=inner[:split];address=inner[split+1:].strip()
            if not address.startswith('cci->base + '):raise ValueError('Original CCI write accessor differs')
            new='cci_write_reg(cci, '+address[len('cci->base + '):]+', '+value+')'
        out+=text[at:m.start()]+new;at=end
    return out+text[at:]
def sources():
    s=json.loads((HERE/'cci-source.json').read_text());b=(HERE/'upstream/i2c-qcom-cci.c').read_bytes()
    if len(b)!=s['bytes'] or hashlib.sha256(b).hexdigest()!=s['sha256']:raise ValueError('Exact CCI source differs')
    t=b.decode();defines=[line for line in t.splitlines() if line.startswith('#define CCI_') and not line.startswith('#define CCI_TIMEOUT')]
    base='/* SPDX-License-Identifier: GPL-2.0 */\n/* Copyright (c) 2012-2016, The Linux Foundation. All rights reserved. */\n/* Copyright (c) 2017-2022 Linaro Limited. */\n'
    constants=base+'\n'.join(defines)+'\n'
    timing=function(t,'static const struct cci_data cci_v2_data =')+';\n'
    functions=[];records={}
    for name in ['cci_i2c_read','cci_i2c_write']:
        original=function(t,'static int '+name+'(');derived=replace_io(replace_io(original,'readl'),'writel');shifts=0
        if name=='cci_i2c_write':
            for j in (1,2,3):
                before=f'val |= load[j + {j}] << {j*8};';after=f'val |= (u32)load[j + {j}] << {j*8};'
                if derived.count(before)!=1:raise ValueError('Original CCI packing anchor differs')
                derived=derived.replace(before,after);shifts+=1
        functions.append(derived);records[name]={'original_body_sha256':hashlib.sha256(original.encode()).hexdigest(),
            'adapted_body_sha256':hashlib.sha256(derived.encode()).hexdigest(),'changes':'readl/writel providers and '+str(shifts)+' unsigned byte-packing promotions'}
    return {'CciRegisters.inc':constants.encode(),'CciV2Timing.inc':(base+timing).encode(),
        'CciTransfer.inc':(base+'\n\n'.join(functions)+'\n').encode()},records
