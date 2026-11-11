"""Decode complete CRC-checked ADSP log pages from a session-bound USB capture."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import zlib


def decode(data, gaps=()):
    if any(type(x) is not int or not 0 <= x <= len(data) for x in gaps):
        raise ValueError('Invalid capture gap')
    if list(gaps) != sorted(gaps):
        raise ValueError('Unordered capture gaps')
    points = [0, *sorted(set(gaps)), len(data)]
    result = []
    seen = set()
    for a, b in zip(points, points[1:]):
        active = None
        text = data[a:b].decode('ascii', errors='replace')
        # The USB control worker can interleave a complete status/ACK inside
        # a diagnostic row. Strip only its exact framing; CRC still validates.
        text = re.sub(r'\r?\nELDEN STATUS v1 session=[0-9a-f]{16}(?: [a-z_]+=[a-z0-9-]+)+\r?\n', '', text)
        text = re.sub(r'\r?\nELDEN ACK (?:HOME|TAP_SETTINGS|SWIPE_UP|FASTBOOT|CHARGE) session=[0-9a-f]{16}[^\r\n]*\r?\n', '', text)
        for raw in text.splitlines(keepends=True):
            if not raw.endswith('\n'):
                continue
            line = raw.strip()
            if line.startswith('ELDEN_ULOG_BEGIN'):
                match = re.fullmatch(r'ELDEN_ULOG_BEGIN sequence=(1[0-2]|[1-9]) epoch=(\d+) op=(18|23) bytes=8192 crc=([0-9a-f]{8})', line)
                if not match:
                    raise ValueError('Malformed ULog begin')
                key = (int(match[1]), int(match[2]))
                if key in seen:
                    raise ValueError('Repeated ULog page')
                seen.add(key)
                active = dict(sequence=key[0], epoch=key[1], opcode=int(match[3],16),
                              crc=int(match[4],16), payload=bytearray())
            elif line.startswith('ELDEN_ULOG_ROW'):
                if active is None:
                    continue  # never join a page across a recorded discontinuity
                match = re.fullmatch(r'ELDEN_ULOG_ROW sequence=(1[0-2]|[1-9]) epoch=(\d+) offset=(\d+) data=([0-9a-f]{128})',line)
                if not match or (int(match[1]),int(match[2])) != (active['sequence'],active['epoch']):
                    raise ValueError('Malformed/foreign ULog row')
                if int(match[3]) != len(active['payload']) or len(active['payload']) >= 8192:
                    raise ValueError('Repeated or missing ULog row')
                active['payload'].extend(bytes.fromhex(match[4]))
            elif line.startswith('ELDEN_ULOG_END'):
                if active is None:
                    continue
                match = re.fullmatch(r'ELDEN_ULOG_END sequence=(1[0-2]|[1-9]) epoch=(\d+) crc=([0-9a-f]{8})',line)
                if not match or (int(match[1]),int(match[2])) != (active['sequence'],active['epoch']):
                    raise ValueError('Malformed/foreign ULog end')
                payload = bytes(active.pop('payload'))
                if len(payload)!=8192 or zlib.crc32(payload)!=active['crc'] or int(match[3],16)!=active['crc']:
                    raise ValueError('Incomplete ULog page or CRC mismatch')
                active['payload'] = payload
                result.append(active)
                active = None
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log',type=Path);parser.add_argument('--session',required=True)
    parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    data=args.log.read_bytes();meta=json.loads(args.log.with_suffix('.json').read_text())
    if meta.get('candidate_session')!=args.session or meta.get('raw_sha256')!=hashlib.sha256(data).hexdigest():
        raise ValueError('Require a completed capture with matching SHA and session')
    pages=decode(data,[entry['offset'] for entry in meta.get('discontinuities',[])])
    args.output.mkdir(parents=True,exist_ok=True);report=[]
    for page in pages:
        raw=page.pop('payload');stem=f"ulog-{page['epoch']}-{page['sequence']}-{page['opcode']:x}"
        (args.output/(stem+'.bin')).write_bytes(raw)
        # Keep the full binary as evidence; NUL-terminated text is only a view.
        (args.output/(stem+'.txt')).write_text(raw.split(b'\0',1)[0].decode('utf-8',errors='replace'),encoding='utf-8')
        report.append(dict(**page,session=args.session,sha256=hashlib.sha256(raw).hexdigest(),empty=not raw[0]))
    (args.output/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
