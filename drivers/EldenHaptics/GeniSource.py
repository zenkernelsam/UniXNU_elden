"""Reuse the exact BSD GENI state machine with namespaced symbols and types.

The original FIFO packing/command/error state machine is retained. Transmit
storage grows from2 to16 words with a64-byte cap, admitted only after the actual
SE reports32-bit/>=16-word FIFO. Bus.c splits63-byte payloads and4-byte reads.
"""
from pathlib import Path
import hashlib,json,re
HERE=Path(__file__).resolve().parent
def sources():
    plan=json.loads((HERE/'source.json').read_text());result={}
    for filename in ('GeniI2c.c','GeniI2c.h'):
        spec=next(s for s in plan['sources'] if s['file']==filename);raw=(HERE/'upstream'/filename).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=spec['sha256']:raise ValueError('Original GENI input changed')
        text=raw.decode().replace('"GeniI2c.h"','"GeniCore.h"').replace('pagani_geni_i2c_','elden_haptics_geni_')
        text=re.sub(r'\bGeniIo\b','EldenHapticsGeniIo',text)
        text=re.sub(r'\bGeniI2c\b','EldenHapticsGeni',text)
        text=text.replace('PAGANI_GENI_I2C_H','ELDEN_HAPTICS_GENI_CORE_H')
        if filename.endswith('.h'):
            if text.count('uint32_t tx[2], length;')!=1:raise ValueError('GENI transmit storage changed')
            text=text.replace('uint32_t tx[2], length;','uint32_t tx[16], length;')
        else:
            before='g->tx[0] = g->tx[1] = 0;'
            if text.count(before)!=1 or text.count('length > 8')!=1:raise ValueError('GENI bounded write anchor changed')
            text=text.replace(before,'for (unsigned i = 0; i < 16; ++i) g->tx[i] = 0;').replace('length > 8','length > 64')
        result['GeniCore'+Path(filename).suffix]=text.encode()
    return result
