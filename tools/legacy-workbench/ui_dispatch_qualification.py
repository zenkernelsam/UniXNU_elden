"""Binding of the opt-in observation to an actually inspected native kernel."""
import hashlib,json
def verify(root,version,kernel):
 p=root/f'edl_work/basic-bundle-20261108/v{version}/ui-dispatch-codegen.json'
 report=json.loads(p.read_text());digest=hashlib.sha256(kernel).hexdigest()
 assert report['accepted'] and report['controls_unchanged'] and report['compiled_snapshot']
 assert report['kernel_sha256']==digest and report['window_seconds']==30
 if version in (78,79,80):
  from vdog_qualification import verify as verify_vdog
  assert report['recovery_delta']==verify_vdog(root,version,kernel)
 if version in (81,82,83,84,85,86,87,88,89,90,91,92):
  from cpu_guard_qualification import verify as verify_cpu_guard
  assert report['cpu_guard_delta']==verify_cpu_guard(root,version,kernel)
 src=root/f'research/basic-bundle-20261108/source-compiled-v{version}/xnu'
 assert hashlib.sha256((src/'kdebug.c').read_bytes()).hexdigest()==report['kdebug_sha256']
 for n,h in report['ui_sources'].items():assert hashlib.sha256((src/n).read_bytes()).hexdigest()==h
 assert b'ELDEN_UI_DISPATCH done total=' in kernel and b'elden-ui\0' in kernel
 return dict(codegen_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),kernel_sha256=digest,window_seconds=30,
             source='21H461 UIApplication sendEvent tracepoint',hardware_tested=False)
