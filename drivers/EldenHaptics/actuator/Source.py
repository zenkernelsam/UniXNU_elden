"""Extract exact same-OS17 Actuator properties from the hash-bound donor.

Retain property/function bodies; only public namespace/ABI constants and owned
IO state substitutions differ. No Pagani speaker, voltage, fake backend or
VirtualAudio byte patch is selected by this generator.
"""
from pathlib import Path
import hashlib,json
HERE=Path(__file__).resolve().parent
def function(text,signature):
    start=text.index(signature);end=text.index('{',start)+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
def properties():
    spec=json.loads((HERE/'source.json').read_text());raw=(HERE/'upstream/PaganiAudioPlugIn.c').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=spec['sha256']:raise ValueError('Actuator donor changed')
    text=raw.decode();format_body=function(text,'static AudioStreamBasicDescription format(')
    body=function(text,'static OSStatus actuatorData(')
    body=body.replace('PAGANI_AUDIO_ACT_','ELDEN_ACTUATOR_')
    return ('/* SPDX-License-Identifier: BSD-2-Clause-Patent */\n/* Copyright (c) 2026, UniXNU contributors. All rights reserved. */\n'
            '/* Exact original property bodies, namespace/constants only changed. */\n'+format_body+'\n'+
            'static AudioStreamBasicDescription actuatorFormat(void){return format(1);}\n'+body+'\n').encode()
