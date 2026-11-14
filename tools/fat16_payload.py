"""Write deterministic payload FAT16 with LFNs using the existing source writer."""
from pathlib import Path
import struct,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
from make_payload import Fat16Builder

class PayloadFat(Fat16Builder):
    def __init__(self):
        super().__init__(114688)
        self.aliases={0:set()}
        self.append_rows(0,self.entry(b'ELDENRAM   ',8,0,0))
    def _choose_geometry(self):return 4,112
    def append_rows(self,parent,rows):
        directory=self._read_cluster(parent);free=self._first_free_entry(directory)
        if free+len(rows)+32>len(directory):raise ValueError('Payload directory full')
        directory[free:free+len(rows)]=rows;self._write_cluster(parent,directory)
    def entry(self,alias,attr,cluster,size):
        r=bytearray(32);r[:11]=alias;r[11]=attr;date=((2024-1980)<<9)|(11<<5)|25
        for off in (16,18,24):struct.pack_into('<H',r,off,date)
        struct.pack_into('<HI',r,26,cluster,size);return bytes(r)
    def named(self,parent,name,attr,cluster,size):
        if not name or '/' in name or '\\' in name or '\0' in name:raise ValueError('Invalid FAT name')
        stem,sep,ext=name.rpartition('.')
        if not sep:stem,ext=name,''
        safe=lambda s:''.join(c for c in s.upper() if c.isascii() and c.isalnum())
        stem,ext=safe(stem),safe(ext)[:3]
        for serial in range(1,10):
            alias=(stem[:6]+'~'+str(serial)).ljust(8).encode()+ext.ljust(3).encode()
            if alias not in self.aliases[parent]:break
        else:raise ValueError('Too many aliases')
        self.aliases[parent].add(alias);checksum=0
        for value in alias:checksum=(((checksum&1)<<7)+(checksum>>1)+value)&255
        chars=name.encode('utf-16-le')+b'\0\0';chars+=b'\xff\xff'*((-len(chars)//2)%13)
        count=len(chars)//26;rows=bytearray()
        if count>20:raise ValueError('FAT name too long')
        for i in range(count,0,-1):
            data=chars[(i-1)*26:i*26];r=bytearray(32);r[0]=i|(0x40 if i==count else 0);r[11]=15;r[13]=checksum;r[1:11]=data[:10];r[14:26]=data[10:22];r[28:32]=data[22:26];rows+=r
        self.append_rows(parent,rows+self.entry(alias,attr,cluster,size))
    def directory(self,parent,name):
        cluster=self._allocate(b'');self.aliases[cluster]=set()
        self.append_rows(cluster,self.entry(b'.          ',16,cluster,0)+self.entry(b'..         ',16,parent,0));self.named(parent,name,16,cluster,0);return cluster
    def file(self,parent,name,data):self.named(parent,name,32,self._allocate(data),len(data))

def build(files):
    volume=PayloadFat();dirs={'':0}
    for name,data in sorted(files.items()):
        parent='';parts=name.split('/')
        for part in parts[:-1]:
            key=(parent+'/' if parent else '')+part
            if key not in dirs:dirs[key]=volume.directory(dirs[parent],part)
            parent=key
        volume.file(dirs[parent],parts[-1],data)
    if volume.next_cluster>len(volume.fat):raise ValueError('FAT entries exhausted')
    return volume.finish()
