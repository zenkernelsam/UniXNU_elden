"""Prepare qualifications for the fixed diagnostics-only v57 policy capture."""
from pathlib import Path
import json,hashlib,difflib
root=Path(__file__).resolve().parents[1];sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
old=root/'research/basic-bundle-20261108/source-compiled-v56';new=root/'research/basic-bundle-20261108/source-compiled-v57'
base=root/'edl_work/basic-bundle-20261108/v56/final/dc-noop-focused-gestures'
out=root/'edl_work/basic-bundle-20261108/v57/final/pd-policy-trace-focused-gestures';out.mkdir(exist_ok=True)
reviewdir=root/'research/pd-policy-20261110';review=json.loads((reviewdir/'source-review.json').read_text())
allowed={'power/'+n:q['new'] for n,q in review['changed'].items()}
recovery=new/'usb/EldenSdamRecovery.inc'
expected=(old/'usb/EldenSdamRecovery.inc').read_text().replace('extern "C" int elden_dc_noop_request_stop(void) __attribute__((weak_import));','extern "C" int elden_dc_noop_request_stop(void) __attribute__((weak_import));\nextern "C" int elden_ulog_filter_request_stop(void) __attribute__((weak_import));')
hook='''  if(elden_ulog_filter_request_stop){
    int stopped=elden_ulog_filter_request_stop();
    eldenSdamLog(stopped?"\\nELDEN_ULOG_FILTER stop=1; cold log defaults acknowledged\\n":
        "\\nELDEN_ULOG_FILTER stop=0; diagnostics restoration unconfirmed, continuing reset\\n");
  }
'''
marker='static void eldenRecoveryShutdown(void){\n  __atomic_store_n(&gEldenRpcHoldLogs,0,__ATOMIC_RELEASE);\n';assert expected.count(marker)==1
assert expected.replace(marker,marker+hook)==recovery.read_text()
allowed['usb/EldenSdamRecovery.inc']=sha(recovery)
for filename in ['backlight-ui-qualification.json','battery-publication-contract.json','thermal-qualification.json','pd-qualification.json','pd-bootstrap-qualification.json','ucsi-events-qualification.json']:
 q=json.loads((base/filename).read_text());key='source_sha256' if filename=='battery-publication-contract.json' else 'sources';prefix='' if filename.startswith('backlight-') else 'power/';changed={}
 for n,h in q[key].items():
  rel=prefix+n;assert sha(old/rel)==h
  updated=sha(new/rel)
  if updated!=h:assert allowed.get(rel)==updated,rel;changed[rel]={'old':h,'new':updated};q[key][n]=updated
 q['parent_qualification_sha256']=sha(base/filename);q['reviewed_changed_sources']=changed;q['policy_trace_review_sha256']=sha(reviewdir/'source-review.json')
 if filename=='pd-bootstrap-qualification.json':q['previous_archive_sha256']=q['archive_sha256'];q['archive_sha256']=json.loads((out.parents[1]/'ulog-filter-codegen/audit.json').read_text())['archive_sha256']
 if filename=='ucsi-events-qualification.json':q['native_codegen_sha256']=sha(out.parents[1]/'ucsi-codegen/audit.json')
 (out/filename).write_text(json.dumps(q,indent=2)+'\n')
codegen=out.parents[1]/'ulog-filter-codegen/audit.json';code=json.loads(codegen.read_text());assert code['accepted']
q=dict(accepted=True,mode=3,control_set=True,only_diagnostic_set=True,power_controls_added=False,filter_checks=163,paging_checks=326,protocol_checks=24643,transport_checks=29142,
 fixed_reply_bytes=8204,global_tx_message_bytes=560,boot_request_cap=12,set_request_cap=2,interval_ms=5000,
 diagnostic_mask='0x402000020',level=3,restore_mask=0,restore_level=4,hardware_tested=False,sources=code['sources'],
 native_codegen_sha256=sha(codegen),protocol_proof_sha256=sha(reviewdir/'filter-protocol-proof.json'),recovery_source_sha256=sha(recovery),source_review_sha256=sha(reviewdir/'source-review.json'))
(out/'ulog-qualification.json').write_text(json.dumps(q,indent=2)+'\n');print('Prepared fixed policy trace qualifications; no hardware test implied')
