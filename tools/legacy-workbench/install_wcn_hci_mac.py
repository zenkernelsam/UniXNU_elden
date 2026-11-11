"""Install this task's checked ordinary-source archive inside the Mac Y700 repo."""
from pathlib import Path
import hashlib,json,shutil,zipfile
root=Path('/Users/ciscohe/Desktop/UniXNU_elden').resolve()
archive=root/'out/hci-upload.zip'
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='60c5a280e0ccc9c22567b173396dc8ca3ea2f98016e9e44a156c7447b5c54beb'
with zipfile.ZipFile(archive) as z:
    m=json.loads(z.read('out/hci-upload.json'))
    assert set(z.namelist())==set(m['new_files'])|{'out/hci-upload.json'}
    assert len(z.namelist())==len(set(z.namelist()))
    for name,old in m['expected_previous_files'].items():
        f=root/name
        assert f.is_file() and not f.is_symlink()
        assert hashlib.sha256(f.read_bytes()).hexdigest() in (old,m['new_files'][name]),name
    backup=root/'out/hci-source-previous-20261108'
    for name,digest in m['new_files'].items():
        target=(root/name).resolve()
        assert target.is_relative_to(root) and not target.is_symlink(),name
        data=z.read(name);assert hashlib.sha256(data).hexdigest()==digest,name
        if target.exists():
            assert target.is_file(),name
            previous=backup/name
            if not previous.exists():
                previous.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(target,previous)
        target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
print('Installed checked HCI source inputs; existing artifacts preserved')
