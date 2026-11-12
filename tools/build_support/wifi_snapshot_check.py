"""Validate explicitly provided Linux+Peach and supplicant source snapshots."""
import hashlib,json,os
from pathlib import Path
def check():
    manifest=Path(os.environ['ELDEN_INPUT_MANIFEST']);d=json.loads(manifest.read_text())
    for path,expected in d['checked_files'].items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest()!=expected:raise ValueError('Provided source changed: '+path)
    output=Path(d['identity_output']);text=json.dumps(d['identity'],indent=2,sort_keys=True)+'\n'
    output.parent.mkdir(parents=True,exist_ok=True)
    if output.exists() and output.read_text()!=text:raise ValueError('Source identity changed')
    if not output.exists():output.write_text(text)
    print('Verified provided source snapshot: Linux base + explicit Peach overlay, and original supplicant')
