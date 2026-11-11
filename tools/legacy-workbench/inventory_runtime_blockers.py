"""Inventory saved Y700 captures; no device access and no inference from absence."""
from pathlib import Path
import collections,hashlib,json,re
r=Path(__file__).resolve().parents[1];out=r/'research/blocker-audit-20261111';out.mkdir(exist_ok=True)
current={'live-icon-observer-v72-20261111','live-icon-pro-v72-20261111','live-icon-home-v72-20261111','live-event-clock-v73-20261111'}
fault_re=re.compile(r'ELDEN_FAULT pid=(\d+) name=(\S+) esr=([0-9a-f]+) pc=([0-9a-f]+) lr=([0-9a-f]+) far=([0-9a-f]+)')
meta_re=re.compile(r'ELDEN_FAULT_META pid=(\d+) pc=([0-9a-f]+) slide=([0-9a-f]+) copy=(-?\d+)')
wait_re=re.compile(r'ELDEN_WAIT pass=(\d+) name=(\S+) pid=(\d+) thread=(\d+) slide=([0-9a-f]+) state=([0-9a-f]+) wait=([0-9a-f]+) cont=([0-9a-f]+) trap=([0-9a-f]+) pc=([0-9a-f]+) lr=([0-9a-f]+) frames=([0-9a-f,]*) error=(\d+)')
logs=[];faults=[];waits=[];panics=[];skipped=[]
for raw in sorted((r/'edl_work').glob('**/usb/*.bin')):
    meta_path=raw.with_suffix('.json')
    if not meta_path.exists():skipped.append(dict(path=str(raw.relative_to(r)),reason='no capture metadata'));continue
    try:meta=json.loads(meta_path.read_text())
    except (ValueError,UnicodeError):skipped.append(dict(path=str(raw.relative_to(r)),reason='unreadable capture metadata'));continue
    if raw.stat().st_size>64*1024*1024:skipped.append(dict(path=str(raw.relative_to(r)),reason='large capture needs separate streaming review',bytes=raw.stat().st_size));continue
    b=raw.read_bytes();text=b.decode(errors='replace');run=raw.parent.parent.name
    digest=hashlib.sha256(b).hexdigest()
    if meta.get('raw_sha256'):assert digest==meta['raw_sha256'],raw
    row=dict(run=run,path=str(raw.relative_to(r)),current=run in current,bytes=len(b),sha256=digest,finished=bool(meta.get('raw_sha256')),
             dropped=max((int(x['dropped']) for x in meta.get('statuses',[])),default=None),discontinuities=meta.get('discontinuities',[]),fault_prefix_count=text.count('ELDEN_FAULT pid='))
    slides={};parsed=0
    combined=sorted([(m.start(),'meta',m) for m in meta_re.finditer(text)]+[(m.start(),'fault',m) for m in fault_re.finditer(text)])
    for position,kind,m in combined:
        pid=int(m[1])
        if kind=='meta':slides[pid]=int(m[3],16);continue
        slide=slides.get(pid);pc=int(m[4],16);lr=int(m[5],16)
        def canon(a):return hex(a-slide) if slide is not None and slide!=0xffffffff and a>=0x180000000+slide else hex(a) if a==0 else None
        faults.append(dict(run=run,current=run in current,offset=position,pid=pid,name=m[2],esr=m[3],pc=hex(pc),lr=hex(lr),far=m[6],slide=hex(slide) if slide is not None else None,canonical_pc=canon(pc),canonical_lr=canon(lr)));parsed+=1
    row['parsed_faults']=parsed;logs.append(row)
    for m in wait_re.finditer(text):
        slide=int(m[5],16)
        def normalize(a):return hex(a-slide) if a>=0x180000000+slide else hex(a)
        waits.append(dict(run=run,current=run in current,pass_=int(m[1]),name=m[2],pid=int(m[3]),thread=int(m[4]),state=m[6],
                          pc=normalize(int(m[10],16)),lr=normalize(int(m[11],16)),frames=[normalize(int(a,16)) for a in m[12].split(',') if a],error=int(m[13])))
    for m in re.finditer(r'panic\(cpu[^\r\n]{0,600}',text):panics.append(dict(run=run,current=run in current,offset=m.start(),text=m[0]))
groups={}
for f in faults:
    key=(f['name'],f['canonical_pc'] or 'unresolved',f['canonical_lr'] or 'unresolved',f['esr'])
    g=groups.setdefault(key,dict(name=key[0],pc=key[1],lr=key[2],esr=key[3],count=0,current_count=0,runs=set(),pids_by_run=collections.defaultdict(set)))
    g['count']+=1;g['current_count']+=f['current'];g['runs'].add(f['run']);g['pids_by_run'][f['run']].add(f['pid'])
group_rows=[]
for g in groups.values():
    g['runs']=sorted(g['runs']);g['pids_by_run']={k:sorted(v) for k,v in g['pids_by_run'].items()};group_rows.append(g)
group_rows.sort(key=lambda x:(-x['current_count'],-x['count']))
repeated=[];bucket=collections.defaultdict(list)
for w in waits:
    if w['current']:bucket[(w['run'],w['name'],w['pid'],w['thread'],w['pc'],w['lr'],tuple(w['frames'][:12]))].append(w)
for rows in bucket.values():
    passes=sorted(set(x['pass_'] for x in rows))
    if len(passes)>1:repeated.append(dict(**{k:v for k,v in rows[0].items() if k!='pass_'},passes=passes,classification='repeated saved context; may be normal idle, not automatically a blocker'))
for name,data in [('capture-index.json',logs),('faults.json',faults),('fault-groups.json',group_rows),('waits.json',waits),('current-repeated-waits.json',repeated),('panics.json',panics),('skipped-captures.json',skipped)]:
    (out/name).write_text(json.dumps(data,indent=2)+'\n')
print(json.dumps(dict(captures=len(logs),bytes=sum(x['bytes'] for x in logs),faults=len(faults),fault_groups=len(group_rows),current_repeated_contexts=len(repeated),panics=len(panics)),indent=2))
print(json.dumps(group_rows[:15],indent=2))
