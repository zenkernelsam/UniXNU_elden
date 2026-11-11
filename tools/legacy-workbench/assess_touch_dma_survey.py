"""Assess pinned touch surveys. Never grants DMA or SMMU-write permission."""
import argparse
import hashlib
import json
import re
from pathlib import Path

SESSION = '219d57d3edfcf40c'
IMAGE = 'd285afd934c83a23b5a7e2df39d822d32230d5e19d2d6b99bad355b2a57d127e'
SIDS = (0xa3, 0xb6)
CANDIDATES = {24: (SESSION, IMAGE),
              25: ('b2da49234e530d4e', '88083769901bbdcbe311598e1d2e411cedae7fca551555c31c3fbb22ee1852b5')}


def fields(line):
    return dict(re.findall(r'([\w-]+)=([^\s;]+)', line))


def assess(raw, metadata, flash, version=24):
    session, image = CANDIDATES[version]
    digest = hashlib.sha256(raw).hexdigest()
    errors = []
    if metadata.get('candidate_session') != session or metadata.get('raw_sha256') != digest:
        errors.append('log session or full raw SHA mismatch')
    if not metadata.get('bound_startup_milestone_observed'):
        errors.append('session-bound healthy root/Data startup not observed')
    if not flash.get('success') or flash.get('image_sha256') != image or flash.get('full_readback_sha256') != image:
        errors.append('pinned full device image verification missing')
    if flash.get('index') != 1738713600 or flash.get('footer_location') != 0 or not flash.get('gpt_identity_checked'):
        errors.append('stock index or target identity verification missing')
    text = raw.decode('utf-8', 'replace').replace('\r', '')
    lines = text.splitlines()
    statuses = [fields(line) for line in lines if line.startswith('ELDEN STATUS v1 ')]
    if not statuses or any(s.get('session') != session or s.get('dropped') != '0' for s in statuses):
        errors.append('missing, foreign or dropped status stream')
    expected = dict(configured='1', dtr='1', cache='0', data='0', init_exec='1', user_trap='1')
    if not any(all(s.get(k) == v for k, v in expected.items()) for s in statuses):
        errors.append('healthy startup status missing from raw stream')
    if re.search(r'panic\s*\(|kernel data abort', text, re.I):
        errors.append('kernel panic or data abort observed')
    platforms = [fields(l) for l in lines if l.startswith('ELDEN_TOUCH platform=')]
    stops = [fields(l) for l in lines if l.startswith('ELDEN_TOUCH stop ')]
    reports = [fields(l) for l in lines if l.startswith('ELDEN_TOUCH_DMA SMMU result=')]
    matches = [fields(l) for l in lines if l.startswith('ELDEN_TOUCH_DMA matches ')]
    if len(platforms) != 1 or platforms[0].get('mode') != '4' or platforms[0].get('platform') != '0':
        errors.append('one successful mode4 platform entry required')
    if len(stops) != 1 or any(stops[0].get(k) != '0' for k in ('error', 'released', 'quarantine', 'reports', 'invalid')):
        errors.append('survey did not stop cleanly without touch reports')
    if len(reports) != 1 or reports[0].get('result') != '0':
        errors.append('one successful global SMMU report required')
    if len(matches) != 1 or matches[0].get('DMA-qualified') != '0':
        errors.append('missing explicit unqualified survey result')
    routes = {sid: [] for sid in SIDS}
    contexts = {}
    try:
        for line in lines:
            if line.startswith('ELDEN_TOUCH_DMA route '):
                f = fields(line); sid = int(f['sid'], 16)
                if sid not in routes:
                    raise ValueError('unexpected SID')
                routes[sid].append(dict(slot=int(f['slot']), smr=int(f['smr'], 16),
                                        s2cr=int(f['s2cr'], 16), type=int(f['type']), bank=int(f['bank'])))
            elif line.startswith('ELDEN_TOUCH_DMA context '):
                f = fields(line); key = (int(f['sid'], 16), int(f['bank']))
                if key in contexts:
                    raise ValueError('repeated context record')
                contexts[key] = dict(cbar=int(f['cbar'], 16), cba2r=int(f['cba2r'], 16))
        if len(reports) == 1:
            f = reports[0]; cr0, id0, id1, id2 = (int(f[k], 16) for k in ('cr0', 'id0', 'id1', 'id2'))
            groups = int(f['groups'])
            if any(not 0 <= v < 0xffffffff for v in (cr0, id0, id1, id2)) or cr0 & 8 or id0 & 0x48000000 != 0x48000000:
                raise ValueError('unsupported global register format')
            if groups != (id0 & 255) or not 1 <= groups <= 255:
                raise ValueError('invalid group count')
            for sid, entries in routes.items():
                for r in entries:
                    mask = (r['smr'] >> 16) & 0x7fff
                    if not 0 <= r['slot'] < groups or not r['smr'] & 0x80000000 or (r['smr'] ^ sid) & 0xffff & ~mask:
                        raise ValueError('route does not match its reported SID/slot')
                    if r['type'] != (r['s2cr'] >> 16) & 3 or r['bank'] != r['s2cr'] & 255:
                        raise ValueError('route decoding mismatch')
                    r['mask'] = mask
                    r['context'] = contexts.get((sid, r['bank']))
                    if r['type'] == 0 and r['bank'] < (id1 & 255) and r['context'] is None:
                        raise ValueError('expected global context record missing')
        count_line = next((l for l in lines if l.startswith('ELDEN_TOUCH_DMA matches ')), '')
        counts = [(int(sid, 16), int(count)) for sid, count in re.findall(r'sid=([0-9a-fA-F]+) count=(\d+)', count_line)]
        if counts != [(sid, len(routes[sid])) for sid in SIDS]:
            raise ValueError('route count summary mismatch')
    except (KeyError, ValueError) as e:
        errors.append('malformed or incomplete survey: ' + str(e))
    decisions = {}
    for sid, entries in routes.items():
        if errors:
            next_step = 'retain log; resolve incomplete or mismatched evidence before using route values'
        elif not entries:
            next_step = 'no route; inspect complete slot ownership and firmware quirk before designing a new mapping'
        elif len(entries) != 1 or entries[0]['mask']:
            next_step = 'duplicate or masked route; resolve overlap and shared ownership before changes'
        elif entries[0]['type'] == 0:
            next_step = 'translated route; qualify only this bank access, SCTLR, sharing and address widths; global CBAR alone is insufficient'
        elif entries[0]['type'] == 1:
            next_step = 'inherited bypass reported; qualify address width, exact mapping stability, ownership and DMA buffers'
        else:
            next_step = 'fault/reserved route; inspect firmware ownership before defining an isolated mapping'
        decisions[f'{sid:x}'] = dict(routes=entries, next_step=next_step)
    return dict(session=session, version=version, raw_sha256=digest, survey_evidence_valid=not errors,
                errors=errors, streams=decisions, dma_qualified=False, smmu_writes_authorized=False,
                fastboot_return_requires_separate_pc_confirmation=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('raw', type=Path)
    p.add_argument('metadata', type=Path)
    p.add_argument('flash_result', type=Path)
    p.add_argument('--output', required=True, type=Path)
    p.add_argument('--version', type=int, choices=CANDIDATES, default=24)
    a = p.parse_args()
    result = assess(a.raw.read_bytes(), json.loads(a.metadata.read_text(encoding='utf-8')),
                    json.loads(a.flash_result.read_text(encoding='utf-8')), a.version)
    with a.output.open('x', encoding='utf-8') as f:
        f.write(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    if not result['survey_evidence_valid']:
        raise SystemExit(2)


if __name__ == '__main__':
    main()
