"""Reuse the exact BSD GENI state machine with namespaced symbols and types.

AW data-port blocks are split by Bus.c into <=7 write bytes or <=4 read bytes,
so no change to the original FIFO packing/command/error paths is required.
"""
from pathlib import Path
import hashlib,json,re
HERE=Path(__file__).resolve().parent
def sources():
    plan=json.loads((HERE/'source.json').read_text());result={}
    for filename in ('GeniI2c.c','GeniI2c.h'):
        spec=next(s for s in plan['sources'] if s['file']==filename);raw=(HERE/'upstream'/filename).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=spec['sha256']:raise ValueError('Original GENI input changed')
        text=raw.decode().replace('pagani_geni_i2c_','elden_haptics_geni_')
        text=re.sub(r'\bGeniIo\b','EldenHapticsGeniIo',text)
        text=re.sub(r'\bGeniI2c\b','EldenHapticsGeni',text)
        text=text.replace('PAGANI_GENI_I2C_H','ELDEN_HAPTICS_GENI_CORE_H').replace('"GeniI2c.h"','"GeniCore.h"')
        result['GeniCore'+Path(filename).suffix]=text.encode()
    return result
