"""Focused negatives for the session-aware full-link boundary."""
import sys,hashlib,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import audit_camera_session_link as a
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def write(path,data):path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);return str(path)
tmp=Path(tempfile.mkdtemp(prefix='camera-session-link-'))
before_dir=tmp/'parent';after_dir=tmp/'build'
before={};after={};report={'actual_original_camera_IRQ_executor':True,'camera_irq_direct_deltas':{},'camera_irq_builds':{},'inherited_inputs':{}}
# Retained peers: byte-identical in both commands, receipt-pinned.
for name in ('CameraDmaBinding.o','Peer.o'):
    p=write(before_dir/name,name.encode());before[name]=p;after[name]=p
    report['inherited_inputs'][p]=sha(p)
# Approved replacements: distinct rebuilt objects bound to declared receipts.
build_sha={'pipeline_build':'p'*64,'frames_build':'f'*64}
receipt={'NativePipeline.o':build_sha['pipeline_build'],'FrameOutput.o':build_sha['frames_build'],'Service.o':build_sha['frames_build']}
for name,kind in (('NativePipeline.o','pipeline_build'),('FrameOutput.o','frames_build'),('Service.o','frames_build')):
    old=write(before_dir/name,b'old-'+name.encode());new=write(after_dir/name,b'new-'+name.encode())
    before[name]=old;after[name]=new
    report['inherited_inputs'][old]=sha(old);report['inherited_inputs'][new]=sha(new)
    report['camera_irq_direct_deltas'][name]={'before_path':old,'before':sha(old),'after_path':new,'after':sha(new),'source_build_sha256':receipt[name]}
    report['camera_irq_builds'][name]=receipt[name]
session_obj=write(after_dir/'CaptureSession.o',b'session')
session_build={'object_sha256':sha(session_obj)}
def command(mapping):
    return ['clang++','-o','/out/native.macho',mapping['CameraDmaBinding.o'],mapping['Peer.o'],
            mapping['NativePipeline.o'],mapping['FrameOutput.o'],mapping['Service.o']]
before_cmd=command(before)
good_cmd=command(after)+[session_obj]
session_path=a.check_link_delta(before_cmd,good_cmd,report,session_build)
if str(session_path)!=session_obj:raise SystemExit('positive link delta did not resolve session object')
def reject(cmd,rep,tag):
    try:a.check_link_delta(before_cmd,cmd,rep,session_build)
    except ValueError as e:print('reject '+tag+': '+str(e));return
    raise SystemExit('session link delta accepted '+tag)
# An undeclared object delta: an extra non-approved input joins the link.
extra=write(after_dir/'ExtraCamera.o',b'extra')
reject(command(after)+[session_obj,extra],report,'undeclared object delta')
# A duplicate session object: exactly one CaptureSession.o may be linked.
reject(command(after)+[session_obj,session_obj],report,'duplicate session object')
# A changed retained input: bytes differ from the inherited-input receipt.
import copy;rep2=copy.deepcopy(report);rep2['inherited_inputs'][before['Peer.o']]='0'*64
reject(good_cmd,rep2,'changed retained input')
print('PASS session link delta focused negatives: approved replacements + single session object enforced')
