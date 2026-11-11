"""Fetch every research clone and fast-forward clean tracking branches only."""
import argparse, concurrent.futures, datetime, json, os, re, subprocess, sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
RESEARCH=(ROOT/'research').resolve()
ENV={**os.environ,'GIT_TERMINAL_PROMPT':'0','GCM_INTERACTIVE':'never'}

def redact(text):
    return re.sub(r'(https?://)[^/\s@]+@',r'\1[redacted]@',text)

def git(repo,*args,check=True,timeout=120):
    result=subprocess.run(['git','-c','http.version=HTTP/1.1','-C',str(repo),*args],capture_output=True,env=ENV,timeout=timeout)
    out=result.stdout.decode('utf-8','replace').strip()
    err=result.stderr.decode('utf-8','replace').strip()
    if check and result.returncode:raise RuntimeError(redact(err or out)[-1800:])
    return out,result.returncode

def discover():
    result=subprocess.run(['rg','--files','-uu','research','-g','**/.git/HEAD','-g','**/.git'],
        cwd=ROOT,capture_output=True,text=True,check=True)
    repos=set()
    for name in result.stdout.splitlines():
        marker=ROOT/name
        repo=(marker.parent.parent if marker.name=='HEAD' else marker.parent).resolve()
        if repo.is_relative_to(RESEARCH):repos.add(repo)
    return sorted(repos)

def remote_refs(repo):
    return dict(line.split(' ',1) for line in git(repo,'for-each-ref','--format=%(refname) %(objectname)','refs/remotes')[0].splitlines() if line)

def record_remote_changes(repo,r,before):
    after=remote_refs(repo);changes=[]
    for ref in sorted(set(before)|set(after)):
        old,new=before.get(ref),after.get(ref)
        if old==new:continue
        row=dict(ref=ref,before=old,after=new)
        if old and new:
            row['new_commits']=git(repo,'log','--format=%H %s',old+'..'+new)[0].splitlines()
        else:row['comparison']='new-or-removed-ref; no previous range'
        changes.append(row)
    r['remote_ref_changes']=changes

def sync(repo,apply):
    r={'repo':str(repo.relative_to(ROOT)),'action':'pending'}
    refs_before=None
    try:
        r['before']=git(repo,'rev-parse','HEAD')[0]
        r['branch']=git(repo,'branch','--show-current')[0]
        dirty=bool(git(repo,'status','--porcelain=v1','--untracked-files=no')[0])
        r['dirty']=dirty
        remote_names=git(repo,'remote')[0].splitlines()
        if not remote_names:r['action']='no-remote';return r
        upstream,rc=git(repo,'rev-parse','--abbrev-ref','--symbolic-full-name','@{upstream}',check=False)
        r['upstream']=upstream if rc==0 else None
        superproject=git(repo,'rev-parse','--show-superproject-working-tree')[0]
        if not apply:r['action']='planned';return r
        refs_before=remote_refs(repo)
        # A Linux reference need not transfer unrelated branch/tag history on
        # every cycle. Refresh its configured tracking ref; pinned clones have
        # no tracking branch and retain the all-remotes fetch behavior.
        tracking=git(repo,'rev-parse','--symbolic-full-name','@{upstream}',check=False)[0] if rc==0 else ''
        remote=git(repo,'config','--get','branch.'+r['branch']+'.remote',check=False)[0] if r['branch'] else ''
        merge=git(repo,'config','--get','branch.'+r['branch']+'.merge',check=False)[0] if r['branch'] else ''
        if tracking.startswith('refs/remotes/') and remote in remote_names and merge.startswith('refs/heads/'):
            git(repo,'fetch','--no-tags','--recurse-submodules=no',remote,'+'+merge+':'+tracking,timeout=120)
        else:git(repo,'fetch','--all','--no-tags','--recurse-submodules=no',timeout=120)
        if not r['branch']:r['action']='fetched-detached'
        elif superproject:r['action']='fetched-pinned-submodule'
        elif dirty:r['action']='fetched-dirty'
        elif not r['upstream']:r['action']='fetched-no-upstream'
        else:
            ahead,behind=map(int,git(repo,'rev-list','--left-right','--count','HEAD...'+upstream)[0].split())
            r.update(ahead=ahead,behind=behind)
            if ahead and behind:r['action']='fetched-diverged'
            elif not behind:r['action']='local-ahead' if ahead else 'already-current'
            elif git(repo,'rev-parse','HEAD')[0]!=r['before'] or git(repo,'status','--porcelain=v1','--untracked-files=no')[0]:
                r['action']='changed-during-sync'
            else:
                git(repo,'merge','--ff-only','--no-edit','--',upstream)
                r['action']='fast-forwarded'
        r['after']=git(repo,'rev-parse','HEAD')[0]
        if r['before']!=r['after']:
            r['new_commits']=git(repo,'log','--format=%h %s',r['before']+'..'+r['after'])[0].splitlines()
        if r['upstream']:
            r['remote_head']=git(repo,'rev-parse',r['upstream'])[0]
    except Exception as e:r.update(action='error',error=redact(str(e)))
    if refs_before is not None:
        try:record_remote_changes(repo,r,refs_before)
        except Exception as e:r['remote_ref_audit_error']=redact(str(e))
    return r

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--apply',action='store_true')
    p.add_argument('--workers',type=int,default=4,choices=range(1,5))
    p.add_argument('--repo',action='append',help='Exact research clone directory name; omission selects all')
    a=p.parse_args()
    base=ROOT/'edl_work/reference-sync';base.mkdir(exist_ok=True)
    lock=(base/'sync.lock').open('a+b');lock.seek(0)
    if lock.read(1)==b'':lock.write(b'0');lock.flush()
    lock.seek(0)
    try:
        if os.name=='nt':
            import msvcrt
            msvcrt.locking(lock.fileno(),msvcrt.LK_NBLCK,1)
        else:
            import fcntl
            fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
    except OSError:print('Another reference synchronization is running.');return
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S')
    out=base/stamp;out.mkdir()
    repos=discover();results=[]
    if a.repo:
        unknown=set(a.repo)-{r.name for r in repos}
        if unknown:raise SystemExit('Unknown research clones: '+', '.join(sorted(unknown)))
        repos=[r for r in repos if r.name in a.repo]
    print(json.dumps(dict(repositories=len(repos),apply=a.apply,output=str(out))),flush=True)
    # The discovered clones are separate workspaces. Git's own locks remain in
    # force; no reset, clean, stash, branch switch, submodule update or push.
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.workers) as pool:
        futures={pool.submit(sync,r,a.apply):r for r in repos}
        for future in concurrent.futures.as_completed(futures):
            r=future.result();results.append(r)
            print(r['repo']+': '+r['action'],flush=True)
            (out/'progress.json').write_text(json.dumps(results,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    counts={k:sum(r['action']==k for r in results) for k in sorted(set(r['action'] for r in results))}
    report=dict(utc=stamp,applied=a.apply,scope=str(RESEARCH),counts=counts,repositories=sorted(results,key=lambda r:r['repo']))
    (out/'report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    (base/'latest.json').write_text(json.dumps(dict(report=str(out/'report.json'),counts=counts),indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(report=str(out/'report.json'),counts=counts),indent=2),flush=True)
    lock.close()
    if counts.get('error'):sys.exit(1)

if __name__=='__main__':main()
