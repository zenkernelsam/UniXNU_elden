"""Execute exact 21H461 routing instructions at two slides; no tablet access.

External allocation/product/logging and the heavy model constructor are modeled.
This tests routing/ABI/write boundaries, not native iOS services or HCI success.
"""
from pathlib import Path
import argparse, importlib.util, json, struct, sys
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_PROT_ALL
from unicorn.arm64_const import *

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'drivers/EldenBluetooth/userland'))
import Native17 as native

def main():
    if sys.flags.optimize:
        raise ValueError('Assertions required')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Fresh output required')
    original = native.Image(args.image)
    patched = native.Image(args.image).routing()
    reports = []
    for slide in (0, 0x28000000):
        for version, image in (('original', original), ('patched', patched)):
            def machine():
                u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
                u.mem_map(native.BASE + slide, 0xa00000, UC_PROT_ALL)
                for name, va, vs, off, size, prot in image.segments:
                    if name != '__PAGEZERO':
                        u.mem_write(va + slide, bytes(image.data[off:off + size]))
                u.mem_map(0x300000000, 0x20000, UC_PROT_ALL)
                u.reg_write(UC_ARM64_REG_SP, 0x30001e000)
                u.reg_write(UC_ARM64_REG_LR, 0x30001f000)
                u.reg_write(UC_ARM64_REG_CPACR_EL1, 3 << 20)
                return u
            def run(u, begin, ends, callbacks=None):
                endset = {x + slide for x in ends}
                outcome = []
                def hook(emu, pc, size, context):
                    if pc == 0x30001f000 or pc in endset:
                        outcome.append(pc - slide if pc != 0x30001f000 else pc)
                        emu.emu_stop(); return
                    callback = (callbacks or {}).get(pc - slide)
                    if callback:
                        callback(emu)
                token = u.hook_add(UC_HOOK_CODE, hook)
                u.emu_start(begin + slide, 0, count=25000)
                u.hook_del(token)
                assert len(outcome) == 1, ('did not reach expected exit', hex(u.reg_read(UC_ARM64_REG_PC)))
                return outcome[0]
            def ret(u, value=None):
                if value is not None:
                    u.reg_write(UC_ARM64_REG_X0, value)
                u.reg_write(UC_ARM64_REG_PC, u.reg_read(UC_ARM64_REG_LR))

            # Existing registration range1..3999 must be preserved exactly.
            for chip in (0, 1, 23, 1999, 2000, 3999, 4000, 6000, 0xffffffff):
                u = machine(); u.reg_write(UC_ARM64_REG_W0, chip); u.reg_write(UC_ARM64_REG_X1, 0xface012345)
                u.mem_write(native.BASE + 0x927754 + slide, b'\xcc' * 4)
                u.mem_write(native.BASE + 0x9321c0 + slide, b'\xcc' * 8)
                run(u, native.REGISTER, [])
                accepted = 1 <= chip <= 3999 or (version == 'patched' and chip == 0)
                assert u.reg_read(UC_ARM64_REG_W0) == (0 if accepted else 101)
                saved = struct.unpack('<I', u.mem_read(native.BASE + 0x927754 + slide, 4))[0]
                context = struct.unpack('<Q', u.mem_read(native.BASE + 0x9321c0 + slide, 8))[0]
                assert saved == (chip if accepted else 0xcccccccc)
                assert context == (0xface012345 if accepted else 0xcccccccccccccccc)
                reports.append({'case': 'registration', 'slide': hex(slide), 'version': version, 'chip': chip, 'accepted': accepted})

            # Both callback paths keep the common callback store, but type0 does
            # not register any Apple vendor-specific callbacks.
            for idx, (site, epilogue) in enumerate(native.CALLBACKS):
                entry = native.BASE + (0x1e1f9c if idx == 0 else 0x1e205c)
                common = native.BASE + (0x9321e8 if idx == 0 else 0x932358)
                for chip in (0, 1, 1999, 2000, 3999, 4000, 0xffffffff):
                    u = machine(); u.reg_write(UC_ARM64_REG_X0, 0xabc87654321)
                    u.mem_write(native.BASE + 0x931830 + slide, struct.pack('<I', chip))
                    stops = [epilogue, native.BASE + 0x1a5ebc]
                    if idx == 0: stops.append(native.BASE + 0x113d00)
                    result = run(u, entry, stops, {native.BASE + 0x577f4: lambda u: ret(u, 0)})
                    assert struct.unpack('<Q', u.mem_read(common + slide, 8))[0] == 0xabc87654321
                    expected = (native.BASE + 0x1a5ebc if 1 <= chip <= 1999 else
                                native.BASE + 0x113d00 if idx == 0 and 2000 <= chip <= 3999 else epilogue)
                    assert result == expected
                    reports.append({'case': 'callback', 'slide': hex(slide), 'version': version, 'index': idx, 'chip': chip})

            # Reopen after another vendor must clear tables, hooks and opcodes;
            # all nonzero types continue through their original selection.
            for chip in (0, 1, 1999, 2000, 3999, 4000, 0xffffffff):
                u = machine(); u.reg_write(UC_ARM64_REG_W0, chip); u.reg_write(UC_ARM64_REG_X1, 0xabcdef)
                for address, size in [(native.VENDOR_OPS - 8, native.VENDOR_OPS_BYTES + 16)] + [(a - 8, 24) for a in native.VENDOR_HOOKS] + [(native.VENDOR_OPCODES - 4, 12)]:
                    u.mem_write(address + slide, b'\xa5' * size)
                clears = []
                def zero(emu):
                    address = emu.reg_read(UC_ARM64_REG_X0); size = emu.reg_read(UC_ARM64_REG_W1)
                    assert (address, size) == (native.VENDOR_OPS + slide, native.VENDOR_OPS_BYTES)
                    clears.append((address, size)); emu.mem_write(address, b'\0' * size); ret(emu)
                def log(emu): ret(emu, 0)
                stops = [native.BASE + 0x1e1d04, native.BASE + 0x1e1d3c, native.BASE + 0x1e1da0]
                result = run(u, native.VENDOR_INIT, stops, {native.BASE + 0x708bcc: zero, native.BASE + 0x577f4: log})
                expected = native.BASE + (0x1e1d04 if 1 <= chip <= 1999 else 0x1e1d3c if 2000 <= chip <= 3999 else 0x1e1da0)
                assert result == expected
                assert struct.unpack('<I', u.mem_read(native.BASE + 0x931830 + slide, 4))[0] == chip
                cleared = version == 'patched' and chip == 0
                assert len(clears) == int(cleared)
                assert bytes(u.mem_read(native.VENDOR_OPS + slide, native.VENDOR_OPS_BYTES)) == (b'\0' if cleared else b'\xa5') * native.VENDOR_OPS_BYTES
                # opcode occupies the8 bytes immediately preceding the table.
                assert native.VENDOR_OPS + native.VENDOR_OPS_BYTES in native.VENDOR_HOOKS
                assert bytes(u.mem_read(native.VENDOR_OPS + native.VENDOR_OPS_BYTES + 8 + slide, 8)) == b'\xa5' * 8
                for address in native.VENDOR_HOOKS:
                    assert bytes(u.mem_read(address + slide, 8)) == (b'\0' if cleared else b'\xa5') * 8
                assert bytes(u.mem_read(native.VENDOR_OPCODES + slide, 4)) == (b'\0' if cleared else b'\xa5') * 4
                assert bytes(u.mem_read(native.VENDOR_OPCODES + 4 + slide, 4)) == b'\xa5' * 4
                reports.append({'case': 'vendor-init', 'slide': hex(slide), 'version': version, 'chip': chip, 'cleared': cleared})

            # Run the complete product factory using failed product queries and
            # no environment override. Model only external allocation/logging and
            # constructor body; verify every original branch into its real thunk.
            u = machine(); canary = 0x300000100; object_address = 0x300001000; allocated = 0x300002000
            u.mem_write(native.BASE + 0x8cd2d8 + slide, struct.pack('<Q', canary))
            u.mem_write(canary, struct.pack('<Q', 0x9988776655443322))
            u.mem_write(native.BASE + 0x93e708 + slide, struct.pack('<Q', object_address))
            u.mem_write(native.BASE + 0x93e710 + slide, struct.pack('<Q', 0xffffffffffffffff))
            u.mem_write(object_address, struct.pack('<Q', object_address + 0x100))
            u.mem_write(object_address + 0x208, struct.pack('<Q', native.VM_CHECK + slide))
            # Original VM cache is false, matching observed factory-null failure.
            u.mem_write(native.BASE + 0x93eb90 + slide, struct.pack('<Q', 0xffffffffffffffff))
            constructors = []; queries = []
            def alloc(emu):
                assert emu.reg_read(UC_ARM64_REG_W0) == 0x588
                ret(emu, allocated)
            def construct(emu):
                constructors.append(emu.reg_read(UC_ARM64_REG_W1))
                assert emu.reg_read(UC_ARM64_REG_X0) == allocated
                emu.mem_write(allocated + 0x2a8, struct.pack('<I', constructors[-1])); ret(emu, allocated)
            def product(emu): queries.append(True); ret(emu, 0)
            callbacks = {native.BASE + 0x709058: lambda u: ret(u, 0), native.BASE + 0x708038: product,
                         native.BASE + 0x708938: alloc, native.CONSTRUCTOR: construct,
                         native.BASE + 0x709778: lambda u: ret(u, 0), native.BASE + 0x7094e4: lambda u: ret(u, 0),
                         native.BASE + 0x7093e8: lambda u: ret(u)}
            run(u, native.FACTORY, [], callbacks)
            model = struct.unpack('<Q', u.mem_read(native.MODEL + slide, 8))[0]
            assert len(queries) == 73, len(queries)
            assert model == (allocated if version == 'patched' else 0)
            assert constructors == ([0] if version == 'patched' else [])
            reports.append({'case': 'unknown-product-full-factory', 'version': version, 'slide': hex(slide), 'queries': len(queries), 'model_created': bool(model), 'constructor_types': constructors})
            if version == 'patched':
                for chip in (1, 10, 2000, 0xffffffff):
                    u = machine(); u.reg_write(UC_ARM64_REG_W1, chip);u.reg_write(UC_ARM64_REG_X0, allocated)
                    run(u, native.CONSTRUCTOR_THUNK, [native.CONSTRUCTOR])
                    assert u.reg_read(UC_ARM64_REG_W1) == 0 and u.reg_read(UC_ARM64_REG_X0) == allocated
                u = machine(); run(u, native.VM_CHECK, []); assert u.reg_read(UC_ARM64_REG_W0) == 0
    # This complete byte-diff audit also verifies original F2/F3 unchanged.
    report = patched.report()
    assert patched.data[0x2ef860:0x2efbe0] == original.data[0x2ef860:0x2efbe0]
    assert patched.data[0xddd20:0xddd24] == original.data[0xddd20:0xddd24]
    args.output.mkdir(parents=True)
    (args.output / 'result.json').write_text(json.dumps({'cases': reports, 'patch': report, 'hardware_verified': False,
        'limits': 'Unicorn instruction execution with modeled allocation, product queries, logging and constructor body; no iOS services, capabilities or HCI validation.'}, indent=2) + '\n')
    print('PASS', len(reports), 'original/patched instruction cases at two ASLR slides')

if __name__ == '__main__':
    main()
