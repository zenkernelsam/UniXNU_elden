"""Review selected GPU branch updates; emit a proposal, never edit the active lock.

Run prepare/check with --lock proposal.json in a fresh output directory before
reviewing or committing a proposal. Git worktrees and local edits stay intact.
"""
from pathlib import Path
import argparse, json, subprocess, hashlib

ROOT=Path(__file__).resolve().parents[1]

def git(path,*args):
    p=subprocess.run(['git','-c','http.version=HTTP/1.1','-c','submodule.recurse=false','-C',str(path),*args],capture_output=True,timeout=120)
    if p.returncode:raise RuntimeError(p.stderr.decode('utf-8','replace')[-1500:])
    return p.stdout

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--references',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--lock',type=Path,default=ROOT/'config/gpu-offline.lock.json')
    p.add_argument('--fetch-selected',action='store_true')
    a=p.parse_args();original=a.lock.read_bytes();lock=json.loads(original)
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);rows=[]
    for name,spec in lock['repositories'].items():
        repo=a.references/spec['directory'];ref=spec['ref']
        if not ref.startswith('refs/heads/'):raise ValueError('Named branch required')
        remote='refs/remotes/origin/'+ref[len('refs/heads/'):]
        row={'component':name,'branch':ref,'before':spec['commit']}
        head=git(repo,'rev-parse','HEAD');status=git(repo,'status','--porcelain=v1','--untracked-files=all')
        try:
            if a.fetch_selected:git(repo,'fetch','--no-tags','--recurse-submodules=no','origin','+'+ref+':'+remote)
            new=git(repo,'rev-parse',remote).decode().strip();row['proposed']=new
            row['commits']=git(repo,'log','--format=%H %s',spec['commit']+'..'+new).decode().splitlines()
            row['diff_stat']=git(repo,'diff','--stat',spec['commit'],new).decode().strip()
            row['changed']=new!=spec['commit'];row['success']=True
            if name=='gpu':
                old=git(repo,'show',spec['commit']+':NativeAdrenoABI.h')
                newabi=git(repo,'show',new+':NativeAdrenoABI.h')
                row['wire_abi_changed']=old!=newabi
                row['old_abi_sha256']=hashlib.sha256(old).hexdigest()
                row['new_abi_sha256']=hashlib.sha256(newabi).hexdigest()
            if name=='firmware':
                header=git(repo,'show',new+':AdrenoHfiStartup.h')
                row['has_a8xx_fields']=all(x in header for x in (b'AdrenoHfiPerfA8xx',b'performance_table',b'gpu_dependency_votes'))
                if not row['has_a8xx_fields']:raise ValueError('A8xx HFI interface missing; cannot substitute generic main')
            spec['commit']=new
        except Exception as e:row.update(success=False,error=str(e))
        if head!=git(repo,'rev-parse','HEAD') or status!=git(repo,'status','--porcelain=v1','--untracked-files=all'):
            raise RuntimeError('Reference changed during review: '+name)
        rows.append(row)
    if original!=a.lock.read_bytes():raise RuntimeError('Active lock changed during review')
    report={'components':rows,'active_lock_changed':False,'candidate_built':False,'device_access':False,
            'review_required':True,'passed':all(x['success'] for x in rows)}
    (out/'review.json').write_text(json.dumps(report,indent=2)+'\n')
    if report['passed']:(out/'proposal.json').write_text(json.dumps(lock,indent=2)+'\n')
    print(json.dumps({'passed':report['passed'],'changed':sum(x.get('changed',False) for x in rows),'active_lock_changed':False}))
    if not report['passed']:raise SystemExit(1)
if __name__=='__main__':main()
