"""Compare audited source bodies; no build, no device access, no input change."""
from pathlib import Path
import hashlib,json,re,subprocess,os
root=Path(__file__).resolve().parents[1]
repo=root/'research/ShengHidInput';out=root/'research/blocker-audit-20261111'
pin='5f3a37cec0c3fc4234b10726b25489a55f0d77ef'
env={**os.environ,'GIT_NO_LAZY_FETCH':'1'}
def show(rev):return subprocess.check_output(['git','-C',str(repo),'show',rev+':NativeHidInput.cpp'],env=env).decode()
old,new=show(pin),show('HEAD')
frozen=root/'research/basic-bundle-20261108/source-compiled-v72/tree/build/sheng-hid-input/src/NativeHidInput.cpp'
local=frozen.read_text()
def body(s):return s.split('    void touchSend() {',1)[1].split('    // The button driver',1)[0]
def norm(s):return re.sub(r'\s+',' ',s).strip()
# The latest generic helper still calls handleReport(report,0,0) in legacy mode.
newbody=body(new).replace('self->submitReport(buffer)','self->handleReport(buffer,0,0)')
boardbody=body(old).replace('transformCoordinate(xs[i],30480)','transformCoordinate(xs[i],32768)').replace('transformCoordinate(ys[i],20320)','transformCoordinate(ys[i],32768)')
def desc(s):return s.split('#define FINGER_DESC',1)[1].split('#define TOUCH_FINGERS',1)[0]
row={
 'pin':pin,'latest':subprocess.check_output(['git','-C',str(repo),'rev-parse','HEAD'],env=env).decode().strip(),
 'frozen_path':str(frozen.relative_to(root)),'frozen_sha256':hashlib.sha256(frozen.read_bytes()).hexdigest(),
 'latest_touchsend_equals_pin_after_legacy_submit_helper':norm(newbody)==norm(body(old)),
 'v72_touchsend_equals_pin_after_elden_normalized_coordinate_ranges':norm(body(local))==norm(boardbody),
 'v72_touch_descriptor_equals_pin':norm(desc(local))==norm(desc(old)),
 'latest_touch_descriptor_equals_pin':norm(desc(new))==norm(desc(old)),
 'limits':['Source comparison only; no new hardware acceptance.','Does not compare producer, per-device event wrapper or all class lifecycle code.','macOS/PAC branches and boot argument rename remain separate adaptations.']}
(out/'hid-source-comparison.json').write_text(json.dumps(row,indent=2)+'\n')
print(json.dumps(row,indent=2))
