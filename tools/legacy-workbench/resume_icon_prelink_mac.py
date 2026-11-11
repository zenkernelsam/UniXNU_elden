"""Resume only prelink after completed v65 compile; retain the earlier candidate."""
from pathlib import Path
import hashlib,json,re,shlex,subprocess,sys,time
r=Path.cwd();assert r==Path('/Users/ciscohe/Desktop/UniXNU_elden');out=r/'out/startup-basic-bundle-v65-20261108';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(out/'kernel.macho')=='f0e6949eb87ff07b40a25c27f972ddaf19b6ab983afa6541dd9913e954ed4343'
text=(r/'out/icon-dependency-20261111/status-worker-scalar.asm').read_text();ops=re.findall(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',text,re.M)
assert ops and not any(re.search(r'\b[qvdshb]\d+(?:\b|\.)',x) for x in ops)
command=shlex.split(next(x[2:] for x in (out/'build.log').read_text().splitlines() if x.startswith('+ ') and '/emit_prelink.py ' in x))
assert Path(command[1]).is_relative_to(r) and command[1].endswith('/kits/XnuPrelink/emit_prelink.py')
assert Path(command[command.index('--output')+1])==out/'kernel.macho'
raw=Path(command[command.index('--kernel')+1]);assert raw.is_relative_to(r) and raw.is_file()
archive=out/'before-scalar';assert not archive.exists();archive.mkdir()
for n in ['kernel.macho','kernel.json','result.json']:
 p=out/n
 if p.exists():p.rename(archive/n)
started=time.monotonic();subprocess.run(command,cwd=r,check=True)
report=dict(kernel_sha256=sha(out/'kernel.macho'),seconds=time.monotonic()-started,resumed_prelink_only=True,compiled_kernel_sha256=sha(raw),pinned_root=True,read_only=True,active_configuration_changed=False)
(out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
