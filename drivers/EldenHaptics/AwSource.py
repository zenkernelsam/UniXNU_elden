"""Extract fixed Gen4 AW86937 bodies, preserving every register/error path.
The sole playback change is infinite-loop15 -> single-shot0. A separate owned
policy decides whether a shot may run; no speaker or SPMI haptic code is used.
"""
from pathlib import Path
import hashlib,json
HERE=Path(__file__).resolve().parent
def function(text,name):
    start=text.index('static int '+name+'(');at=text.index('{',start)+1;depth=1
    while depth:depth+=(text[at]=='{')-(text[at]=='}');at+=1
    return text[start:at]
def derive():
    spec=json.loads((HERE/'source.json').read_text());item=next(x for x in spec['sources'] if x['file']=='aw86927.c')
    raw=(HERE/'upstream/aw86927.c').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=item['sha256']:raise ValueError('Pinned Gen4 AW source changed')
    source=raw.decode();definitions=source[source.index('#define AW86927_RSTCFG_REG'):source.index('struct aw86927_data {')]
    names=['aw86927_wait_enter_standby','aw86927_play_mode','aw86927_stop','aw86927_play_sine','aw8693x_haptic_init','aw86927_ram_load','aw86927_detect']
    selected=[]
    for name in names:
        text=function(source,name)
        if name=='aw86927_play_sine':
            before='FIELD_PREP(AW86927_WAVCFG9_SEQ1LOOP_MASK,\n\t\t\t\t\t    AW86927_WAVCFG9_SEQ1LOOP_INFINITELY)'
            if text.count(before)!=1:raise ValueError('Original loop-selection anchor changed')
            text=text.replace(before,'FIELD_PREP(AW86927_WAVCFG9_SEQ1LOOP_MASK, 0)')
        selected.append(text)
    return ('/* SPDX-License-Identifier: GPL-2.0-only */\n/* Exact fixed Gen4 source bodies + explicit adapter. */\n#include "AwKernelShim.h"\n'+definitions+'\n'+(HERE/'AwAdapter.inc').read_text()+'\n'+'\n\n'.join(selected)+'\n'+(HERE/'AwPublic.inc').read_text()+'\n'+(HERE/'AwRtp.inc').read_text()).encode()
