"""Reconstruct session-bound GOP readbacks only after all rows and CRC32 match."""
import argparse,hashlib,json,re,struct,zlib
from pathlib import Path
from PIL import Image

def _decode_segment(data,session):
    frames={}
    # Status responses use a separate USB reply path and may land between two
    # chunks of a log row. Remove only the complete, session-bound control frame;
    # joined pixel bytes must still satisfy every row length and the whole CRC.
    text=data.decode('ascii',errors='replace')
    status=r'\r?\nELDEN STATUS v1 session='+re.escape(session)+r'(?: [a-z_]+=[a-z0-9-]+)+\r?\n'
    text=re.sub(status,'',text)
    # Explicit command acknowledgements share the same reply endpoint and
    # can also split a pixel row. Remove only complete matching-session ACKs;
    # row extents and the complete image CRC still have to validate.
    ack=r'\r?\nELDEN ACK (?:HOME|TAP_SETTINGS|SWIPE_UP|FASTBOOT|CHARGE) session='+re.escape(session)+r'[^\r\n]*\r?\n'
    text=re.sub(ack,'',text)
    if not text.endswith('\n'):
        text=text.rsplit('\n',1)[0] if '\n' in text else ''
    for line in text.splitlines():
        m=re.fullmatch(r'ELDEN_FRAME_BEGIN session=([0-9a-f]{16}) id=(\d+) source=(\d+) width=238 height=380 format=rgb565le',line)
        if m:
            token,ident,source=m.groups();ident=int(ident)
            if token!=session:raise ValueError('Preview session mismatch')
            if not 1<=ident<=10 or ident in frames:raise ValueError('Duplicate or unexpected frame')
            frames[ident]=dict(session=token,source_frame=int(source),rows={},end=None)
            continue
        m=re.fullmatch(r'ELDEN_FRAME_ROW id=(\d+) y=(\d+) hex=([0-9a-f]+)',line)
        if m:
            ident,y,hexdata=m.groups();ident=int(ident);y=int(y)
            if ident not in frames or not 0<=y<380:raise ValueError('Row without frame or invalid coordinate')
            f=frames[ident]
            if y in f['rows'] or f['end'] is not None:raise ValueError('Duplicate or late row')
            row=bytes.fromhex(hexdata)
            if len(row)!=476:raise ValueError('Invalid row width')
            f['rows'][y]=row;continue
        m=re.fullmatch(r'ELDEN_FRAME_END id=(\d+) crc32=([0-9a-f]{8}) bytes=180880',line)
        if m:
            ident,crc=m.groups();ident=int(ident)
            if ident not in frames or frames[ident]['end'] is not None:raise ValueError('Unexpected trailer')
            frames[ident]['end']=int(crc,16)
    output=[]
    for ident,f in sorted(frames.items()):
        if f['end'] is None or len(f['rows'])!=380:
            continue # A live capture may not have reached its trailer yet.
        raw=b''.join(f['rows'][y] for y in range(380))
        if zlib.crc32(raw)!=f['end']:raise ValueError('Preview CRC32 mismatch')
        output.append((ident,f,raw))
    return output

def decode(data,session,discontinuities=()):
    offsets=list(discontinuities)
    if any(type(x) is not int or x<0 or x>len(data) for x in offsets) or offsets!=sorted(offsets):
        raise ValueError('Invalid capture discontinuities')
    if not offsets:return _decode_segment(data,session)
    boundaries=[0,*sorted(set(offsets)),len(data)];result=[];seen=set()
    for i,(start,end) in enumerate(zip(boundaries,boundaries[1:])):
        part=data[start:end]
        if i:
            # Never join a lost USB fragment to another row/header. A frame
            # must begin and finish within one continuous transport segment.
            begin=re.search(rb'(?m)^ELDEN_FRAME_BEGIN ',part)
            if begin is None:continue
            part=part[begin.start():]
        for frame in _decode_segment(part,session):
            if frame[0] in seen:raise ValueError('Repeated frame across transport segments')
            seen.add(frame[0]);result.append(frame)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path)
    p.add_argument('--expect-session',required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--metadata',type=Path,help='logger JSON; defaults to the adjacent capture JSON when present')
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True);report=[]
    data=a.log.read_bytes();gaps=[]
    metadata=a.metadata or (a.log.with_suffix('.json') if a.log.with_suffix('.json').is_file() else None)
    if metadata:
        meta=json.loads(metadata.read_text())
        if meta.get('candidate_session')!=a.expect_session:raise ValueError('Metadata session differs')
        if meta.get('raw_sha256') and meta['raw_sha256']!=hashlib.sha256(data).hexdigest():raise ValueError('Metadata raw hash differs')
        gaps=[x['offset'] for x in meta.get('discontinuities',[])]
    for ident,frame,raw in decode(data,a.expect_session,gaps):
        image=Image.new('RGB',(238,380))
        image.putdata([(((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31) for v, in struct.iter_unpack('<H',raw)])
        path=a.output/f'frame-{ident}.png';image.save(path)
        image.resize((714,1140),Image.Resampling.NEAREST).save(a.output/f'frame-{ident}-3x.png')
        report.append(dict(id=ident,source_frame=frame['source_frame'],session=frame['session'],width=238,height=380,
            raw_sha256=hashlib.sha256(raw).hexdigest(),crc32=f"{frame['end']:08x}",all_rows_verified=True,image=str(path.resolve()),
            description='Read-only 1:8 sampled framebuffer; UI content still requires visual assessment'))
    (a.output/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
