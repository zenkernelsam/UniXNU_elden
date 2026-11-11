"""Exercise real local Git state transitions; retain the temporary test directory."""
from pathlib import Path
import json,subprocess,tempfile
import sync_reference_repos as sync

def git(p,*args):
    return subprocess.check_output(['git','-C',str(p),*args],stderr=subprocess.STDOUT,text=True).strip()

def main():
    root=Path(tempfile.mkdtemp(prefix='y700-reference-sync-test-'))
    remote=root/'remote.git';publisher=root/'publisher';research=root/'research';research.mkdir()
    git(root,'init','--bare',str(remote));git(root,'clone',str(remote),str(publisher))
    git(publisher,'config','user.name','Local test');git(publisher,'config','user.email','local-test@example.invalid')
    git(publisher,'checkout','-b','main');(publisher/'one').write_text('one')
    git(publisher,'add','one');git(publisher,'commit','-m','one');git(publisher,'push','-u','origin','main')
    git(remote,'symbolic-ref','HEAD','refs/heads/main')
    clones={}
    for name in ('clean','untracked','detached','diverged'):
        p=research/name;git(root,'clone',str(remote),str(p));clones[name]=p
    before=git(clones['clean'],'rev-parse','HEAD')
    (clones['untracked']/'keep-local').write_text('do not overwrite')
    git(clones['detached'],'checkout','--detach')
    d=clones['diverged'];git(d,'config','user.name','Local test');git(d,'config','user.email','local-test@example.invalid')
    (d/'local').write_text('local');git(d,'add','local');git(d,'commit','-m','local')
    local_head=git(d,'rev-parse','HEAD')
    (publisher/'two').write_text('two');git(publisher,'add','two');git(publisher,'commit','-m','two');git(publisher,'push')
    after=git(publisher,'rev-parse','HEAD');sync.ROOT=root;sync.RESEARCH=research
    assert len(sync.discover())==4
    results={n:sync.sync(p,True) for n,p in clones.items()}
    assert results['clean']['action']=='fast-forwarded' and git(clones['clean'],'rev-parse','HEAD')==after
    assert results['untracked']['action']=='fetched-dirty' and git(clones['untracked'],'rev-parse','HEAD')==before
    assert (clones['untracked']/'keep-local').read_text()=='do not overwrite'
    assert results['detached']['action']=='fetched-detached' and git(clones['detached'],'rev-parse','HEAD')==before
    assert results['diverged']['action']=='fetched-diverged' and git(d,'rev-parse','HEAD')==local_head
    (research/'not-a-repository').mkdir()
    try:sync.discover()
    except RuntimeError:pass
    else:raise AssertionError('Non-repository entry accepted')
    report={'actions':{k:v['action'] for k,v in results.items()},'non_repository_rejected':True,'retained_test_directory':str(root)}
    (root/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))

if __name__=='__main__':main()
