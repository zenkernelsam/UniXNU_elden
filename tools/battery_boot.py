"""Opt-in battery UI using existing, qualified Elden scalar units.

Only the primary entry changes. Numeric-one options use XNU's equivalent bare
form to fit the real 1024-byte buffer; no option is dropped to make room.
"""
import copy
import tomllib

BATTERY = {'elden-battery': '1', 'elden-battery-profile': '1', 'elden-battery-current': '-1'}
# Keep the two security-related options byte-for-byte, even though the parser
# also accepts their bare forms. This list is reviewed against the v91 menu.
COMPACT = frozenset('''elden-return-fastboot elden-disable-watchdogd elden-userfault-log
elden-gop-console native-ufs-root elden-usb-debug elden-usb-required
offline-mobile-graphics-load offline-legacy-register offline-legacy-gop
offline-legacy-pm offline-legacy-validate offline-legacy-transfer
offline-legacy-cpu-present sheng-vsync-fixed elden-frame-preview
elden-key-events elden-key-state elden-disable-unported-services elden-hid
elden-pmic elden-ucsi-query elden-pmic-notify elden-power-votes elden-charge-control
elden-touch-hid elden-ui-replay elden-touch-helper elden-touch-id-base
elden-ucsi-pdos elden-icon-cpu sheng-smp armcpu-self-ipi elden-ui'''.split())

def arguments(text):
    result = {}
    for token in text.split():
        key, sep, value = token.partition('=')
        if key in result:
            raise ValueError('Duplicate boot argument: ' + key)
        result[key] = value if sep else '1'
    return result

def verify(before, after):
    """Independent semantic check, separate from the serialization transform."""
    old, new = tomllib.loads(before), tomllib.loads(after)
    if old['entry'][0]['id'] != 'xnu' or new['entry'][0]['id'] != 'xnu':
        raise ValueError('Unexpected primary entry')
    old_args, new_args = old['entry'][0]['args'], new['entry'][0]['args']
    old_values = arguments(old_args)
    if set(BATTERY) & old_values.keys():
        raise ValueError('Battery flags already present in baseline')
    if arguments(new_args) != old_values | BATTERY:
        raise ValueError('Menu changes options beyond battery UI')
    comparable = copy.deepcopy(new)
    comparable['entry'][0]['args'] = old_args
    if comparable != old:
        raise ValueError('Menu fields or fallback entries changed')
    length = len(new_args.replace('@SESSION@', '0' * 16).encode())
    if length > 1023:
        raise ValueError('Battery menu exceeds boot-args buffer')
    return {'boot_args_bytes': length, 'battery_options': BATTERY,
            'existing_options_semantically_equal': True, 'hardware_tested': False}

def template(before):
    cfg = tomllib.loads(before)
    args = cfg['entry'][0]['args']
    values = arguments(args)
    if any(values.get(key) != '1' for key in COMPACT):
        raise ValueError('Reviewed numeric-one options differ')
    tokens = [t.partition('=')[0] if t.partition('=')[0] in COMPACT else t
              for t in args.split()]
    tokens += [f'{k}={v}' for k, v in BATTERY.items()]
    needle = 'args = "' + args + '"'
    if before.count(needle) != 1:
        raise ValueError('Ambiguous primary menu serialization')
    after = before.replace(needle, 'args = "' + ' '.join(tokens) + '"', 1)
    verify(before, after)
    return after
