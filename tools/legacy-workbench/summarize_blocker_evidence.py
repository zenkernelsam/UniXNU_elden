"""Join saved logs and previously UUID-checked symbolication; never reads device."""
import collections, json, re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'research/blocker-audit-20261111'
symbols={}
for folder in ('touch-event-waits-20261110','iconservices-fault-20261110'):
    doc=json.loads((ROOT/'research'/folder/'symbolication.json').read_text())
    for sample in doc.get('samples',[]):
        for row in sample.get('resolved',[]):
            if row.get('canonical') and row.get('symbol'):
                symbols[row['canonical']]={k:row[k] for k in ('symbol','offset','image') if k in row}
waits=json.loads((OUT/'current-repeated-waits.json').read_text())
for row in waits:
    row['resolved_exact_prior']=[dict(address=a,**symbols.get(a,{})) for a in [row['pc'],row['lr'],*row['frames']]]
    row['symbol_caveat']='Reuses an exact address match from the UUID-checked prior report, but names are nearest preceding symbols, not verified function extents. Large offsets must not be treated as exact callee identities.'
    # This is a saved user register context, not a fresh runnable stack. Keep
    # normal run-loop/dispatch waits separate from causal synchronous chains.
    names=' '.join(x.get('symbol','') for x in row['resolved_exact_prior'])
    row['assessment']=('normal idle-compatible run-loop/dispatch context; no deadlock proof'
                       if any(s in names for s in ('CFRunLoopRun','_dispatch_worker_thread','_dispatch_workloop_worker_thread'))
                       else 'unresolved wait; repetition alone is not a blocker')
(OUT/'waits-with-prior-symbols.json').write_text(json.dumps(waits,indent=2)+'\n')
latest=[]
for cap in json.loads((OUT/'capture-index.json').read_text()):
    if not cap['current']:continue
    text=(ROOT/cap['path']).read_bytes().decode(errors='replace')
    relevant=[]
    pattern=re.compile(r'ELDEN_ICON.*(?:EXIT|exit|loaded|installed|reject)|ELDEN_FAULT|panic\(cpu|Launch Constraint|burning CPU|memorystatus.*kill|jetsam|\bstall\w*|timed out|timeout|Failed to|failed to|ELDEN_UI_REPLAY|ELDEN_HID.*(?:touch=|mask=)',re.I)
    for line in text.splitlines():
        if pattern.search(line):relevant.append(line[:1600])
    row={'run':cap['run'],'dropped':cap['dropped'],'fault_prefix':cap['fault_prefix_count'],
         'wait_processes':dict(collections.Counter(x['name'] for x in waits if x['run']==cap['run'])),
         'lines':relevant}
    latest.append(row)
(OUT/'current-text-events.json').write_text(json.dumps(latest,indent=2)+'\n')
summary=[]
for row in latest:
    lines=row['lines'];counts=collections.Counter(lines)
    summary.append(row['run']+' drop='+str(row['dropped'])+' faults='+str(row['fault_prefix']))
    summary.append('repeated saved contexts by process: '+str(row['wait_processes']))
    summary.extend(str(n)+'x '+line for line,n in counts.items())
    summary.append('')
(OUT/'current-text-summary.txt').write_text('\n'.join(summary),encoding='utf-8')
print('contexts',len(waits),'resolved addresses',sum(bool(x.get('symbol')) for r in waits for x in r['resolved_exact_prior']))
print('current events',[(r['run'],len(r['lines'])) for r in latest])
