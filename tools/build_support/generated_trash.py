"""Recycle only this build's generated paths; used inside isolated generators."""
import json,os,stat,sys,uuid
from pathlib import Path
_installed=False
def retire(path,*,missing_ok=False,recursive=False,dir_fd=None):
    if dir_fd is not None:raise ValueError('Relative-directory deletion is not supported')
    p=Path(os.fsdecode(path)).absolute()
    if p.name in ('.','..',''):raise ValueError('Invalid generated target')
    p=p.parent.resolve()/p.name
    roots=[Path(v).resolve() for v in json.loads(os.environ['ELDEN_GENERATED_ROOTS'])]
    if not any(p!=r and p.is_relative_to(r) for r in roots):raise ValueError('Refuse retirement outside generated roots: '+str(p))
    try:mode=p.lstat().st_mode
    except FileNotFoundError:
        if missing_ok:return
        raise
    if stat.S_ISDIR(mode) and not recursive:raise IsADirectoryError(p)
    if not (stat.S_ISREG(mode) or stat.S_ISDIR(mode) or stat.S_ISLNK(mode)):raise ValueError('Unsupported generated file type')
    trash=Path(os.environ['ELDEN_BUILD_TRASH']).resolve()
    base=(Path.home()/'.Trash').resolve()
    if trash==base or not trash.is_relative_to(base):raise ValueError('Expected private macOS Trash bucket')
    dest=trash/(uuid.uuid4().hex+'-'+p.name)
    journal=Path(os.environ['ELDEN_RETIRE_JOURNAL'])
    entry={'source':str(p),'destination':str(dest),'kind':'link' if stat.S_ISLNK(mode) else 'directory' if stat.S_ISDIR(mode) else 'file'}
    def record(state):
        fd=os.open(journal,os.O_WRONLY|os.O_APPEND|os.O_CREAT,0o600)
        try:os.write(fd,(json.dumps({**entry,'state':state})+'\n').encode())
        finally:os.close(fd)
    record('planned')
    os.rename(p,dest)  # same-volume only; no copy/delete fallback
    record('moved')
def install():
    global _installed
    if _installed:return
    import shutil
    def unlink(path,*,dir_fd=None):return retire(path,dir_fd=dir_fd)
    def rmtree(path,ignore_errors=False,onerror=None,*,onexc=None,dir_fd=None):
        return retire(path,missing_ok=ignore_errors,recursive=True,dir_fd=dir_fd)
    os.unlink=unlink;os.remove=unlink;shutil.rmtree=rmtree;_installed=True
def shell_main():
    recursive=force=False;paths=[];options=True
    for value in sys.argv[1:]:
        if options and value=='--':options=False;continue
        if options and value.startswith('-'):
            if any(c not in 'rfR' for c in value[1:]):raise ValueError('Unsupported rm option')
            recursive|='r' in value or 'R' in value;force|='f' in value
        else:paths.append(value)
    if not paths and not force:raise ValueError('Missing generated path')
    for path in paths:retire(path,missing_ok=force,recursive=recursive)
