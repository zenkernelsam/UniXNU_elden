"""Verify only the exact declared original camera IRQ/frame object replacement."""
from pathlib import Path
import hashlib,struct
from replay_xnu_objects import sections,global_symbols

ALLOWED={'NativePipeline.o','FrameOutput.o'}

def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def retained_camera_irq_input(path,current_command,report):
    path=Path(path)
    if report.get('actual_original_camera_IRQ_executor') is not True or path.name not in ALLOWED:
        raise ValueError('Camera IRQ replacement requires its exact source profile')
    row=report.get('camera_irq_direct_deltas',{}).get(path.name)
    if not isinstance(row,dict) or row.get('before_path')!=str(path):
        raise ValueError('Exact original camera IRQ object path not declared')
    replacement=Path(row.get('after_path',''))
    if replacement.name!=path.name or not replacement.is_file():
        raise ValueError('Exact camera IRQ replacement file absent')
    old=report.get('inherited_inputs',{}).get(str(path))
    if old is None or sha(path)!=old or row.get('before')!=old:
        raise ValueError('Original camera IRQ object fingerprint changed')
    new=report.get('inherited_inputs',{}).get(str(replacement))
    if new is None or sha(replacement)!=new or row.get('after')!=new:
        raise ValueError('Replacement camera IRQ object fingerprint changed')
    if current_command.count(str(path)) or current_command.count(str(replacement))!=1:
        raise ValueError('Original/replacement camera IRQ input duplicated or absent')
    matches=[p for p in current_command if Path(p).name==path.name]
    if matches!=[str(replacement)]:
        raise ValueError('Ambiguous camera IRQ direct object')
    if row.get('source_build_sha256')!=report.get('camera_irq_builds',{}).get(path.name):
        raise ValueError('Camera IRQ source-build receipt differs')
    return row

def loaded_relocations(path):
    """Raw reloc entries for loaded sections; debug-only offsets are excluded."""
    data=Path(path).read_bytes();at=32;rows={}
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        command,size=struct.unpack_from('<II',data,at)
        if command==0x19:
            for i in range(struct.unpack_from('<I',data,at+64)[0]):
                off=at+72+80*i;name=data[off:off+16].split(b'\0')[0].decode();seg=data[off+16:off+32].split(b'\0')[0].decode()
                if seg=='__DWARF':continue
                reloff,count=struct.unpack_from('<II',data,off+56)
                rows[seg+'/'+name]=hashlib.sha256(data[reloff:reloff+8*count]).hexdigest()
        at+=size
    return rows

def equivalent_service(old,new):
    old=Path(old);new=Path(new)
    if old.name!='Service.o' or new.name!='Service.o':raise ValueError('Only unchanged camera service comparison permitted')
    if sections(old)!=sections(new) or global_symbols(old)!=global_symbols(new) or loaded_symbols(old)!=loaded_symbols(new) or loaded_relocations(old)!=loaded_relocations(new):
        raise ValueError('Recompiled Service loadable code/data/symbol/relocations differ')
    return {'retained_path':str(old),'retained_sha256':sha(old),'recompiled_path':str(new),'recompiled_sha256':sha(new),
            'loaded_sections_equal':True,'global_symbols_equal':True,'loaded_relocations_equal':True}

def loaded_symbols(path):
    """Compare all local/global semantic bindings referenced by relocations."""
    data=Path(path).read_bytes();at=32;section_rows=[];symtab=None
    for _ in range(struct.unpack_from('<I',data,16)[0]):
        command,size=struct.unpack_from('<II',data,at)
        if command==0x19:
            for i in range(struct.unpack_from('<I',data,at+64)[0]):
                off=at+72+80*i;name=data[off:off+16].split(b'\0')[0].decode();seg=data[off+16:off+32].split(b'\0')[0].decode()
                section_rows.append((seg+'/'+name,struct.unpack_from('<Q',data,off+32)[0]))
        elif command==2:symtab=struct.unpack_from('<4I',data,at+8)
        at+=size
    if symtab is None:raise ValueError('Camera service object symbol table missing')
    offset,count,strings,_=symtab;rows={}
    for i in range(count):
        name,kind,section,desc,value=struct.unpack_from('<IBBHQ',data,offset+i*16)
        if kind&0xe0:continue
        spelling=data[strings+name:data.index(b'\0',strings+name)].decode();target='-'
        if kind&0x0e==0x0e:
            target,base=section_rows[section-1]
            if target.startswith('__DWARF/'):continue
            value-=base
        elif kind&0x0e==0x0a:value=data[strings+value:data.index(b'\0',strings+value)].decode()
        rows[spelling]=[kind,target,value,desc]
    return rows
