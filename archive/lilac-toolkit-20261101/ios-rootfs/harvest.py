import struct, subprocess, glob
seen={}
for ko in glob.glob('/root/xperia/research/kos/*.ko'):
    out=subprocess.check_output(['aarch64-linux-gnu-readelf','-SW',ko]).decode()
    for line in out.splitlines():
        if '__versions' in line:
            p=line.split(); off=int(p[4],16); sz=int(p[5],16)
            data=open(ko,'rb').read()
            for i in range(sz//64):
                e=data[off+i*64:off+i*64+64]
                crc=struct.unpack('<Q',e[:8])[0]
                name=e[8:].split(b'\0')[0].decode()
                seen[name]=crc
with open('/root/k44/Module.symvers','w') as f:
    for name,crc in seen.items():
        f.write('0x%08x\t%s\tvmlinux\tEXPORT_SYMBOL\n'%(crc,name))
print('total',len(seen))
for s in ['module_layout','memremap','memunmap','misc_register','misc_deregister','kfree','kmalloc','copy_to_user','copy_from_user','default_llseek','memcpy']:
    print(s,hex(seen.get(s,-1)))
