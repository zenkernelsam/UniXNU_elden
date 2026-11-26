"""Export a complete pinned GPU build tree from Git objects, never dirty files."""
from pathlib import Path, PurePosixPath
import argparse, hashlib, json, subprocess

ROOT=Path(__file__).resolve().parents[1]
def git_files(repo,commit,prefix):
    rows=subprocess.check_output(['git','-C',str(repo),'ls-tree','-rz',commit,*(['--',prefix] if prefix else [])]).split(b'\0')
    entries=[]
    for row in rows:
        if not row:continue
        info,name=row.split(b'\t',1);mode,kind,oid=info.split()
        if kind==b'commit':continue
        if kind!=b'blob' or mode==b'120000':raise ValueError('Unsupported source tree entry')
        entries.append((name.decode(),oid.decode()))
    data=subprocess.check_output(['git','-C',str(repo),'cat-file','--batch'],input=''.join(oid+'\n' for _,oid in entries).encode())
    at=0
    for name,oid in entries:
        end=data.index(b'\n',at);found,kind,size=data[at:end].split();size=int(size)
        if found.decode()!=oid or kind!=b'blob':raise ValueError('Git object stream differs')
        raw=data[end+1:end+1+size];at=end+size+2
        if len(raw)!=size or data[at-1:at]!=b'\n':raise ValueError('Truncated Git object stream')
        yield PurePosixPath(name),raw
    if at!=len(data):raise ValueError('Extra Git object data')
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--references',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--lock',type=Path,default=ROOT/'config/gpu-build.lock.json');a=p.parse_args()
    lock=json.loads(a.lock.read_text());out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);records={}
    for name,row in lock['sources'].items():
        repo=a.references/row['directory'];commit=row['commit'];prefix=row.get('prefix','')
        count=0
        for path,raw in git_files(repo,commit,prefix):
                if path.is_absolute() or '..' in path.parts:raise ValueError('Unsafe export member')
                relative=path.relative_to(prefix) if prefix else path
                dest=out/row['destination']/str(relative)
                if dest.exists():raise ValueError('Source owner collision: '+str(dest))
                # Raw Git blobs bypass checkout/archive EOL filters. A genuinely
                # CRLF patch is normalized explicitly, with both hashes retained.
                data=raw.replace(b'\r\n',b'\n') if relative.suffix=='.patch' else raw
                dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(data)
                records[dest.relative_to(out).as_posix()]={'component':name,'commit':commit,'git_path':str(path),
                    'raw_git_sha256':hashlib.sha256(raw).hexdigest(),'sha256':hashlib.sha256(data).hexdigest(),
                    'transform':'crlf-to-lf-patch' if data!=raw else None};count+=1
        print(name,count,flush=True)
    for path,row in lock.get('compatibility_inputs',{}).items():
        if path not in records or records[path]['component']!='tooling':raise ValueError('Compatibility override must replace declared tooling')
        if row.get('worktree_file'):
            raw=(ROOT/row['worktree_file']).read_bytes()
        else:
            raw=subprocess.check_output(['git','-C',str(a.references/row['directory']),'show',row['commit']+':'+row['path']])
        if hashlib.sha256(raw).hexdigest()!=row['sha256']:raise ValueError('Compatibility source identity changed')
        (out/path).write_bytes(raw)
        records[path]={'component':'tooling-compatibility','worktree_file':row.get('worktree_file'),'commit':row.get('commit'),'git_path':row.get('path'),'sha256':row['sha256']}
    (out/'elden-source-manifest.json').write_text(json.dumps({'lock_sha256':hashlib.sha256(a.lock.read_bytes()).hexdigest(),'files':records},indent=2)+'\n')
    (out/'elden-build-lock.json').write_bytes(a.lock.read_bytes())
if __name__=='__main__':main()
