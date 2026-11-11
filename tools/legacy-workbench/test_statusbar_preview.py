"""Synthetic transport fixtures, never tablet evidence."""
import zlib
from decode_statusbar_preview import decode
s='0123456789abcdef';pixel=bytes(range(238))*2;raw=pixel*96
begin=f'\nELDEN_BAR_BEGIN session={s} id=1 source=8 x=1666 y=0 width=238 height=96 format=rgb565le\n'
rows=''.join(f'ELDEN_BAR_ROW id=1 y={y} hex={pixel.hex()}\n' for y in range(96))
end=f'ELDEN_BAR_END id=1 crc32={zlib.crc32(raw):08x} bytes=45696\n'
data=(begin+rows+end).encode();assert decode(data,s)[0][3]==raw
at=data.index(b'hex=')+51
for reply in [f'\nELDEN STATUS v1 session={s} configured=1\n',f'\nELDEN ACK HOME session={s} result=0\n']:
    mixed=data[:at]+reply.encode()+data[at:];assert decode(mixed,s)[0][3]==raw
assert decode(data[:at],s)==[] and decode(data,s,[at])==[]
for bad in [data.replace(b'width=238',b'width=239'),data.replace(s.encode(),b'f'*16,1),data.replace(b'hex=0001',b'hex=0101',1),data.replace(b'y=95 ',b'y=94 '),data+data]:
    try:decode(bad,s)
    except ValueError:pass
    else:raise AssertionError('Invalid crop accepted')
print('PASS: complete rows/CRC, bound reply interleaving, truncation, gaps, geometry, session and duplicates')
