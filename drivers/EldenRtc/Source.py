"""Combine exact Gen4 RTC read/carry arithmetic and upstream platform clock
registration with the existing Elden platform source. No new DT/nub behavior.
"""
from pathlib import Path
import hashlib,json
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
def pinned(name):
    spec=json.loads((HERE/'source.json').read_text())['files'][name]
    raw=(HERE/'upstream'/name).read_bytes()
    if hashlib.sha256(raw).hexdigest()!=spec['sha256']:raise ValueError('RTC upstream source changed: '+name)
    return raw.decode()
def function(text,signature):
    at=text.index(signature);end=text.index('{',at)+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[at:end]
def core():
    body=function(pinned('rtc-pm8xxx.c'),'static int pm8xxx_rtc_read_raw(')
    return ('/* SPDX-License-Identifier: GPL-2.0-only */\n#include "CoreShim.h"\n'+body+'\n'+(HERE/'CorePublic.inc').read_text()).encode()
def platform():
    base=ROOT/'profiles/v91/native-build/extra/07f96a42bdd6/ArmDTPlatform.cpp';raw=base.read_bytes()
    if hashlib.sha256(raw).hexdigest()!='07f96a42bdd638022960f5fcaf9e3a07dd73a82c869a70815781cac6d4873b3b':raise ValueError('Existing Elden platform changed')
    text=raw.decode();anchor='  const char *excludeList (void) override;'
    if text.count(anchor)!=1:raise ValueError('Platform override anchor changed')
    text=text.replace(anchor,anchor+'\n  long getGMTTimeOfDay (void) override;\n  void setGMTTimeOfDay (long secs) override;')
    upstream=pinned('ArmDTPlatform.cpp');start=upstream.index('// The calendar at boot.');end=upstream.index('// Serialized empty name lists:',start)
    block=upstream[start:end];position=text.index('// Serialized empty name lists:')
    return (text[:position]+block+text[position:]).encode()
