"""Host checks for RAM exclusions and root-header construction; no device I/O."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import uuid
from fdt_memory_plan import inspect,subtract,efi_ranges,MIB
from root_image_preflight import prepare,decode_header,build_image,PROFILE

ROOT=Path(__file__).resolve().parents[1];OUT=ROOT/'edl_work/prerequisites-20261106'
OUT.mkdir(exist_ok=True)
def make_fdt():
    strings=bytearray();names={};body=bytearray()
    def word(v):body.extend(struct.pack('>I',v))
    def pad():body.extend(bytes((-len(body))%4))
    def begin(name):word(1);body.extend(name.encode()+b'\0');pad()
    def prop(name,value):
        if name not in names:names[name]=len(strings);strings.extend(name.encode()+b'\0')
        word(3);word(len(value));word(names[name]);body.extend(value);pad()
    begin('');prop('#address-cells',struct.pack('>I',2));prop('#size-cells',struct.pack('>I',2))
    begin('memory');prop('device_type',b'memory\0');prop('reg',struct.pack('>QQQQ',0x800000000,8<<30,0x80000000,0));word(2)
    begin('reserved-memory');prop('#address-cells',struct.pack('>I',2));prop('#size-cells',struct.pack('>I',2))
    begin('secure');prop('reg',struct.pack('>QQ',0x880000000,2<<30));prop('no-map',b'');word(2)
    begin('dynamic');prop('size',struct.pack('>Q',64*MIB));word(2);word(2)
    begin('chosen');prop('linux,initrd-start',struct.pack('>Q',0x800100000));prop('linux,initrd-end',struct.pack('>Q',0x800200000));word(2)
    word(2);word(9)
    reserve=struct.pack('>QQQQ',0x800000000,4096,0,0);start=40+len(reserve);st=start+len(body)
    header=struct.pack('>10I',0xd00dfeed,st+len(strings),start,st,40,17,16,0,len(strings),len(body))
    return header+reserve+body+strings

def apfs(path,identity,extra=0):
    block=bytearray(4096);block[32:36]=b'NXSB';struct.pack_into('<IQ',block,36,4096,256);block[72:88]=uuid.UUID(identity).bytes
    m=0xffffffff;a=b=0
    for value in struct.unpack('<1022I',block[8:]):a=(a+value)%m;b=(b+a)%m
    c=m-((a+b)%m);d=m-((a+c)%m);struct.pack_into('<Q',block,0,(d<<32)|c)
    with path.open('wb') as stream:stream.write(block);stream.truncate(MIB+extra)

data=make_fdt();memory,reserved,unresolved=inspect(data);free=subtract(memory,reserved)
assert (0x900000000,0xa00000000) in free and unresolved==['/reserved-memory/dynamic']
assert all(not(a<0x900000000 and b>0x880000000) for a,b in free)
bad=bytearray(data);struct.pack_into('>I',bad,4,len(bad)+1)
try:inspect(bad);raise AssertionError('truncated FDT accepted')
except ValueError:pass

temp_parent=OUT/'test-temp';temp_parent.mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(dir=temp_parent) as directory:
    folder=Path(directory).resolve();assert temp_parent.resolve() in folder.parents
    log=folder/'efi.txt';log.write_text('ELDEN_MEMORY: conventional base=0x900000000 bytes=0x80000000 attrs=0xf\n')
    assert efi_ranges(log)==[(0x900000000,0x980000000)]
    os_file=folder/'OS.raw';sys_file=folder/'SystemOS.raw'
    apfs(os_file,'11111111-2222-4333-8444-555555555555',512)
    apfs(sys_file,'aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee')
    spec={'profile':PROFILE,'target':{'lun':0,'partition':'ipados','bytes':64*MIB},
          'windows':[{'name':'OS','path':str(os_file)},{'name':'SystemOS','path':str(sys_file)}]}
    header,report=prepare(spec,folder);assert not report['ready_to_flash']
    parsed=decode_header(header,64*MIB);assert len(parsed['windows'])==2
    build_image(header,report,folder/'root.img')
    with (folder/'root.img').open('rb') as stream:
        for item in report['windows']:
            stream.seek(item['offset']);assert hashlib.sha256(stream.read(item['bytes'])).hexdigest()==item['sha256']
    bad=bytearray(header);struct.pack_into('<Q',bad,24+128+32,report['windows'][0]['offset']);bad[4064:]=hashlib.sha256(bad[:4064]).digest()
    try:decode_header(bad,64*MIB);raise AssertionError('overlap accepted')
    except ValueError:pass
    wrong=copy.deepcopy(spec);wrong['target']['partition']='userdata'
    try:prepare(wrong,folder);raise AssertionError('Android userdata accepted as root target')
    except ValueError:pass
    wrong=copy.deepcopy(spec);wrong['root_volume_uuid']='11111111-2222-4333-8444-555555555555'
    try:prepare(wrong,folder);raise AssertionError('container UUID accepted as root UUID')
    except ValueError:pass
    with os_file.open('ab') as stream:stream.write(bytes(512))
    try:build_image(header,report,folder/'changed.img');raise AssertionError('changed input published')
    except IOError:pass
    assert not (folder/'changed.img').exists()
report={'RAM_reservations_and_initrd_excluded':True,'EFI_map_parsed':True,'malformed_FDT_rejected':True,
        'root_header_and_local_image_readback':True,'overlapping_windows_rejected':True,
        'Android_target_rejected':True,'container_UUID_rejected':True,'changed_source_not_published':True,
        'fixtures':'synthetic APFS headers; no filesystem mountability claim','device_access':False}
(OUT/'plan-tests.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
