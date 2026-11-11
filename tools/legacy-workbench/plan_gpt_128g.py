"""Prepare the reviewed LUN0 GPT edit from fresh binary snapshots; no device I/O."""
import hashlib,json,struct,uuid,zlib,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'research'))
from gpt_offline_audit import parse,pair_check
OUT=ROOT/'edl_work/partition-128g-20261106'
def sha(b):return hashlib.sha256(b).hexdigest()
def main():
    before=[(OUT/'pre-main.bin').read_bytes(),(OUT/'pre-backup.bin').read_bytes()]
    parsed=[parse(x) for x in before];assert pair_check(*parsed)['complete_valid_pair']
    p=parsed[0];assert p['entry_count']==32 and p['entry_size']==128 and p['alternate_lba']==248838143
    old=next(x for x in p['partitions'] if x['name']=='userdata')
    assert (old['first_lba'],old['last_lba'])==(5972392,248838138)
    assert not any(x['name']=='ipados' for x in p['partitions'])
    entries=bytearray(before[0][8192:12288]);slot=next(i for i in range(32) if entries[i*128:(i+1)*128]==bytes(128))
    struct.pack_into('<Q',entries,(old['index']-1)*128+40,215283455)
    e=bytearray(128);e[:16]=uuid.UUID('7c3457ef-0000-11aa-aa11-00306543ecac').bytes_le
    unique=uuid.uuid4();e[16:32]=unique.bytes_le;struct.pack_into('<QQQ',e,32,215283456,248838138,0)
    name='ipados'.encode('utf-16-le');e[56:56+len(name)]=name;entries[slot*128:(slot+1)*128]=e
    after=[]
    for blob,info in zip(before,parsed):
        b=bytearray(blob);h=info['header_offset'];start=(info['entry_lba']-info['file_first_lba'])*4096
        b[start:start+4096]=entries;struct.pack_into('<I',b,h+88,zlib.crc32(entries));struct.pack_into('<I',b,h+16,0)
        struct.pack_into('<I',b,h+16,zlib.crc32(b[h:h+info['header_size']]));after.append(bytes(b))
    new=[parse(x) for x in after];assert pair_check(*new)['complete_valid_pair']
    parts={x['name']:x for x in new[0]['partitions']}
    for oldpart in p['partitions']:
        current=parts[oldpart['name']]
        if oldpart['name']=='userdata':
            assert current['first_lba']==oldpart['first_lba'] and current['unique_guid']==oldpart['unique_guid']
            assert current['attributes']==oldpart['attributes'] and current['last_lba']==215283455
        else:assert current==oldpart
    for name,data in zip(('post-main.bin','post-backup.bin'),after):
        path=OUT/name;assert not path.exists();path.write_bytes(data)
    report={'old_userdata':old,'new_userdata':parts['userdata'],'ipados':parts['ipados'],
            'pre_main_sha256':sha(before[0]),'pre_backup_sha256':sha(before[1]),
            'post_main_sha256':sha(after[0]),'post_backup_sha256':sha(after[1]),
            'unchanged_existing_entries':15,'disk_guid_unchanged':p['disk_guid']==new[0]['disk_guid'],
            'expected_shrunk_fs_blocks':209310803,'new_userdata_capacity_blocks':209311064,
            'write_order':['backup array','backup header','primary array','primary header'],'device_written':False}
    (OUT/'plan.json').write_text(json.dumps(report,indent=2)+'\n')
    header='\n'.join(f'#define {key.upper()} "{report[key]}"' for key in ('pre_main_sha256','pre_backup_sha256','post_main_sha256','post_backup_sha256'))+'\n'
    (OUT/'gpt_hashes.h').write_text(header)
    print(json.dumps({'new_userdata_bytes':parts['userdata']['bytes'],'ipados_bytes':parts['ipados']['bytes'],'ipados_guid':str(unique),'sha256':report['post_main_sha256']},indent=2))
if __name__=='__main__':main()
