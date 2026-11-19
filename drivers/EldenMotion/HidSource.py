"""Extract only hash-bound same-OS17 accel/gyro descriptors; no board transport."""
from pathlib import Path
import hashlib,json
HERE=Path(__file__).resolve().parent
def descriptors():
    s=json.loads((HERE/'hid-source.json').read_text());row=next(r for r in s['files'] if r['path'].endswith('SensorsHid.c'))
    b=(HERE/'upstream/PaganiSensorsHid.c').read_bytes()
    if len(b)!=row['bytes'] or hashlib.sha256(b).hexdigest()!=row['sha256']:raise ValueError('Original sensor HID source differs')
    t=b.decode();start=t.index('#define PROPERTIES');end=t.index('static const uint8_t kMag[]',start)
    return ('/* SPDX-License-Identifier: BSD-2-Clause-Patent */\n/* Copyright (c) 2026, UniXNU contributors. All rights reserved. */\n'
        '/* Exact original OS17 sensor descriptors. No firmware/register data. */\n'+t[start:end]+'\n#undef AXES\n#undef PROPERTIES\n').encode()
