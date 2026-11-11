"""Independent FAT/Mach-O/AVB and ARM64 entry-stub audit, with no device I/O."""
import argparse
import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import struct

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM
from unicorn import arm64_const as regs
from avb_guard import check_image

ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT.parents[1] / 'UniXNU_elden'


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class Fat:
    def __init__(self, data):
        self.data = data
        self.bps = struct.unpack_from('<H', data, 11)[0]
        self.spc = data[13]
        self.reserved = struct.unpack_from('<H', data, 14)[0]
        self.fats = data[16]
        self.root_entries = struct.unpack_from('<H', data, 17)[0]
        smallfat = struct.unpack_from('<H', data, 22)[0]
        self.bits = 16 if smallfat else 32
        self.fat_sectors = smallfat or struct.unpack_from('<I', data, 36)[0]
        self.root_sectors = (self.root_entries*32+self.bps-1)//self.bps
        self.root_start = (self.reserved+self.fats*self.fat_sectors)*self.bps
        self.first_data = self.root_start+self.root_sectors*self.bps
        self.cluster_bytes = self.bps*self.spc
        self.root_cluster = 0 if self.bits == 16 else struct.unpack_from('<I', data, 44)[0]

    def chain(self, cluster):
        chunks, seen = [], set()
        eof = 0xfff8 if self.bits == 16 else 0x0ffffff8
        while cluster < eof:
            assert cluster >= 2 and cluster not in seen
            seen.add(cluster)
            start = self.first_data+(cluster-2)*self.cluster_bytes
            assert start+self.cluster_bytes <= len(self.data)
            chunks.append(self.data[start:start+self.cluster_bytes])
            pos = self.reserved*self.bps+cluster*(self.bits//8)
            cluster = int.from_bytes(self.data[pos:pos+self.bits//8], 'little')
            if self.bits == 32:
                cluster &= 0x0fffffff
        return b''.join(chunks)

    def directory(self, cluster):
        raw = self.chain(cluster) if cluster else self.data[self.root_start:self.first_data]
        names, result = {}, {}
        for offset in range(0, len(raw), 32):
            entry = raw[offset:offset+32]
            if entry[0] == 0:
                break
            if entry[0] == 0xe5:
                names = {}; continue
            if entry[11] == 0x0f:
                if entry[0] & 0x40:
                    names = {}
                names[entry[0]&0x1f] = entry[1:11]+entry[14:26]+entry[28:32]
                continue
            short = entry[:8].decode('ascii').rstrip()+('.'+entry[8:11].decode('ascii').rstrip() if entry[8:11].strip() else '')
            name = b''.join(names[i] for i in sorted(names)).decode('utf-16-le').split('\0')[0].rstrip('\uffff') if names else short
            names = {}
            if entry[11] & 8:
                continue
            first = struct.unpack_from('<H', entry, 26)[0]
            if self.bits == 32:
                first |= struct.unpack_from('<H', entry, 20)[0] << 16
            result[name.casefold()] = (first, struct.unpack_from('<I', entry, 28)[0], entry[11])
        return result

    def read(self, path):
        cluster = self.root_cluster
        parts = path.strip('/').split('/')
        for index, name in enumerate(parts):
            cluster, size, flags = self.directory(cluster)[name.casefold()]
            if index != len(parts)-1:
                assert flags & 0x10
        assert not flags & 0x10
        result = self.chain(cluster)[:size]
        assert len(result) == size
        return result


def emulate_entry(kernel, preflight, maker):
    parsed = preflight.parse_kernel(kernel)
    sections = maker.segments(kernel)
    stub = next(s for s in sections if s['name'] == '__ELDEN_PROBE')
    physical = 0xb927c000
    entry = physical+parsed['entry']-parsed['base']
    stub_pa = physical+stub['address']-parsed['base']
    entry_word = struct.unpack_from('<I', kernel, parsed['entry_file_offset'])[0]
    assert maker.branch_target(entry_word, parsed['entry']) == stub['address']
    code = kernel[stub['file_offset']:stub['file_offset']+stub['file_size']]
    target_va = maker.branch_target(struct.unpack_from('<I', code, len(code)-4)[0], stub['address']+len(code)-4)
    end = physical+target_va-parsed['base']
    args, framebuffer = 0xd1000000, 0xfc800000
    width, height, stride = 1904, 3040, 7616
    emu = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    emu.mem_map(entry & ~4095, 4096)
    emu.mem_write(entry, struct.pack('<I', entry_word))
    emu.mem_map(stub_pa & ~4095, (stub['file_size']+4095)&~4095)
    emu.mem_write(stub_pa, code)
    emu.mem_map(args, 4096)
    emu.mem_write(args+0x28, struct.pack('<6Q', framebuffer, 1, stride, width, height, 32))
    emu.mem_map(framebuffer, (stride*height+4095)&~4095)
    saved = [args, entry, physical, 0x10000000, 0x30d019d0, 0, 64, 63]
    for i, value in enumerate(saved):
        emu.reg_write(getattr(regs, 'UC_ARM64_REG_X'+str(i)), value)
    emu.emu_start(entry, end, count=1500000)
    assert emu.reg_read(regs.UC_ARM64_REG_PC) == end
    assert all(emu.reg_read(getattr(regs,'UC_ARM64_REG_X'+str(i))) == value for i,value in enumerate(saved))
    top = height//2-32
    band = bytes(emu.mem_read(framebuffer+top*stride,64*stride))
    assert band == struct.pack('<I',0xff0000ff)*(64*width)
    assert bytes(emu.mem_read(framebuffer+(top-1)*stride,stride)) == bytes(stride)
    assert bytes(emu.mem_read(framebuffer+(top+64)*stride,stride)) == bytes(stride)
    return {'model':'entry branch + real stub under supplied physical memory/Video model; not real EBS',
            'entry_pa':hex(entry),'stub_pa':hex(stub_pa),'return_pa':hex(end),
            'blue_rows':64,'blue_pixels':64*width,'x0_to_x7_preserved':True,'branch_back':True}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('image',type=Path)
    p.add_argument('--output',type=Path,required=True)
    a = p.parse_args()
    admitted = check_image(a.image,'boot_a')
    data = a.image.read_bytes()
    blob = gzip.decompress(data[4096:4096+struct.unpack_from('<I',data,8)[0]])
    old_fd = (ROOT/'edl_work/offline-20261105/old-fd.bin').read_bytes()
    assert blob[128:0x400080] == old_fd
    fat = Fat(blob[0x400080:])
    kernel = fat.read('/EFI/UniXNU/Payloads/kernel.macho')
    adt = fat.read('/EFI/UniXNU/Payloads/kernel.macho.adt')
    config = fat.read('/EFI/UniXNU/unixnu.toml').decode('utf-8')
    assert 'elden-gop-console=1' in config
    preflight = module('boot_preflight',PORT/'scripts/darwinboot_preflight.py')
    maker = module('probe_maker',PORT/'scripts/make_entry_probe_kernel.py')
    parsed = preflight.parse_kernel(kernel)
    tree = preflight.adt_walk(adt)
    assert tree['window'] == 0x10000000
    symbols = maker.symbols(kernel)
    assert '_PE_elden_enable_gop_kprintf' in symbols
    seg, off = maker.locate(kernel,0xfffffff004aaecfc)
    assert struct.unpack_from('<I',kernel,off)[0] == 0x17ffffd4
    report = {'image_sha256':admitted.sha256,'index':admitted.index,'location':admitted.location,
              'fd_unchanged':True,'kernel_bytes':len(kernel),'kernel_sha256':hashlib.sha256(kernel).hexdigest(),
              'adt_sha256':hashlib.sha256(adt).hexdigest(),'segments':parsed['segments'],
              'source_console_symbol':hex(symbols['_PE_elden_enable_gop_kprintf']),
              'entry_simulation':emulate_entry(kernel,preflight,maker),'actual_config':config,'device_tested':False}
    a.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:report[k] for k in ('image_sha256','index','kernel_sha256','entry_simulation','device_tested')},indent=2))


if __name__ == '__main__':
    main()
