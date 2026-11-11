"""Verify rebuilt EFI/debug identity and the packed, source-built Data fixture."""
from pathlib import Path
import hashlib,json,struct
ROOT=Path(__file__).resolve().parents[1];PROFILE=ROOT/'profiles/v91/boot-build'
def verify_loader(path):
    p=json.loads((PROFILE/'loader-contract.json').read_text());data=bytes(path) if isinstance(path,(bytes,bytearray)) else Path(path).read_bytes();at=p['debug_guid_offset'];n=p['debug_guid_bytes']
    if len(data)!=p['bytes'] or data[at-4:at]!=b'RSDS' or hashlib.sha256(data[:at]+bytes(n)+data[at+n:]).hexdigest()!=p['normalized_sha256']:raise ValueError('Loader differs outside the allowed CodeView GUID')
    return {'sha256':hashlib.sha256(data).hexdigest(),'only_codeview_guid_may_differ':True}
def verify_packed_seed(path,report_path):
    profile=json.loads((PROFILE/'data-seed.json').read_text());report=json.loads(Path(report_path).read_text());data=bytes(path) if isinstance(path,(bytes,bytearray)) else Path(path).read_bytes()
    if hashlib.sha256(data).hexdigest()!=report['packed_sha256'] or report['logical_manifest_sha256']!=hashlib.sha256((PROFILE/'data-seed.json').read_bytes()).hexdigest() or report['volume_uuid']!=profile['volume_uuid'] or report['logical_entries_verified']!=len(profile['entries']):raise ValueError('Seed report/manifest identity mismatch')
    magic,version,block_size,total,count=struct.unpack_from('<8sIIQQ',data)
    if (magic,version,block_size,total)!=(b'DBRDISK1',1,4096,536870912) or not 1<=count<=65536:raise ValueError('Invalid packed Data header')
    cursor=32+count*16;end=0;h=hashlib.sha256();zero=bytes(1<<20)
    for i in range(count):
        offset,length=struct.unpack_from('<QQ',data,32+i*16)
        if offset<end or length<=0 or (offset|length)%4096 or offset+length>total or cursor+length>len(data):raise ValueError('Invalid Data extent')
        gap=offset-end
        while gap:take=min(gap,len(zero));h.update(zero[:take]);gap-=take
        h.update(data[cursor:cursor+length]);cursor+=length;end=offset+length
    while end<total:take=min(total-end,len(zero));h.update(zero[:take]);end+=take
    if cursor!=len(data) or h.hexdigest()!=report['raw_sha256']:raise ValueError('Packed Data reconstruction differs from verified image')
    if report.get('fsck_returncode')!=0 or not report.get('apfs_feature_flags_match_baseline'):raise ValueError('Read-only APFS qualification missing')
    return {'packed_sha256':hashlib.sha256(data).hexdigest(),'raw_sha256':h.hexdigest(),'logical_entries_verified':report['logical_entries_verified'],'volume_uuid':report['volume_uuid'],'packed_reconstruction_verified':True}
