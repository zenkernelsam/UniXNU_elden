"""Extract original WL2868C enable and regulator disable/readback bodies."""
from pathlib import Path
import hashlib,json
HERE=Path(__file__).resolve().parent
def function(text,signature):
    at=text.index(signature);end=text.index('{',at)+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[at:end]
def derive():
    spec=json.loads((HERE/'power-source.json').read_text());files={}
    for row in spec['files']:
        b=(HERE/'upstream'/row['file']).read_bytes()
        if len(b)!=row['bytes'] or hashlib.sha256(b).hexdigest()!=row['sha256']:raise ValueError('Exact power source changed')
        files[row['file']]=b.decode().replace('\r\n','\n')
    originals=[function(files['wl2868c-regulator.c'],'static int wl2868c_enable('),function(files['wl2868c-regulator.c'],'static int wl2868c_is_enabled('),
               function(files['helpers.c'],'int regulator_disable_regmap(')]
    bodies=[body.replace('int regulator_disable_regmap(','static int regulator_disable_regmap(') for body in originals]
    return ('/* SPDX-License-Identifier: GPL-2.0-only */\n/* Exact source bodies; regmap uses owned shared SE2 access. */\n'+'\n\n'.join(bodies)+'\n').encode(),{
        'original_body_sha256':[hashlib.sha256(v.encode()).hexdigest() for v in originals],
        'only_symbol_visibility_changed':True,'hardware':False}
