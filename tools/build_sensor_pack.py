"""Build an identity-bound readonly Y700 sensor pack from reviewed Android dumps.

No device access, archive extraction, donor defaults or implied calibration.
Use a capture manifest binding every source/path/length/hash, OEM library path,
device/build and ADSP firmware identities. Synthetic peers are host-only and
produce no production input header. Every output directory must be new.
"""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,re,struct,sys,tarfile
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
MAX_PACK=64<<20;MAX_FILE=32<<20;MAX_ENTRIES=8192
IDENTITY_KEYS=('device','build','adsp','adsp_dtb')
def sha(b):return hashlib.sha256(b).hexdigest()
def digest(s):
    if not isinstance(s,str) or not re.fullmatch(r'[0-9a-f]{64}',s) or not int(s,16):raise ValueError('Nonzero lowercase SHA256 required')
    return s
def path(name):
    if not isinstance(name,str) or not name.startswith('/') or name.startswith('//') or any(ord(c)<32 or ord(c)>=127 or c=='\\' for c in name):raise ValueError('Canonical ASCII absolute path required')
    p=PurePosixPath(name)
    if '..' in p.parts or str(p)!=name or (name!='/' and name.endswith('/')) or len(name.encode())>=256:raise ValueError('Ambiguous/traversal/oversized path')
    if name.startswith('/system/vendor/') or name=='/system/vendor' or name.startswith('/persist/') or name=='/persist':raise ValueError('Captured canonical paths required')
    return name
def archive_name(name):
    if not isinstance(name,str) or not name or '\\' in name or '\0' in name:raise ValueError('Bad archive member path')
    if name.startswith('./'):name=name[2:]
    if name.startswith('/') or name.startswith('./') or any(ord(c)<32 or ord(c)>=127 for c in name):raise ValueError('Ambiguous archive path')
    p=PurePosixPath(name)
    if '..' in p.parts or str(p)!=name.rstrip('/') or name.endswith('//'):raise ValueError('Unsafe archive path')
    return str(p)
def allowed(name,kind):
    groups={'configuration':('/vendor/etc/sensors/',),'registry':('/mnt/vendor/persist/sensors/',),
            'soc':('/sys/devices/soc0/',),'library':('/vendor/dsp/','/vendor/lib/rfsa/adsp/','/odm/lib/rfsa/adsp/')}
    return kind in groups and any(name.startswith(prefix) for prefix in groups[kind])
def join_source(root,name):
    if not isinstance(name,str) or not name or '\\' in name:raise ValueError('Input source must be capture-relative')
    p=PurePosixPath(name)
    if p.is_absolute() or '..' in p.parts or str(p)!=name:raise ValueError('Unsafe input source')
    full=root.joinpath(*p.parts).resolve(strict=True)
    if not full.is_relative_to(root) or not full.is_file():raise ValueError('Source escaped dump directory')
    return full
def read_sources(dump,record):
    rows=record.get('files');sources=record.get('sources')
    if not isinstance(rows,list) or not rows or len(rows)>MAX_ENTRIES or not isinstance(sources,dict) or not sources:raise ValueError('Capture sources/file inventory required')
    used=set();result={};opened={};captured={};raw_sources={};total=0
    for row in rows:
        if not isinstance(row,dict):raise ValueError('Malformed capture entry')
        name=path(row.get('path'));kind=row.get('kind');source=row.get('source');expected=digest(row.get('sha256'));length=row.get('bytes')
        if not allowed(name,kind) or name in result or not isinstance(length,int) or isinstance(length,bool) or length<0 or length>MAX_FILE:raise ValueError('Bad/duplicate/out-of-scope capture entry')
        if source not in sources:raise ValueError('Unbound capture source')
        full=join_source(dump,source);metadata=sources[source]
        if not isinstance(metadata,dict) or metadata.get('type') not in ('tar','file'):raise ValueError('Source type must be tar or file')
        if source not in captured:
            if full.stat().st_size>MAX_PACK:raise ValueError('Captured input exceeds bound')
            raw=full.read_bytes()
            if len(raw)>MAX_PACK or sha(raw)!=digest(metadata.get('sha256')) or len(raw)!=metadata.get('bytes'):raise ValueError('Captured archive/file identity differs')
            captured[source]={'path':source,'bytes':len(raw),'sha256':sha(raw),'type':metadata['type']}
            raw_sources[source]=raw
            if metadata['type']=='tar':
                import io
                members={};tar=tarfile.open(fileobj=io.BytesIO(raw))
                try:
                    inventory=tar.getmembers()
                    if len(inventory)>MAX_ENTRIES:raise ValueError('Archive member inventory exceeds bound')
                    for member in inventory:
                        canonical=archive_name(member.name)
                        if canonical in members:raise ValueError('Duplicate archive member')
                        if not member.isdir() and not member.isfile():raise ValueError('Links/special archive members require a resolved original capture')
                        if member.isfile() and member.name.endswith('/'):raise ValueError('File member has directory spelling')
                        if member.size<0 or member.size>MAX_FILE:raise ValueError('Archive member too large')
                        members[canonical]=member
                    opened[source]=(tar,members)
                except Exception:
                    tar.close();raise
        if metadata['type']=='file':
            if row.get('member') is not None:raise ValueError('Raw source cannot name tar member')
            b=raw_sources[source]
        else:
            member_name=archive_name(row.get('member'));tar,members=opened[source];member=members.get(member_name)
            if not member or not member.isfile():raise ValueError('Captured file member absent')
            if member.size!=length:raise ValueError('Member length differs')
            b=tar.extractfile(member).read(MAX_FILE+1)
        if len(b)!=length or sha(b)!=expected:raise ValueError('Captured file bytes differ')
        total+=len(b)
        if total>MAX_PACK:raise ValueError('Pack payload too large')
        result[name]=b;used.add(source)
    for tar,_members in opened.values():tar.close()
    if used!=set(sources):raise ValueError('Unconsumed/unbound capture source')
    if '/vendor/etc/sensors/sns_reg_config' not in result or not result['/vendor/etc/sensors/sns_reg_config']:raise ValueError('Actual sns_reg_config missing')
    registry={row['path']:result[row['path']] for row in rows if row.get('kind')=='registry'}
    if not registry or not any(registry.values()):raise ValueError('Actual registry/calibration files missing')
    return result,registry,captured
