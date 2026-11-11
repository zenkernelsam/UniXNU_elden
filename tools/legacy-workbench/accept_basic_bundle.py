"""Verify and import the combined battery/touch/Wi-Fi firmware build."""
import argparse,hashlib,json,zipfile
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--version',type=int,default=2)
parser.add_argument('--zip-sha256',default='75c0d276189b365dd2fb90b6775b110f127f5ea3df4b49ba3e7bb11a5ad08f29')
args=parser.parse_args();assert 2<=args.version<=99
ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/('edl_work/basic-bundle-20261108/v'+str(args.version)+'/final');OUT.mkdir(parents=True,exist_ok=True)
archive=OUT.parent/'mac-artifacts.zip'
assert hashlib.sha256(archive.read_bytes()).hexdigest()==args.zip_sha256
def extract(z,dest,record,extra):
    assert set(z.namelist())==set(record)|{extra} and len(z.namelist())==len(set(z.namelist()))
    for n,h in record.items():
        b=z.read(n);assert hashlib.sha256(b).hexdigest()==h,n
        p=(dest/n).resolve();assert p.is_relative_to(dest.resolve()) and not p.is_symlink()
        if p.exists():assert p.read_bytes()==b,n
        else:p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
with zipfile.ZipFile(archive) as z:
    r=json.loads(z.read('artifact-hashes.json'));extract(z,OUT,r['files'],'artifact-hashes.json')
    (OUT/'artifact-hashes.json').write_text(json.dumps(r,indent=2)+'\n')
source=ROOT/('research/basic-bundle-20261108/source-compiled-v'+str(args.version))
with zipfile.ZipFile(OUT/'compiled-source.zip') as z:
    m=json.loads(z.read('manifest.json'));extract(z,source,m['files'],'manifest.json')
    (source/'manifest.json').write_text(json.dumps(m,indent=2)+'\n')
print(json.dumps(dict(kernel_sha256=r['files']['kernel.macho'],source_files=len(m['files']),hardware_tested=False),indent=2))
