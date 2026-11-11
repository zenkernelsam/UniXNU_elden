"""Extract bounded stack observations from a session-bound USB capture."""
import argparse,json,re
from pathlib import Path
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path)
    p.add_argument('--session',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    text=a.log.read_bytes().decode('ascii',errors='replace')
    status=r'\r?\nELDEN STATUS v1 session='+re.escape(a.session)+r'(?: [a-z_]+=[a-z0-9-]+)+\r?\n'
    if not re.search(status,text):raise SystemExit('No matching session status in capture')
    text=re.sub(status,'',text)
    pattern=r'ELDEN_WAIT pass=(\d+) name=(\S+) pid=(\d+) thread=(\d+) slide=([0-9a-f]+) state=([0-9a-f]+) wait=([0-9a-f]+) cont=([0-9a-f]+) trap=([0-9a-f]+) pc=([0-9a-f]+) lr=([0-9a-f]+) frames=([0-9a-f,]*) error=(\d+)'
    rows=[]
    for line in text.splitlines():
        m=re.fullmatch(pattern,line)
        if not m:continue
        values=m.groups();row=dict(zip(('pass','name','pid','thread','slide','state','wait','cont','trap','pc','lr','frames','error'),values))
        for key in ('pass','pid','thread','error'):row[key]=int(row[key])
        for key in ('slide','state','wait','cont','trap','pc','lr'):row[key]=int(row[key],16)
        row['frames']=[int(x,16) for x in row['frames'].split(',') if x]
        if len(row['frames'])>24:raise ValueError('Unexpected trace extent')
        row['session']=a.session;rows.append(row)
    a.output.write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(samples=len(rows),passes=sorted(set(r['pass'] for r in rows)),processes=sorted(set(r['name'] for r in rows)))))
if __name__=='__main__':main()
