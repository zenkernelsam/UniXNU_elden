"""Decode the bounded 21H461 FBSDisplayMode observation; no device access."""
import argparse,json,math,re,struct
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);p.add_argument('--output',type=Path)
    a=p.parse_args();rows=[]
    for pid,error,fields in re.findall(r'ELDEN_DISPLAY_MODE pid=(\d+) copy=(-?\d+) fields=([0-9a-f]+)',a.log.read_text(errors='replace')):
        row=dict(pid=int(pid),copy_error=int(error))
        if not row['copy_error']:
            raw=bytes.fromhex(fields)
            if len(raw)!=72:raise SystemExit('Mode observation has incorrect length')
            values=struct.unpack('<QQQddQQQB7x',raw)
            row.update(zip(('width','height','preferred_scale','scale_override','refresh_rate','gamut','hdr','rotation','virtual'),values))
            row['rejected_fields']=[k for k,ok in (
                ('width',row['width'] not in (0,2**64-1)),('height',row['height'] not in (0,2**64-1)),
                ('preferred_scale',row['preferred_scale']!=2**64-1),
                ('scale_override',not math.isnan(row['scale_override']) and row['scale_override']>=0),
                # Actual helper at 0x18f6e545c tests fabs(x) < 2^-52. With no
                # preferred scale, the constructor rejects an effectively zero override.
                ('effective_scale',row['preferred_scale']!=0 or abs(row['scale_override'])>=2.0**-52),
                ('refresh_rate',row['refresh_rate']>0),('gamut',row['gamut']<2),('hdr',row['hdr']<3),
                ('rotation',row['rotation']<4)) if not ok]
        rows.append(row)
    report=dict(exact_function='21H461 FBSDisplayMode+0x244',observations=rows)
    text=json.dumps(report,indent=2)+'\n'
    if a.output:a.output.write_text(text,encoding='utf-8')
    print(text,end='')

if __name__=='__main__':main()
