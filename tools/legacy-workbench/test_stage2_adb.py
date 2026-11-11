"""WSL: exercise ADB orchestration with an independent file-backed transport.

No adb process is launched. The shell is syntax-checked, never executed.
"""
import base64
import hashlib
import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from stage2_adb import AdbStore
from stage2_store import FileStore, MIB, inventory, transaction

class SimulatedAdb(AdbStore):
    def __init__(self, backing, mode='root', bad_identity=False):
        self.backing=backing;self.mode=mode;self.bad_identity=bad_identity
        self.upload=None;self.scripts=[];self.uploads=[];self.fail_stage=False
        super().__init__()

    def adb(self,*arguments,timeout=300):
        if arguments[0]=='push':
            self.upload=Path(arguments[1]).read_bytes();self.uploads.append(len(self.upload));return ''
        assert arguments[0]=='shell'
        command=arguments[1]
        if command=='id -u':return '0\n' if self.mode=='root' else '2000\n'
        if command.startswith('rm -f '):self.upload=None;return ''
        parts=shlex.split(command)
        if parts[:2]==['su','-c']:parts=shlex.split(parts[2])
        assert parts[:2]==['sh','-c'] and len(parts)==3
        script=parts[2];self.scripts.append(script)
        # Syntax-only validation, no commands from the script are executed.
        subprocess.run(['sh','-n'],input=script,text=True,check=True)
        if 'SCSI=' in script:
            return '/dev/block/sde40\nsde\n0:0:0:'+('3' if self.bad_identity else '4')+'\n'+str(self.backing.size)+'\n'
        if script=='sync':self.backing.sync();return ''
        if 'STAGED=' in script:
            if self.fail_stage:raise IOError('simulated staged hash failure')
            digest=re.search(r'= ([0-9a-f]{64}) \]',script)[1]
            assert hashlib.sha256(self.upload).hexdigest()==digest
            offset=int(re.search(r'\bseek=(\d+)',script)[1])*4096
            self.backing.write(offset,self.upload);return ''
        match=re.search(r'\bskip=(\d+) count=(\d+)',script)
        assert match,script
        offset,size=map(lambda x:int(x)*512,match.groups())
        if script.endswith('| base64'):
            return base64.b64encode(self.backing.read(offset,size)).decode()+'\r\n'
        assert script.endswith('| sha256sum')
        return self.backing.digest(offset,size).hex()+'  -\r\n'

cases=[]
with tempfile.TemporaryDirectory() as directory:
    for mode in ('root','su'):
        path=Path(directory)/(mode+'.img')
        with path.open('wb') as stream:stream.truncate(128*MIB)
        backing=FileStore(path,True)
        remote=SimulatedAdb(backing,mode)
        data=b'a'*(2*MIB)
        transaction(remote,data,initialize=True,commit=True)
        assert remote.uploads==[4096,2*MIB,4096],remote.uploads
        baseline=backing.read(0,4096)
        remote.fail_stage=True
        try:transaction(remote,b'b'*(2*MIB),keep_generation=1,commit=True);raise AssertionError('expected upload failure')
        except IOError:pass
        assert backing.read(0,4096)==baseline
        assert [i.generation for i in inventory(backing)[2]]==[1]
        remote.fail_stage=False
        transaction(remote,b'b'*(2*MIB),keep_generation=1,commit=True)
        assert [i.generation for i in inventory(backing)[2]]==[1,2]
        assert backing.read(0,4096)==baseline
        try:SimulatedAdb(backing,mode,bad_identity=True);raise AssertionError('wrong LUN accepted')
        except RuntimeError:pass
        cases.append(dict(mode=mode,whole_payload_single_push=True,staging_failure_preserved_old=True,
                          wrong_lun_rejected=True,shell_scripts_syntax_checked=len(remote.scripts)))
        backing.close()
out=Path(__file__).resolve().parent/'ufs-stage2-20261106/adb-model-tests.json'
out.write_text(json.dumps(dict(cases=cases,real_adb_executed=False,hardware_tested=False),indent=2)+'\n')
print(out.read_text())
