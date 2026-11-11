"""Install compatibility entrypoints after verifying archived source identities."""
from pathlib import Path
import argparse,json,hashlib
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--workbench',type=Path,required=True)
args=parser.parse_args()
w=args.workbench.resolve(strict=True);r=Path(__file__).resolve().parents[2]
names=['avb_guard.py','firehose_transport.py','minifh.py','flash_verified_boot.py','elden_usb_console.py','decode_frame_preview.py']
originals=json.loads((r/'tools/legacy-workbench/import-manifest.json').read_text())
originals={Path(x['workbench_path']).name:x['sha256'] for x in originals['files']}
rows=[];pending=[]
for name in names+['sync_reference_repos.py']:
 p=w/'edl_work'/name;before=hashlib.sha256(p.read_bytes()).hexdigest()
 folder='maintenance' if name=='sync_reference_repos.py' else 'device'
 target=r/'tools'/folder/name;assert target.exists()
 launcher='''"""Compatibility launcher; maintained implementation lives in UniXNU_elden."""
from pathlib import Path
import os,runpy,sys
_workbench=Path(__file__).resolve().parents[1]
_source=Path(os.environ.get('Y700_SOURCE',str(_workbench.parents[1]/'UniXNU_elden'))).resolve()
_directory=_source/'tools'/FOLDER
_target=_directory/NAME
if not _target.is_file():raise RuntimeError('Canonical tool is missing: '+str(_target))
sys.path.insert(0,str(_directory))
if __name__=='__main__' and NEED_WORKBENCH and '--workbench' not in sys.argv:
    sys.argv+=['--workbench',str(_workbench)]
globals().update(runpy.run_path(str(_target),run_name=__name__))
'''.replace('FOLDER',repr(folder)).replace('NAME',repr(name)).replace('NEED_WORKBENCH',str(name in ('flash_verified_boot.py','sync_reference_repos.py')))
 expected=hashlib.sha256(launcher.encode()).hexdigest()
 if before not in (originals[name],expected):raise RuntimeError('Locally modified tool must be reviewed before replacement: '+str(p))
 pending.append((p,launcher))
 rows.append({'path':str(p),'original_sha256':originals[name],'archived_at':str(r/'tools/legacy-workbench'/name),'canonical':str(target),'launcher_sha256':expected})
for p,launcher in pending:
 if p.read_bytes()!=launcher.encode():p.write_text(launcher,encoding='utf-8',newline='\n')
(w/'maintenance/cleanup-20261111').mkdir(parents=True,exist_ok=True)
(w/'maintenance/cleanup-20261111/tool-launchers.json').write_text(json.dumps(rows,indent=2)+'\n')
print('Installed '+str(len(rows))+' canonical compatibility launchers; original recipes preserved in Git.')
