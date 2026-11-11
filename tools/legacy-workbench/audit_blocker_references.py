"""Read existing Git objects only; classify reference scope without changing refs.

This is a reproducible search index, NOT an assertion that every matching
implementation works on hardware. Human review and applicability live separately.
"""
import concurrent.futures, hashlib, json, re, subprocess, os
from pathlib import Path
from sync_reference_repos import ROOT, discover, git, ENV
ENV['GIT_NO_LAZY_FETCH']='1'
os.environ['GIT_NO_LAZY_FETCH']='1'

OUT = ROOT / 'research/blocker-audit-20261111/repositories'
OUT.mkdir(parents=True, exist_ok=True)
PATTERN = (r'IconServices|iconservices|GLContext|PKIconImageCache|CoreImage|'
           r'long.?press|Contact Identifier|contact.?id|tracking.?id|'
           r'IOHIDEventDriver|IOHIDEventService|clock_get_uptime|'
           r'waitForService|watchdog|SMP|completion.wait|busy.wait|'
           r'software.render|\bstall\w*|blocking|阻塞|卡顿')
LINUX = {'tb322fc-linux-kernel','droidspaces-kernel','hackios-xnu-kernel-sm8750'}
def inspect(repo):
    head=git(repo,'rev-parse','HEAD')[0]
    refs=git(repo,'for-each-ref','--format=%(refname) %(objectname)','refs/remotes')[0].splitlines()
    upstream,rc=git(repo,'rev-parse','--symbolic-full-name','@{upstream}',check=False)
    chosen=['HEAD']
    for ref in ([upstream] if rc==0 else [])+['refs/remotes/origin/HEAD','refs/remotes/origin/main','refs/remotes/origin/master','refs/remotes/origin/night']:
        sha,rc=git(repo,'rev-parse','--verify',ref,check=False)
        if rc==0 and sha not in [git(repo,'rev-parse',r)[0] for r in chosen]:chosen.append(ref)
    row={'repo':str(repo.relative_to(ROOT)), 'head':head, 'remote_refs':refs,
         'tracked_worktree_changes':git(repo,'status','--short','--untracked-files=no')[0].splitlines(),
         'scope':'HEAD and available tracked/default/main/master/night refs; all remote tips inventoried', 'reviews':[]}
    for ref in chosen:
        sha=git(repo,'rev-parse',ref)[0]
        files=git(repo,'ls-tree','-r','--name-only',ref)[0].splitlines()
        # Explicit directories avoid searching unrelated huge Linux subsystems.
        paths=(['README*','drivers/input','drivers/power','drivers/ufs','drivers/usb',
                'arch/arm64/boot/dts/qcom','arch/arm64/kernel','Documentation/input']
               if repo.name in LINUX and any(x.startswith('drivers/input/') for x in files)
               else ['*.md','*.c','*.cpp','*.h','*.py','*.patch','*.toml','*.dts','*.dtsi'])
        if repo.name in ('armadaos-freedom','armada-tb321fu','mwp'):
            paths=['README*','devices','packages/kernel','system_files/usr/lib/armada/devices','docs','patches']
        r=subprocess.run(['git','-C',str(repo),'grep','-n','-I','-E',PATTERN,ref,'--',*paths],capture_output=True,timeout=60)
        output=r.stdout.decode('utf-8','replace')
        tag=repo.name+'-'+sha[:12]
        (OUT/(tag+'.matches.txt')).write_text(output,encoding='utf-8')
        (OUT/(tag+'.files.txt')).write_text('\n'.join(files)+'\n',encoding='utf-8')
        readmes=[p for p in files if re.search(r'(^|/)(README[^/]*|porting\.md|PORTING\.md)$',p)][:10]
        excerpts=[]
        for p in readmes:
            source,read_rc=git(repo,'show',ref+':'+p,check=False)
            if read_rc:source='[object not present locally; no lazy fetch permitted]'
            excerpts.append('### '+p+'\n'+'\n'.join(source.splitlines()[:90]))
        (OUT/(tag+'.overview.txt')).write_text('\n\n'.join(excerpts),encoding='utf-8')
        row['reviews'].append({'ref':ref,'sha':sha,'files':len(files),'paths_searched':paths,
            'match_lines':len(output.splitlines()),'matches_sha256':hashlib.sha256(r.stdout).hexdigest(),
            'evidence':tag, 'search_returncode':r.returncode,'search_errors':r.stderr.decode(errors='replace')[:2000],
            'commits_vs_head':git(repo,'log','--format=%h %s','HEAD..'+ref)[0].splitlines() if ref!='HEAD' else []})
    (OUT/(repo.name+'.json')).write_text(json.dumps(row,indent=2)+'\n',encoding='utf-8')
    return row

if __name__=='__main__':
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        rows=list(pool.map(inspect,discover()))
    (OUT/'index.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf-8')
    for r in rows:
        print(Path(r['repo']).name+': '+'; '.join(x['sha'][:8]+' files='+str(x['files'])+' matches='+str(x['match_lines']) for x in r['reviews']))
