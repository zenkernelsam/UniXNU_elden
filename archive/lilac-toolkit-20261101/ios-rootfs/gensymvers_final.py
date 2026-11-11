import struct, subprocess
data=open('/root/lilac-Image.raw','rb').read()
BASE=0xffffff8008080000
KS_S=0xffffff8009a52fa0; KG_S=0xffffff8009a6a900; KF=0xffffff8009a79fc0
KC_S=0xffffff8009a79fc0; KCG_S=0xffffff8009a85c70; KCG_E=0xffffff8009a8d7d0
# 1) ksymtab symbol -> index
nm=subprocess.check_output(['aarch64-linux-gnu-nm','/root/lilac-vmlinux.elf']).decode()
ent={}
for line in nm.splitlines():
    p=line.split()
    if len(p)!=3: continue
    a=int(p[0],16); n=p[2]
    if n.startswith('__ksymtab_'):
        name=n[10:]
        if KS_S<=a<KG_S: ent[name]=('std',(a-KS_S)//12)
        elif KG_S<=a<KF: ent[name]=('gpl',(a-KG_S)//12)
# 2) reloc table file range 0x1c27e40-0x247c048, 24B recs {ins,type,sym,val}
crc=[None]*20000; gpl=[None]*20000
cnt=0
for off in range(0x1c27e40,0x247c048-24,24):
    ins,typ,sym,val=struct.unpack('<QIIQ',data[off:off+24])
    if ins==0 or (ins>>32)!=0xffffff80: continue
    cnt+=1
    if KC_S<=ins<KCG_S: crc[(ins-KC_S)//4]=val
    elif KCG_S<=ins<KCG_E: gpl[(ins-KCG_S)//4]=val
print('relocs seen',cnt)
n=miss=0
with open('/root/k44/Module.symvers','w') as f:
    for name,(sec,i) in ent.items():
        v=(crc if sec=='std' else gpl)[i] if i<20000 else None
        if v is None or v==0: miss+=1; continue
        f.write('0x%08x\t%s\tvmlinux\tEXPORT_SYMBOL\n'%(v&0xffffffff,name)); n+=1
print('wrote',n,'missing',miss)
for s in ['module_layout','memremap','__ioremap','misc_register','kfree']:
    print(s, ent.get(s))
