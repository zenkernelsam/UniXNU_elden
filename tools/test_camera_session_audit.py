"""Focused negatives for the session-link audit required-edge check."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import audit_camera_session_link as a
# Synthetic image layout: one 0x100-byte method range per owner symbol and a
# distinct target address per required leaf.
names=[*a.SESSION.values(),a.CLIENT_CLOSE,a.CLIENT_FREE,'_elden_camera_service_withdraw','_evil_caller']
addr={n:0x100000+0x1000*i for i,n in enumerate(names)}
addr.update({leaf:0x400000+0x1000*i for i,leaf in enumerate(a.REQUIRED)})
def bounds_of(n):
    if n==a.CLIENT_ANY:return [(addr[a.CLIENT_CLOSE],addr[a.CLIENT_CLOSE]+0x100),(addr[a.CLIENT_FREE],addr[a.CLIENT_FREE]+0x100)]
    return [(addr[n],addr[n]+0x100)]
def symbol_of(n):return addr[n]
def good():
    calls=[]
    for leaf,(mandatory,allowed) in a.REQUIRED.items():
        for owner in mandatory:calls.append((addr[owner]+8,addr[leaf]))
    return calls
report=a.check_required_edges(good(),bounds_of,symbol_of)
if len(report)!=len(a.REQUIRED):raise SystemExit('positive coverage incomplete')
def reject(calls,tag):
    try:a.check_required_edges(calls,bounds_of,symbol_of)
    except ValueError as e:print('reject '+tag+': '+str(e));return
    raise SystemExit('session link audit accepted '+tag)
# A missing mandatory owner edge (no shutdown call to output_stop) rejects.
reject([c for c in good() if not (c[1]==addr['_elden_camera_output_stop'] and c[0]==addr[a.SESSION['shutdown']]+8)],'missing mandatory edge')
# A leaf call inside an unapproved function range rejects.
reject(good()+[(addr['_evil_caller']+8,addr['_elden_camera_output_close'])],'wrong caller')
# The retained sensor-only withdraw must have no caller in this composition.
reject(good()+[(addr[a.SESSION['shutdown']]+8,addr['_elden_camera_service_withdraw'])],'sensor-only withdraw')
print('PASS session link audit focused negatives: '+str(len(report))+' leaf APIs with mandatory edges enforced')
