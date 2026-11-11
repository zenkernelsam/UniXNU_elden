"""Verify transferred battery artifacts and preserve successful source inputs."""
import argparse,hashlib,json,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--attempt',type=int,default=3)
p.add_argument('--zip-sha256',required=True);a=p.parse_args();assert 2<=a.attempt<=9
version='v'+str(a.attempt)
OUT=ROOT/('edl_work/battery-telemetry-20261108/final-'+version);OUT.mkdir(parents=True,exist_ok=True)
archive=OUT.parent/('mac-artifacts-'+version+'.zip')
assert hashlib.sha256(archive.read_bytes()).hexdigest()==a.zip_sha256
def extract(z,root,files):
    assert len(z.namelist())==len(set(z.namelist()))
    assert set(z.namelist())==set(files)|{'artifact-hashes.json' if 'artifact-hashes.json' in z.namelist() else 'manifest.json'}
    for name,digest in files.items():
        data=z.read(name);assert hashlib.sha256(data).hexdigest()==digest,name
        p=(root/name).resolve();assert p.is_relative_to(root.resolve()) and not p.is_symlink(),name
        if p.exists():assert p.is_file() and p.read_bytes()==data,'Existing artifact differs: '+name
        else:p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data)
with zipfile.ZipFile(archive) as z:
    record=json.loads(z.read('artifact-hashes.json'));extract(z,OUT,record['files'])
    (OUT/'artifact-hashes.json').write_text(json.dumps(record,indent=2)+'\n')
SOURCE=ROOT/('research/battery-telemetry-20261108/source-compiled-'+version)
with zipfile.ZipFile(OUT/'compiled-source.zip') as z:
    m=json.loads(z.read('manifest.json'));extract(z,SOURCE,m['files'])
    (SOURCE/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
print(json.dumps(dict(accepted=True,source_files=len(m['files']),kernel_sha256=record['files']['kernel.macho'],
    hardware_tested=False),indent=2))
