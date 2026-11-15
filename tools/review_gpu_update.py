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
    entries=lock.get('repositories',lock.get('sources',{}))
    if not entries:raise ValueError('No GPU source entries')
    if 'runtime' in lock:
        # Observe the requested development branch, but leave its compiled pin
        # paired with the exact official builder/patch metadata in the proposal.
        entries=dict(entries,runtime=dict(lock['runtime'],directory=lock['runtime'].get('directory','metal2vulkan-development')))
    for name,spec in entries.items():
        repo=a.references/spec['directory'];ref=spec.get('ref','refs/heads/main')
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
            if name=='runtime':
                row['pin_retained_for_paired_upgrade']=True
                row['patch_and_builder_review_required']=True
            else:spec['commit']=new
        except Exception as e:row.update(success=False,error=str(e))
        if head!=git(repo,'rev-parse','HEAD') or status!=git(repo,'status','--porcelain=v1','--untracked-files=all'):
            raise RuntimeError('Reference changed during review: '+name)
        rows.append(row)
    if original!=a.lock.read_bytes():raise RuntimeError('Active lock changed during review')
    report={'components':rows,'active_lock_changed':False,'candidate_built':False,'device_access':False,
            'review_required':True,'passed':all(x['success'] for x in rows)}
    if 'runtime' in lock:
        report['full_chain_review']=['official compatibility_inputs and runtime patch series must advance together',
            'RuntimeSource/FrameSource exact-source anchors require review for new providers',
            'full native/Metal/Rust/Turnip build and ABI checks required before accepting proposal']
    (out/'review.json').write_text(json.dumps(report,indent=2)+'\n')
    if report['passed']:(out/'proposal.json').write_text(json.dumps(lock,indent=2)+'\n')
    print(json.dumps({'passed':report['passed'],'changed':sum(x.get('changed',False) for x in rows),'active_lock_changed':False}))
    if not report['passed']:raise SystemExit(1)
if __name__=='__main__':main()
