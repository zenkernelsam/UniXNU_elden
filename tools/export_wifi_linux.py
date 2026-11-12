"""Export the pinned Wi-Fi Linux inputs and check every byte against Git blobs."""
import argparse,gzip,hashlib,io,json,subprocess,tarfile
from pathlib import Path
REV='458c6079fc1d41d564c37679c8ace02cd83ee817'
PATHS=['COPYING','include','drivers/net/wireless/ath/ath12k']
RUNTIME_PATHS=['COPYING','include','drivers/net/wireless/ath','drivers/bus/mhi','net/qrtr','net/mac80211','net/wireless','net/core/skbuff.c','drivers/soc/qcom/qmi_interface.c','drivers/soc/qcom/qmi_encdec.c','lib/rbtree.c','lib/crypto/arc4.c']
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repository',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--scope',choices=('peach','runtime'),default='peach');a=p.parse_args()
    if a.output.exists():raise ValueError('New output required')
    git=['git','-c','core.autocrlf=false','-c','core.eol=lf','-C',str(a.repository)]
    paths=PATHS if a.scope=='peach' else RUNTIME_PATHS
    raw=subprocess.check_output(git+['archive','--format=tar',REV,*paths])
    tree=subprocess.check_output(git+['ls-tree','-r','-z',REV,*paths]);expected={}
    for row in tree.split(b'\0'):
        if not row:continue
        identity,path=row.split(b'\t',1);mode,kind,oid=identity.decode().split()
        if kind!='blob' or mode not in ('100644','100755','120000'):raise ValueError('Unexpected source entry')
        expected[path.decode()]=(mode,oid)
    seen=set()
    with tarfile.open(fileobj=io.BytesIO(raw)) as t:
        for member in t.getmembers():
            if member.isdir():continue
            mode,oid=expected[member.name]
            if member.issym() and mode=='120000':data=member.linkname.encode()
            elif member.isfile() and mode!='120000':data=t.extractfile(member).read()
            else:raise ValueError('Archive changed a source entry type')
            actual=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
            if actual!=oid:raise ValueError('Archive transformed Git blob: '+member.name)
            seen.add(member.name)
    if seen!=set(expected):raise ValueError('Archive omitted source entries')
    data=gzip.compress(raw,mtime=0);a.output.mkdir(parents=True);(a.output/'linux.tar.gz').write_bytes(data)
    report={'commit':REV,'paths':paths,'tar_sha256':hashlib.sha256(raw).hexdigest(),'gzip_sha256':hashlib.sha256(data).hexdigest(),
            'tar_bytes':len(raw),'git_blob_entries_verified':len(seen),'archive_eol':'raw Git blobs; autocrlf disabled explicitly'}
    (a.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',newline='\n');print(json.dumps(report,indent=2))
if __name__=='__main__':main()
