import argparse
import json
from pathlib import Path
import sys
import time

import minifh as fh
from avb_guard import AdmissionError
from firehose_transport import ProtocolError, delta_plan


def main():
    parser = argparse.ArgumentParser(description='Elden Firehose; plan is offline, signed writes require exact stock AVB admission')
    parser.add_argument('--port', default=fh.PORT)
    sub = parser.add_subparsers(dest='cmd', required=True)
    for name in ('load', 'nop', 'reset'):
        sub.add_parser(name)
    for name in ('plan_boot_a', 'delta_boot_a'):
        p = sub.add_parser(name)
        p.add_argument('new', type=Path)
        p.add_argument('reference', type=Path)
    p = sub.add_parser('write_boot_a')
    p.add_argument('image', type=Path)
    p = sub.add_parser('delta')
    p.add_argument('lun', type=int)
    p.add_argument('start', type=int)
    p.add_argument('sectors', type=int)
    p.add_argument('new', type=Path)
    p.add_argument('reference', type=Path)
    p = sub.add_parser('read')
    p.add_argument('lun', type=int)
    p.add_argument('start', type=int)
    p.add_argument('sectors', type=int)
    p.add_argument('output', type=Path)
    a = parser.parse_args()
    try:
        if a.cmd in ('plan_boot_a', 'delta_boot_a'):
            fh._admit(4, 113318, 24576, a.new)
            plan = delta_plan(a.new.read_bytes(), a.reference.read_bytes())
            print(json.dumps(plan, indent=2))
            if a.cmd == 'plan_boot_a':
                return 0
        elif a.cmd == 'write_boot_a':
            fh._admit(4, 113318, 24576, a.image)
        elif a.cmd == 'delta':
            fh._admit(a.lun, a.start, a.sectors, a.new)
            delta_plan(a.new.read_bytes(), a.reference.read_bytes())
        elif a.cmd == 'load' and not Path(fh.MELF).is_file():
            raise AdmissionError('accepted Sahara programmer is missing')
        elif a.cmd == 'read' and (not 0 <= a.lun <= 7 or a.start < 0 or a.sectors <= 0):
            raise AdmissionError('invalid read range')
        s = fh.open_port(a.port)
        try:
            if a.cmd == 'load':
                if not fh.sahara_upload(s, fh.MELF):
                    raise ProtocolError('Sahara programmer upload failed')
                time.sleep(1)  # once after loading, never per command/write run
                fh.configure(s)
                fh.send_xml(s, '<data><nop verbose="0"/></data>')
            else:
                fh.configure(s)
                if a.cmd == 'nop':
                    fh.send_xml(s, '<data><nop verbose="0"/></data>')
                elif a.cmd == 'reset':
                    fh.reset(s)
                elif a.cmd == 'write_boot_a':
                    fh.program(s, 4, 113318, 24576, a.image, 'boot_a')
                elif a.cmd == 'delta_boot_a':
                    fh.program_delta(s, 4, 113318, 24576, a.new, a.reference, 'boot_a')
                elif a.cmd == 'delta':
                    fh.program_delta(s, a.lun, a.start, a.sectors, a.new, a.reference, 'delta')
                elif a.cmd == 'read':
                    fh.read_part(s, a.lun, a.start, a.sectors, a.output)
        finally:
            s.close()
        return 0
    except (AdmissionError, ProtocolError, OSError, fh.serial.SerialException) as error:
        print('REFUSED/FAILED:', error, file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
