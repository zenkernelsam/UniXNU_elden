from pathlib import Path
import sys,json
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');sys.path.insert(0,str(r/'scripts'));import make_entry_probe_kernel as m
p=r/'research/preview-four-20261111'
a=m.symbols((p/'v87-control.macho').read_bytes());b=m.symbols((p/'native-v88.macho').read_bytes());d={n:[a.get(n),b.get(n)] for n in a.keys()|b.keys() if a.get(n)!=b.get(n)}
(p/'symbol-differences.json').write_text(json.dumps(d,indent=2)+'\n');print(json.dumps(d,indent=2))
