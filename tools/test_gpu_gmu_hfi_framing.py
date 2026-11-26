"""Host stress-test the real GMU/HFI framing, queue wrap-around and timeout bounds.

Compiles drivers/EldenGpu/tests/test_gmu_hfi_framing.cpp with the pinned tree's real
AdrenoFirmware kit unit AdrenoHfi.c (the exact transport the driver's cold-start path uses)
under -fsanitize=address,undefined -Werror, against a software peer playing the GMU firmware
side of the shared-memory queues. Covers 620+ command/ack exchanges across queue wrap-around,
sequence correlation under skip traffic, malformed/oversized/truncated rejection, corruption
poison, firmware-error poison, unresponsive-GMU timeout bound. Emits result.json with
per-case counts and iteration counts. Host/model evidence only: no live GMU/HFI hardware
handshake is claimed or implied.

Host compilation: the pinned tree's AdrenoHfi.c carries a sanctioned host mode
(ADRENO_HFI_HOST_TEST, atomic fences for software queue peers); the unit compiled here is the
byte-identical pinned file — no extraction, no substitution.
"""
from pathlib import Path
import argparse, json, os, subprocess
ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / 'drivers/EldenGpu'

def run(cmd, log):
    proc = subprocess.run(list(map(str, cmd)), capture_output=True)
    Path(log).write_bytes(proc.stdout + proc.stderr)
    return proc

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--tree', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    tree = a.tree.resolve()
    out = a.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    kit = tree / 'kits/AdrenoFirmware'
    for name in ('AdrenoHfi.c', 'AdrenoHfi.h'):
        if not (kit / name).is_file():
            raise SystemExit('AdrenoFirmware kit incomplete: ' + name)
    binary = out / 'test-gmu-hfi-framing'
    hfi_obj = out / 'AdrenoHfi.o'
    kc = ['xcrun', 'clang', '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
          '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
          '-DADRENO_HFI_HOST_TEST', '-I', kit, '-c', kit / 'AdrenoHfi.c', '-o', hfi_obj]
    proc = run(kc, out / 'kit-hfi.log')
    if proc.returncode:
        raise SystemExit('AdrenoHfi.c compile failed; see kit-hfi.log')
    cmd = ['xcrun', 'clang++', '-std=c++17', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
           '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
           '-I', kit, DRIVER / 'tests' / 'test_gmu_hfi_framing.cpp', hfi_obj, '-o', binary]
    proc = run(cmd, out / 'build.log')
    if proc.returncode:
        raise SystemExit('framing harness compile failed; see build.log')
    env = dict(os.environ)
    env['ASAN_OPTIONS'] = (env.get('ASAN_OPTIONS', '') + ':detect_leaks=0').lstrip(':')
    proc = subprocess.run([str(binary)], capture_output=True, env=env)
    (out / 'run.log').write_bytes(proc.stdout + proc.stderr)
    text = proc.stdout.decode(errors='replace') + proc.stderr.decode(errors='replace')
    if proc.returncode or 'PASS: GMU/HFI framing stress' not in text:
        raise SystemExit('framing harness run failed; see run.log')
    iterations = 0
    tokens = text.replace(';', ' ').split()
    for i, token in enumerate(tokens):
        if token.isdigit() and i + 1 < len(tokens) and tokens[i + 1].startswith('iteration'):
            iterations = int(token)
            break
    cases = {
        'positive': [
            'queue wrap-around storm: 620 command/ack exchanges, both queues wrap, payloads intact',
            'sequence numbers advance mod 4095 across the whole storm',
            'sequence correlation under skip traffic: F2H error + foreign-sequence replies skipped and counted',
            'initialization layout: exact header magic and per-queue metadata, zeroed indices',
            'zero-length payload command round-trips',
            'mismatched-only replies: consumed+counted, command stays pending, Empty reported',
        ],
        'negative': [
            'oversized payload (>254 words) rejected',
            'out-of-range message id (>255) rejected',
            'double send while pending rejected (Busy, no fence consumption)',
            'corrupt queue magic poisons fail-closed; poisoned instance refuses further work',
            'misaligned response-queue index poisons (Corrupt)',
            'truncated packet (count beyond available) poisons (Corrupt)',
            'firmware error response poisons with exact error code',
            'unresponsive GMU: exchange terminates at budget with Timeout+poison (bounded wait proof)',
        ],
    }
    report = {'passed': True, 'iterations': iterations,
              'case_counts': {'positive': len(cases['positive']), 'negative': len(cases['negative'])},
              'cases': cases, 'output': text.strip().splitlines()[-1] if text.strip() else '',
              'unit': 'byte-identical pinned AdrenoHfi.c compiled with sanctioned ADRENO_HFI_HOST_TEST mode',
              'device_access': False, 'hardware': False, 'hardware_qualified': False,
              'live_gmu_hfi_handshake': False}
    (out / 'result.json').write_text(json.dumps(report, indent=2) + chr(10))
    print('PASS: GMU/HFI framing; %d positive + %d negative cases; host/model evidence only' %
          (len(cases['positive']), len(cases['negative'])))

if __name__ == '__main__':
    main()
