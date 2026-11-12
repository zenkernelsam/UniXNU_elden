"""Bind TB322FC USB/WLS report semantics to the exact stock ADSP image.

Offline only. The original firmware and disassembly remain private inputs.
Excerpts are written to the explicit evidence directory, never research/.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

FIRMWARE_SHA = '9edad6f458caa609a189b916694eecec99bb0c01fa8d2bd02c630552a7dbba75'
ASSEMBLY_SHA = '16bf1c895df9b72af95c6afba6c15bd71fa0bb9f18200575e9d7b6fd14f82221'

def digest(data):
    return hashlib.sha256(data).hexdigest()

def audit(firmware, assembly, output):
    binary, listing = firmware.read_bytes(), assembly.read_bytes()
    if digest(binary) != FIRMWARE_SHA or digest(listing) != ASSEMBLY_SHA:
        raise ValueError('Inputs differ from the reviewed stock firmware/disassembly')
    phoff = struct.unpack_from('<I', binary, 28)[0]
    size, count = struct.unpack_from('<HH', binary, 42)
    segments = [struct.unpack_from('<8I', binary, phoff + i * size) for i in range(count)]
    def read(va, length):
        matches = [s for s in segments if s[0] == 1 and s[2] <= va and va + length <= s[2] + s[4]]
        if len(matches) != 1: raise ValueError('Ambiguous file-backed VA')
        s = matches[0]
        at = s[1] + va - s[2]
        return binary[at:at + length]
    rows = {int(m[1], 16): line for line in listing.decode().splitlines()
            if (m := re.match(r'([0-9a-f]{8}):', line))}
    checks = {
        'platform3': (0x6004db28, 'cmp.eq(r2,#0x3)'),
        'usb_get_dispatch': (0x600bac70, 'call 0x601ea670'),
        'wls_get_dispatch': (0x600bae80, 'call 0x601ea930'),
        'usb_callback': (0x6004dc24, 'memw(r2+#0x0) = ##0x600684c0'),
        'wls_callback': (0x6004dc34, 'memw(r2+#0x0) = ##0x60068850'),
        'usb_property_stride': (0x6006882c, 'addasl(r2,r3,#0x2)'),
        'usb_property_base': (0x60068830, 'memw(r2+#0x724)'),
        'wls_property_stride': (0x60068bb8, 'addasl(r2,r3,#0x2)'),
        'wls_property_base': (0x60068bbc, 'memw(r2+#0x758)'),
        'second_input_index': (0x60066b28, 'cmp.eq(r2,#0x1)'),
        'first_input_online_test': (0x60066b3c, 'cmp.eq(r2,#0x0)'),
        'skip_second_usb_only_when_first_online': (0x60066b44, 'if (p0) jump:nt 0x60066ed4'),
        'second_falls_through_usb_update': (0x60066b48, 'jump 0x60066b4c'),
        'usb_online_updated_from_selected_report': (0x60066bd4, 'memw(r3+#0x724) = r4'),
        'second_report_updates_wls_online': (0x60066f8c, 'memw(r3+#0x758) = r4'),
        'wls_lower_result_saved': (0x601ea988, 'memh(r30+#-0xa) = r0'),
        'wls_error_test': (0x601ea990, 'cmp.eq(r2,#0x0)'),
        'wls_wrapper_returns_zero_after_log': (0x601eaa54, 'memh(r2+#0x0) = #0x0'),
        'usb_wrapper_preserves_result': (0x601ea6d0, 'memh(r30+#-0x2) = r2'),
    }
    for label, (va, expected) in checks.items():
        if expected not in rows.get(va, ''): raise ValueError('Changed instruction: ' + label)
    ranges = {
        'platform-selector': (0x6004db1c, 0x6004db38),
        'callback-registration': (0x6004dc18, 0x6004dc38),
        'usb-get-dispatch': (0x600bac44, 0x600bac78),
        'wls-get-dispatch': (0x600bae54, 0x600bae88),
        'usb-get-wrapper': (0x601ea670, 0x601ea6e0),
        'wls-get-wrapper': (0x601ea930, 0x601eaa64),
        'usb-property-load': (0x60068820, 0x60068850),
        'wls-property-load': (0x60068bac, 0x60068bdc),
        'report-priority': (0x60066af0, 0x60066b4c),
        'usb-online-report': (0x60066b94, 0x60066bd8),
        'second-online-report': (0x60066f48, 0x60066f90),
    }
    if output.exists(): raise ValueError('Evidence output must be new')
    output.mkdir(parents=True)
    excerpts = {}
    for name, (low, high) in ranges.items():
        text = '\n'.join(rows[va] for va in range(low, high, 4)) + '\n'
        (output / (name + '.asm')).write_text(text, encoding='utf-8', newline='\n')
        excerpts[name] = {'va': hex(low), 'end_exclusive': hex(high),
                          'firmware_bytes_sha256': digest(read(low, high-low)),
                          'listing_sha256': digest(text.encode())}
    result = {
        'firmware_sha256': FIRMWARE_SHA, 'disassembly_sha256': ASSEMBLY_SHA,
        'checks': {key: True for key in checks}, 'ranges': excerpts,
        'property_fields': {
            'USB_GET_0x32': {name: {'property': n, 'context_offset': hex(0x724 + 4*n)}
                           for n, name in ((0,'online'),(1,'voltage_now'),(3,'current_now'),(4,'current_max'),(5,'input_current_limit'),(6,'type'),(7,'adapter'),(11,'real_type'))},
            'WLS_GET_0x34': {name: {'property': n, 'context_offset': hex(0x758 + 4*n)}
                           for n, name in ((0,'online'),(1,'voltage_now'),(3,'current_now'),(8,'input_current_limit'),(10,'temperature'))}},
        'decision': 'USB-online is a priority-selected logical report, not exclusively the long-edge connector. Port1 also updates USB fields when port0 is offline. Do not broaden charge-policy admission merely by OR-ing WLS-online as a guessed short-edge fix.',
        'limits': ['Static report handling does not prove FET selection or reverse-current protection.',
                   'WLS wrapper masks lower getter errors after logging; successful envelope alone is insufficient for physical input proof.',
                   'Online fields may additionally be forced off by firmware states8/9; no state meaning is assumed here.',
                   'Exact post-disconnect reports were not captured in the historical meter trial.'],
        'driver_changed': False, 'device_operations': False,
    }
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({'checks': len(checks), 'result': str(output/'result.json'), 'driver_changed': False}))

if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--firmware', type=Path, required=True)
    p.add_argument('--assembly', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    audit(a.firmware, a.assembly, a.output)
