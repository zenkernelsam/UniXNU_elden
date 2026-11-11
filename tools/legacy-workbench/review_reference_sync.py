"""Read-only review of a completed sync, including pinned-clone remote refs."""
from pathlib import Path
import argparse,datetime,json,subprocess
r=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('report',type=Path);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
report=json.loads(a.report.read_text());out=a.output.resolve();assert out.is_relative_to(r/'research');out.mkdir(parents=True,exist_ok=True)
before=(datetime.datetime.strptime(report['utc'],'%Y%m%d-%H%M%S').replace(tzinfo=datetime.timezone.utc)-datetime.timedelta(seconds=1)).isoformat()
def git(repo,*args):
 return subprocess.run(['git','-C',str(repo),*args],capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=25)
rows=[]
for item in report['repositories']:
 repo=r/item['repo'];entry=dict(repo=item['repo'],action=item['action'],checkout_before=item.get('before'),checkout_after=item.get('after'),remote_changes=[])
 if item.get('error'):entry['sync_error']=item['error']
 refs=git(repo,'for-each-ref','--format=%(refname) %(objectname) %(symref)','refs/remotes')
 assert refs.returncode==0
 for line in refs.stdout.splitlines():
  fields=line.split();ref,after=fields[:2]
  if len(fields)>2:continue # symbolic HEAD duplicates its target
  old=git(repo,'rev-parse',ref+'@{'+before+'}')
  if old.returncode or old.stderr:
   entry['remote_changes'].append(dict(ref=ref,after=after,baseline_unknown=True,reason=(old.stderr or old.stdout).strip()));continue
  old=old.stdout.strip()
  if old==after:continue
  log=git(repo,'log','--format=%H %s',old+'..'+after);assert log.returncode==0
  stat=git(repo,'diff','--stat',old,after);assert stat.returncode==0
  entry['remote_changes'].append(dict(ref=ref,before=old,after=after,new_commits=log.stdout.splitlines(),diff_stat=stat.stdout))
 if item.get('after') and item['before']!=item['after']:
  log=git(repo,'log','--format=%H %s',item['before']+'..'+item['after']);assert log.returncode==0
  entry['checkout_new_commits']=log.stdout.splitlines()
 rows.append(entry)
result=dict(sync_report=str(a.report),before_time=before,method='remote reflog as-of before sync; unknown baselines explicitly retained; no fetch or checkout changes',repositories=rows)
(out/'remote-deltas.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n')
for e in rows:
 changes=[x for x in e['remote_changes'] if x.get('new_commits')]
 if changes:print(e['repo'],[(x['ref'],len(x['new_commits'])) for x in changes])
print('Reviewed',len(rows),'repositories')
