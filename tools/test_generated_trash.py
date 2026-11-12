"""Exercise recoverable generated-file retirement without deleting the fixtures."""
import argparse,json,os,sys,uuid
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent/'build_support'))
from generated_trash import retire
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve()
    if out.exists():raise ValueError('New fixture directory required')
    out.mkdir(parents=True);generated=out/'generated';generated.mkdir();outside=out/'outside.txt';outside.write_text('protected')
    trash=Path.home()/'.Trash'/('Y700-retire-test-'+uuid.uuid4().hex);trash.mkdir()
    journal=out/'receipts.jsonl'
    os.environ.update(ELDEN_GENERATED_ROOTS=json.dumps([str(generated)]),ELDEN_BUILD_TRASH=str(trash),ELDEN_RETIRE_JOURNAL=str(journal))
    f=generated/'one.txt';f.write_text('one');retire(f)
    folder=generated/'folder';folder.mkdir();(folder/'two.txt').write_text('two');retire(folder,recursive=True)
    link=generated/'outside-link';link.symlink_to(outside);retire(link)
    assert outside.read_text()=='protected'
    denied=0
    for path in (outside,generated,generated/'../outside.txt'):
        try:retire(path,recursive=True)
        except ValueError:denied+=1
        else:raise AssertionError('Unscoped path accepted')
    retire(generated/'absent',missing_ok=True)
    events=[json.loads(s) for s in journal.read_text().splitlines()];moved=[e for e in events if e['state']=='moved']
    assert len(events)==6 and len(moved)==3 and denied==3
    assert Path(moved[0]['destination']).read_text()=='one'
    assert (Path(moved[1]['destination'])/'two.txt').read_text()=='two'
    assert Path(moved[2]['destination']).is_symlink() and Path(moved[2]['destination']).resolve()==outside
    report={'passed':True,'files_directory_symlink_verified':True,'outside_and_root_refused':denied,'moved_entries':3,'trash_bucket':str(trash),'journal':str(journal),'permanent_deletion':False}
    (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
if __name__=='__main__':main()
