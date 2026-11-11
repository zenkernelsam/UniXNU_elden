"""Require the reviewed opt-in SDP-only command guard and its native objects."""
import hashlib,json
def verify(root,version,d):
 sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
 q=json.loads((d/'usb-handoff-qualification.json').read_text());assert q['accepted'] and q['checks']>=35 and q['legacy_checks']>=77 and q['bootstrap_checks']>=318
 assert q['max_host_budget_ua']==500000 and not q['new_register_controls']
 source=root/f'research/basic-bundle-20261108/source-compiled-v{version}/power'
 for n,h in q['sources'].items():assert sha(source/n)==h,n
 code_path=root/f'edl_work/basic-bundle-20261108/v{version}/pd-codegen/audit.json';assert sha(code_path)==q['native_codegen_sha256']
 code=json.loads(code_path.read_text());build=json.loads((d.parent/'archives/EldenPmicClient/build.json').read_text())
 assert code['accepted'] and code['archive_sha256']==build['archive_sha256']
 for n in ['PmicClient.o','UsbIcl.o']:
  row=code['objects'][n];assert row['archive_member_equal'] and not row['fp_simd'] and row['sha256']==build['objects'][n]
 text=(source/'UsbIcl.c').read_text();assert text.index('if(s->deferred)return 0;')<text.index('put32(out,ELDEN_CHARGE_OWNER)')
 assert 'raw->value[EldenChargeUsbAdapter].raw==1' in text and 'raw->value[EldenChargeUsbRealType].raw==1' in text
 assert 'ucsi.events_ready&&!ucsi.active_connector' in (source/'PmicClient.cpp').read_text()
 return q
