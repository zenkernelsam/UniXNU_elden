"""Export a complete pinned GPU build tree from Git objects, never dirty files."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, io, json, subprocess, tarfile

ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--references',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--lock',type=Path,default=ROOT/'config/gpu-build.lock.json');a=p.parse_args()
    lock=json.loads(a.lock.read_text());out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);records={}
    for name,row in lock['sources'].items():
        repo=a.references/row['directory'];commit=row['commit'];prefix=row.get('prefix','')
        data=subprocess.check_output(['git','-C',str(repo),'archive','--format=tar',commit,*([prefix] if prefix else [])])
        count=0
        with tarfile.open(fileobj=io.BytesIO(data)) as archive:
            for member in archive:
                path=PurePosixPath(member.name)
                if path.is_absolute() or '..' in path.parts or member.issym() or member.islnk():raise ValueError('Unsafe export member')
                if not member.isfile():continue
                relative=path.relative_to(prefix) if prefix else path
                dest=out/row['destination']/str(relative)
                if dest.exists():raise ValueError('Source owner collision: '+str(dest))
                raw=archive.extractfile(member).read()
                # Git apply interprets CR bytes as patch context. Some published
                # patch blobs use CRLF while their target repositories use LF.
                # Preserve the original hash and normalize patch files only.
                data=raw.replace(b'\r\n',b'\n') if relative.suffix=='.patch' else raw
                dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
                records[dest.relative_to(out).as_posix()]={'component':name,'commit':commit,'git_path':str(path),
                    'raw_git_sha256':hashlib.sha256(raw).hexdigest(),'sha256':hashlib.sha256(data).hexdigest(),
                    'transform':'crlf-to-lf-patch' if data!=raw else None};count+=1
        print(name,count,flush=True)
    for path,row in lock.get('compatibility_inputs',{}).items():
        if path not in records or records[path]['component']!='tooling':raise ValueError('Compatibility override must replace declared tooling')
        raw=subprocess.check_output(['git','-C',str(a.references/row['directory']),'show',row['commit']+':'+row['path']])
        if hashlib.sha256(raw).hexdigest()!=row['sha256']:raise ValueError('Compatibility source identity changed')
        (out/path).write_bytes(raw)
        records[path]={'component':'tooling-compatibility','commit':row['commit'],'git_path':row['path'],'sha256':row['sha256']}
    (out/'elden-source-manifest.json').write_text(json.dumps({'lock_sha256':hashlib.sha256(a.lock.read_bytes()).hexdigest(),'files':records},indent=2)+'\n')
    (out/'elden-build-lock.json').write_bytes(a.lock.read_bytes())
if __name__=='__main__':main()
