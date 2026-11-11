"""Read raw ciphertext for an already reviewed, quiescent F2FS allocation map.

Device source is hard-coded userdata/O_RDONLY in the companion. This creates
a packed backup, not a flashable image. Independent second read verifies it.
"""
from pathlib import Path
import hashlib,json,struct,subprocess,shutil,time,re

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'edl_work/prepartition-128g-20261106'
ADB=Path('D:/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe')
PREFIX=[str(ADB),'-s','HA2A1PT2']
def shell(command):
    p=subprocess.run(PREFIX+['exec-out','sh','-c',command],capture_output=True,timeout=30)
    if p.returncode:raise RuntimeError('ADB command failed')
    return p.stdout

def main():
    data=json.loads((OUT/'userdata-extents.json').read_text())
    ranges=data['allocated_extents'];total=data['allocated_bytes']
    assert data['logical_bytes']==994778099712 and total==sum(x['bytes'] for x in ranges)
    mounts=shell('cat /proc/mounts').decode()
    assert not any(line.startswith('/dev/block/dm-7 ') for line in mounts.splitlines()),'userdata must remain unmounted'
    assert shell('getprop ro.twrp.version').strip(),'TWRP required'
    assert shutil.disk_usage(OUT).free>total+2*(1<<30),'insufficient backup space'
    binary=struct.pack('<8s4Q',b'ELDEXT1\0',1,data['logical_bytes'],total,len(ranges))
    for entry in ranges:binary+=struct.pack('<QQ',entry['offset'],entry['bytes'])
    plan=OUT/'userdata-extents.bin';plan.write_bytes(binary)
    subprocess.run(PREFIX+['push',str(plan),'/tmp/elden-extents.bin'],check=True,capture_output=True)
    assert shell('sha256sum /tmp/elden-extents.bin').decode().split()[0]==hashlib.sha256(binary).hexdigest()
    partial=OUT/'userdata-allocated-cipher.bin.partial';final=OUT/'userdata-allocated-cipher.bin'
    assert not final.exists() and not partial.exists(),'refuse to overwrite a backup'
    command='/tmp/elden-extent-reader /dev/block/by-name/userdata /tmp/elden-extents.bin 2>/tmp/elden-extent-reader.log'
    p=subprocess.Popen(PREFIX+['exec-out','sh','-c',command],stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    h=hashlib.sha256();eh=hashlib.sha256();entries=[];index=0;left=ranges[0]['bytes'];count=0;last=time.monotonic()
    with partial.open('xb') as f:
        while True:
            chunk=p.stdout.read(1048576)
            if not chunk:break
            if count+len(chunk)>total:raise RuntimeError('unexpected excess bytes; backup remains partial')
            f.write(chunk);h.update(chunk);at=0
            while at<len(chunk):
                n=min(left,len(chunk)-at);eh.update(chunk[at:at+n]);at+=n;left-=n
                if left==0:
                    entries.append(dict(ranges[index],packed_offset=count+at-ranges[index]['bytes'],sha256=eh.hexdigest()))
                    index+=1;eh=hashlib.sha256()
                    left=ranges[index]['bytes'] if index<len(ranges) else 0
            count+=len(chunk)
            if time.monotonic()-last>=10:
                print(f'Backup {count/2**30:.2f}/{total/2**30:.2f} GiB',flush=True);last=time.monotonic()
        f.flush()
        import os;os.fsync(f.fileno())
    rc=p.wait();errors=p.stderr.read();(OUT/'cipher-read-adb-stderr.txt').write_bytes(errors)
    if rc or count!=total or index!=len(ranges):raise RuntimeError('incomplete backup; do not resize')
    print('Backup read complete; independently rereading the selected raw blocks for SHA256.',flush=True)
    command='/tmp/elden-extent-reader /dev/block/by-name/userdata /tmp/elden-extents.bin --hash 2>/tmp/elden-extent-verify.log'
    q=subprocess.run(PREFIX+['exec-out','sh','-c',command],capture_output=True)
    result=q.stdout.decode().strip();match=re.fullmatch(r'OK bytes=(\d+) sha256=([0-9a-f]{64})',result)
    assert match and int(match[1])==total and match[2]==h.hexdigest(),'independent raw read differs; do not resize'
    partial.rename(final)
    report={'format':'elden-packed-cipher-extents-v1','source_device':'HA2A1PT2','source_partition':'userdata',
            'original_logical_bytes':data['logical_bytes'],'packed_bytes':total,'sha256':h.hexdigest(),
            'domain_sha256':data['domain_sha256'],'plan_sha256':hashlib.sha256(binary).hexdigest(),
            'independent_device_read_match':True,'source_unmounted':True,'raw_ciphertext':True,
            'plaintext_files_exported_by_this_backup':False,'extents':entries,
            'restore_contract':'Restore original GPT bounds, matching metadata and these raw extents at their original userdata-relative byte offsets. Packed file is NOT directly flashable.'}
    (OUT/'userdata-cipher-backup.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:v for k,v in report.items() if k!='extents'},indent=2),flush=True)

if __name__=='__main__':main()
