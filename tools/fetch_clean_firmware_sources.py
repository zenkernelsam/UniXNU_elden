"""Fetch the exact public firmware sources for build_clean into fresh clones.

Existing destinations are refused, never reset/stashed or repointed. Original
Crypto/Qualcomm binary inputs retain their separate documented requirements.
"""
from pathlib import Path
import argparse,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);p.add_argument('--plan-only',action='store_true');a=p.parse_args()
    plan=json.loads((ROOT/'profiles/v91/firmware-build/plan.json').read_text())
    sources=[('mu_wide_platforms','https://github.com/sunflower2333/mu_wide_platforms.git',plan['mu_commit']),
             ('UniXNU','https://github.com/UniXNU/UniXNU.git',plan['unixnu_commit'])]
    if a.plan_only:
        print(json.dumps([{'directory':n,'url':u,'commit':c} for n,u,c in sources],indent=2));return
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    if any((out/n).exists() for n,_,_ in sources):raise ValueError('A source destination exists; choose a fresh directory or supply your existing pinned clones explicitly')
    def run(args):subprocess.run(list(map(str,args)),check=True)
    for name,url,commit in sources:
        dest=out/name;run(['git','-c','http.version=HTTP/1.1','clone','--no-checkout',url,dest]);run(['git','-C',dest,'checkout','--detach',commit])
        args=['git','-C',dest,'-c','http.version=HTTP/1.1','submodule','update','--init','--recursive']
        if name=='UniXNU':args+=['--','drivers/DarwinBoot']
        run(args)
    actual=subprocess.check_output(['git','-C',out/'UniXNU/drivers/DarwinBoot','rev-parse','HEAD'],text=True).strip()
    if actual!=plan['darwin_commit']:raise ValueError('Pinned DarwinBoot submodule mismatch')
    print('Source clones ready. Set mu_repository and unixnu_repository in your input JSON.')
if __name__=='__main__':main()
