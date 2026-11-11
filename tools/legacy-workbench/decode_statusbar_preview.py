"""Decode only complete session-bound native-scale status-bar crops with CRC32."""
from pathlib import Path
import argparse,hashlib,json,re,struct,zlib
from PIL import Image

def segment(data,session):
    text=data.decode('ascii',errors='replace')
    text=re.sub(r'\r?\nELDEN STATUS v1 session='+re.escape(session)+r'(?: [a-z_]+=[a-z0-9-]+)+\r?\n','',text)
    text=re.sub(r'\r?\nELDEN ACK (?:HOME|TAP_SETTINGS|SWIPE_UP|FASTBOOT|CHARGE) session='+re.escape(session)+r'[^\r\n]*\r?\n','',text)
    if not text.endswith('\n'):text=text.rsplit('\n',1)[0] if '\n' in text else ''
    frames={}
    for line in text.splitlines():
        if line.startswith('ELDEN_BAR_BEGIN'):
            m=re.fullmatch(r'ELDEN_BAR_BEGIN session=([0-9a-f]{16}) id=(\d+) source=(\d+) x=1666 y=0 width=238 height=96 format=rgb565le',line)
            if not m or m[1]!=session:raise ValueError('Invalid crop geometry or session')
            ident=int(m[2]);source=int(m[3])
            if not 1<=ident<=10 or ident in frames or source<1:raise ValueError('Duplicate or invalid crop')
            frames[ident]={'source':source,'rows':{},'end':None,'aborted':False};continue
        if line.startswith('ELDEN_BAR_ROW'):
            m=re.fullmatch(r'ELDEN_BAR_ROW id=(\d+) y=(\d+) hex=([0-9a-f]+)',line)
            if not m:raise ValueError('Malformed crop row')
            ident,y=int(m[1]),int(m[2]);row=bytes.fromhex(m[3])
            if ident not in frames or not 0<=y<96 or len(row)!=476:raise ValueError('Unexpected crop row')
            f=frames[ident]
            if y in f['rows'] or f['end'] is not None or f['aborted']:raise ValueError('Repeated or late crop row')
            f['rows'][y]=row;continue
        if line.startswith('ELDEN_BAR_END'):
            m=re.fullmatch(r'ELDEN_BAR_END id=(\d+) crc32=([0-9a-f]{8}) bytes=45696',line)
            if not m:raise ValueError('Malformed crop end')
            ident=int(m[1])
            if ident not in frames or frames[ident]['end'] is not None or frames[ident]['aborted']:raise ValueError('Unexpected crop end')
            frames[ident]['end']=int(m[2],16);continue
        m=re.fullmatch(r'ELDEN_BAR_ABORT id=(\d+)',line)
        if m and int(m[1]) in frames:frames[int(m[1])]['aborted']=True
    result=[]
    for ident,f in sorted(frames.items()):
        if f['aborted'] or f['end'] is None or len(f['rows'])!=96:continue
        raw=b''.join(f['rows'][i] for i in range(96))
        if zlib.crc32(raw)!=f['end']:raise ValueError('Crop CRC32 mismatch')
        result.append((ident,f['source'],f['end'],raw))
    return result

def decode(data,session,gaps=()):
    if any(type(x) is not int or not 0<=x<=len(data) for x in gaps) or list(gaps)!=sorted(gaps):raise ValueError('Invalid discontinuity offsets')
    if not gaps:return segment(data,session)
    points=[0,*sorted(set(gaps)),len(data)];result=[];seen=set()
    for i,(a,b) in enumerate(zip(points,points[1:])):
        part=data[a:b]
        if i:
            m=re.search(rb'(?m)^ELDEN_BAR_BEGIN ',part)
            if not m:continue
            part=part[m.start():]
        for row in segment(part,session):
            if row[0] in seen:raise ValueError('Duplicate crop across gaps')
            seen.add(row[0]);result.append(row)
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--session',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    data=a.log.read_bytes();meta=json.loads(a.log.with_suffix('.json').read_text())
    if meta['candidate_session']!=a.session:raise ValueError('Metadata session differs')
    if meta.get('raw_sha256') and meta['raw_sha256']!=hashlib.sha256(data).hexdigest():raise ValueError('Completed capture hash differs')
    a.output.mkdir(parents=True,exist_ok=True);report=[]
    for ident,source,crc,raw in decode(data,a.session,[x['offset'] for x in meta.get('discontinuities',[])]):
        image=Image.new('RGB',(238,96));image.putdata([(((v>>11)&31)*255//31,((v>>5)&63)*255//63,(v&31)*255//31) for v, in struct.iter_unpack('<H',raw)])
        path=a.output/f'bar-{ident}.png';image.save(path);image.resize((714,288),Image.Resampling.NEAREST).save(a.output/f'bar-{ident}-3x.png')
        report.append(dict(id=ident,source_frame=source,x=1666,y=0,width=238,height=96,crc32=f'{crc:08x}',raw_sha256=hashlib.sha256(raw).hexdigest(),all_rows_verified=True,image=str(path.resolve())))
    (a.output/'report.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