def registry_hash(files):
    h=hashlib.sha256()
    for name in sorted(files):
        h.update(name.encode()+b'\0');h.update(struct.pack('<Q',len(files[name])));h.update(hashlib.sha256(files[name]).digest())
    return h.hexdigest()
def c_digest(s):return '{'+','.join('0x'+s[i:i+2] for i in range(0,64,2))+'}'
def build(dump,capture,output,host_fixture=False):
    dump=Path(dump).resolve(strict=True);capture=Path(capture).resolve(strict=True);output=Path(output).resolve()
    record=json.loads(capture.read_text(encoding='utf-8'));identity=record.get('identity')
    if record.get('schema')!=1 or record.get('board')!='lenovo-tb322fc' or record.get('soc')!='SM8750' or not isinstance(identity,dict):raise ValueError('Exact Gen4 capture identity required')
    observed=record.get('provenance')
    if observed=='host-fixture':
        if not host_fixture:raise ValueError('Synthetic fixture cannot be used as candidate source')
    elif observed!='android-observed' or record.get('calibration_observed') is not True:raise ValueError('Actual Android/calibration capture required')
    for n in IDENTITY_KEYS:digest(identity.get(n+'_sha256'))
    files,registry,sources=read_sources(dump,record)
    library=record.get('library_path')
    if not isinstance(library,str) or not library or len(library.encode())>=191 or any(not part or path(part)!=part for part in library.split(';')):raise ValueError('Actual canonical ADSP_LIBRARY_PATH required')
    for directory in library.split(';'):
        if not any(directory==p or directory.startswith(p+'/') for p in ('/vendor/dsp','/vendor/lib/rfsa/adsp','/odm/lib/rfsa/adsp')):raise ValueError('Library path outside OEM DSP directories')
    files['/.elden/library_path']=(library+'\n').encode()
    sys.path.insert(0,str(DRIVER));from PackSource import serializer
    pack,reference,function_hash=serializer();blob=pack(files)
    if len(blob)>MAX_PACK:raise ValueError('Final pack exceeds64MiB')
    count,names,total,checksum,_reserved=struct.unpack_from('<IIQII',blob,8);table_size=count*24+names
    if total!=len(blob):raise ValueError('Original serializer emitted wrong size')
    namespace=sha(blob[32:32+table_size]);registry_digest=registry_hash(registry)
    actual_identity={n+'_sha256':identity[n+'_sha256'] for n in IDENTITY_KEYS};actual_identity['registry_sha256']=registry_digest
    seal={'version':1,'bytes':len(blob),'sha256':sha(blob),'namespace_sha256':namespace,'identity':actual_identity}
    validation={'passed':True,'capture_manifest_sha256':sha(capture.read_bytes()),'source_record_sha256':sha((DRIVER/'pack-source.json').read_bytes()),
        'original_serializer_sha256':function_hash,'reference':reference,'sources':sources,'files':{n:{'bytes':len(b),'sha256':sha(b)} for n,b in files.items()},
        'seal':seal,'host_fixture':observed=='host-fixture','production_header_emitted':observed!='host-fixture','hardware_tested':False}
    output.mkdir(parents=True,exist_ok=False)
    (output/'pack.bin').write_bytes(blob)
    (output/'seal.json').write_text(json.dumps(seal,indent=2)+'\n',encoding='utf-8',newline='\n')
    (output/'record.json').write_text(json.dumps(validation,indent=2)+'\n',encoding='utf-8',newline='\n')
    # Binary fixture is a test-only C layout, explicitly marked. Production
    # C input uses actual reviewed hex bytes and keeps identities out of Git.
    if observed=='host-fixture':
        sealed=struct.pack('<I4xQ',1,len(blob))+bytes.fromhex(seal['sha256'])+bytes.fromhex(namespace)+b''.join(bytes.fromhex(actual_identity[n+'_sha256']) for n in (*IDENTITY_KEYS,'registry'))
        (output/'seal.host-only.bin').write_bytes(sealed)
    else:
        text='/* Private actual Android input; never commit device/calibration identities. */\n#include "NativePack.h"\nstatic const EldenSensorPackSeal elden_sensor_pack_input = {1,UINT64_C('+str(len(blob))+'),'+c_digest(seal['sha256'])+','+c_digest(namespace)+',{'+','.join(c_digest(actual_identity[n+'_sha256']) for n in (*IDENTITY_KEYS,'registry'))+'}};\n'
        (output/'EldenSensorPackInputs.h').write_text(text,encoding='utf-8',newline='\n')
    return validation
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('dump','capture','output'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--host-fixture',action='store_true');a=p.parse_args();r=build(a.dump,a.capture,a.output,a.host_fixture)
    print(json.dumps({'passed':True,'files':len(r['files']),'bytes':r['seal']['bytes'],'sha256':r['seal']['sha256'],'host_fixture':r['host_fixture'],'production_header':r['production_header_emitted']}))
if __name__=='__main__':main()
