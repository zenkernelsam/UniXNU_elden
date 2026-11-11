"""Offline latency/delta benchmark. Does not open a COM port or claim USB speed."""
import argparse
import hashlib
import json
from pathlib import Path
import time

from firehose_transport import Firehose, delta_plan


class ModelPort:
    def __init__(self, data):
        self.rx = bytearray(data)
        self.timeout = 30
        self.wait_seconds = 0

    @property
    def in_waiting(self):
        return len(self.rx)

    def read(self, n):
        if len(self.rx) < n:
            self.wait_seconds += self.timeout
        data = bytes(self.rx[:n])
        del self.rx[:n]
        return data


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--new', type=Path, required=True)
    p.add_argument('--reference', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    ack = b'<data><response value="ACK" rawmode="false"/></data>'
    old, new = ModelPort(ack), ModelPort(ack)
    old.read(4096)
    Firehose(new).response()
    current, reference = a.new.read_bytes(), a.reference.read_bytes()
    start = time.perf_counter()
    strict = delta_plan(current, reference, overhead_bytes=0)
    merged = delta_plan(current, reference)
    elapsed = time.perf_counter() - start
    reconstructed = bytearray(reference)
    for first, count in merged['runs']:
        reconstructed[first*4096:(first+count)*4096] = current[first*4096:(first+count)*4096]
    if reconstructed != current:
        raise RuntimeError('delta plan does not reconstruct the admitted image')
    report = {'type': 'offline model; no hardware throughput measurement',
              'short_ack_old_timeout_seconds': old.wait_seconds,
              'short_ack_new_timeout_seconds': new.wait_seconds,
              'old_plan': strict, 'merged_plan': merged, 'planning_seconds': elapsed,
              'exact_image_reconstruction': True,
              'new_sha256': hashlib.sha256(current).hexdigest(),
              'reference_sha256': hashlib.sha256(reference).hexdigest()}
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: report[k] for k in ['type','short_ack_old_timeout_seconds',
          'short_ack_new_timeout_seconds','planning_seconds','exact_image_reconstruction']}, indent=2))
    print(f'runs {len(strict["runs"])} -> {len(merged["runs"])}; '
          f'bytes {strict["write_sectors"]*4096} -> {merged["write_sectors"]*4096}')


if __name__ == '__main__':
    main()
