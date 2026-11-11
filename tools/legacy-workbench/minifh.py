# minifh.py — minimal Firehose client over raw COM serial
# protocol: send xml + NUL, read xml responses until </data> complete
import serial, sys, time, os, struct
from pathlib import Path
import hashlib
import re
from xml.sax.saxutils import quoteattr
from firehose_transport import session, write_all, delta_plan, ProtocolError
from avb_guard import check_image, AdmissionError

PORT = 'COM7'
BAUD = 115200

def open_port(port=PORT):
    s = serial.Serial(port, BAUD, timeout=30, write_timeout=120)
    # A mid-state Sahara request or an early Firehose ACK is evidence, not
    # disposable startup noise. Do not reset the input buffer here.
    try:
        s.set_buffer_size(rx_size=1048576, tx_size=1048576)
    except (AttributeError, NotImplementedError, OSError):
        pass
    return s

# ---------- Sahara loader upload ----------
# cmd ids: HELLO_REQ=1 HELLO_RSP=2 READ_DATA=3 END_TRANSFER=4 DONE_REQ=5
#          DONE_RSP=6 RESET_REQ=7 READ_DATA_64=0x12
def _hello_rsp(mode=0):
    # mode 0 = IMAGE_TX_PENDING
    return struct.pack('<12I', 2, 0x30, 2, 1, 0, mode, 1, 2, 3, 4, 5, 6)

def _read_exact(s, n, tmo=10):
    buf = b''; t0 = time.time()
    while len(buf) < n:
        c = s.read(n - len(buf))
        if c: buf += c
        elif time.time() - t0 > tmo: break
    return buf

def sahara_upload(s, melf_path):
    """Serve the Sahara image-transfer state machine until firehose is up.
    Handles both fresh handshakes (HELLO_REQ) and mid-state devices that are
    already sitting on an unanswered READ_DATA request."""
    prog = open(melf_path, 'rb').read()
    s.timeout = 1
    # poke: device may be waiting in a state expecting a HELLO_RSP it never got
    s.write(_hello_rsp(0))
    t0 = time.time()
    while True:
        hdr = _read_exact(s, 8, tmo=15)
        if len(hdr) < 8:
            if time.time() - t0 > 30:
                print('sahara: no response, giving up'); return False
            s.write(_hello_rsp(0)); continue
        cmd, plen = struct.unpack('<II', hdr)
        if not 8 <= plen <= 4096:
            raise ProtocolError('invalid Sahara packet length; do not flush arbitrary byte counts')
        if cmd == 1:                       # HELLO_REQ (48B): consume rest, reply
            _read_exact(s, plen - 8, 5)
            s.write(_hello_rsp(0))
            print('sahara: HELLO handshake')
            continue
        if cmd == 3:                       # READ_DATA (20B): image_id,off,len u32
            rest = _read_exact(s, plen - 8, 5)
            iid, off, n = struct.unpack('<III', rest[:12])
        elif cmd == 0x12:                  # READ_DATA_64 (32B): u64 fields
            rest = _read_exact(s, plen - 8, 5)
            iid, off, n = struct.unpack('<QQQ', rest[:24])
        elif cmd == 4:                     # END_TRANSFER: consume, send DONE
            rest = _read_exact(s, plen - 8, 5)
            iid, status = struct.unpack('<II', rest[:8])
            print(f'sahara: END_TRANSFER id={iid} status={status}')
            if status != 0:
                return False
            s.write(struct.pack('<II', 5, 8))          # DONE_REQ
            d = _read_exact(s, 8, 10)
            if len(d) >= 8 and struct.unpack('<I', d[:4])[0] == 6:
                print('sahara: DONE -> firehose coming up')
                s.timeout = 30
                return True
            print('sahara: no DONE_RSP', d[:32]); return False
        else:
            print(f'sahara: unexpected cmd {cmd:#x} len={plen} — flushing')
            _read_exact(s, plen - 8, 2)
            continue
        if n <= 0 or off > len(prog) or n > len(prog) - off:
            raise ProtocolError('Sahara requested data outside the accepted programmer')
        data = prog[off:off + n]
        write_all(s, data)
        print(f'  sahara: served id={iid} @{off:#x}+{n} ({100*(off+n)//len(prog)}%)', flush=True)

def read_resp(s, want_ack=True, quiet=False):
    """read until a <response .../> block completes; return all text"""
    _, document = session(s).response()
    txt = document.decode('utf-8', 'replace')
    if not quiet: print(txt.strip()[:800])
    return txt

def send_xml(s, xml):
    attrs, document = session(s).command(xml)
    session(s).ack(attrs)
    return document.decode('utf-8', 'replace')

def configure(s, max_payload=1048576):
    xml = ('<?xml version="1.0" ?><data>'
           '<configure MemoryName="ufs" Verbose="0" AlwaysValidate="0" '
           f'MaxDigestTableSizeInBytes="8192" MaxPayloadSizeToTargetInBytes="{max_payload}" '
           'AckRawDataEveryNumPackets="0" '
           'ZLPAwareHost="1" SkipStorageInit="0" SkipWrite="0"/></data>')
    transport = session(s)
    for _ in range(3):
        attrs, document = transport.command(xml)
        maximum = int(attrs.get('MaxPayloadSizeToTargetInBytes', max_payload))
        if maximum <= 0 or maximum > 16 * 1024 * 1024:
            raise ProtocolError('invalid target payload size')
        if attrs.get('value') == 'ACK':
            transport.max_payload = min(maximum, max_payload)
            return document.decode('utf-8', 'replace')
        if attrs.get('value') not in ('NAK', 'NACK') or maximum >= max_payload:
            transport.ack(attrs)
        xml = xml.replace(f'MaxPayloadSizeToTargetInBytes="{max_payload}"',
                          f'MaxPayloadSizeToTargetInBytes="{maximum}"')
        max_payload = maximum
    raise ProtocolError('configure did not converge')


# Admission is based on physical ranges, not an arbitrary caller label.
PROTECTED = [(4, 113318, 24576, 'boot_a'), (4, 321824, 24576, 'boot_b'),
             (4, 179850, 25600, 'recovery_a'), (4, 380676, 25600, 'recovery_b'),
             (4, 137946, 16, 'vbmeta_a'), (4, 346452, 16, 'vbmeta_b'),
             (0, 5857672, 16, 'vbmeta_system_a'), (0, 5857688, 16, 'vbmeta_system_b'),
             (4, 205962, 2048, 'init_boot_a'), (4, 406788, 2048, 'init_boot_b'),
             (4, 147530, 24576, 'vendor_boot_a'), (4, 356036, 24576, 'vendor_boot_b'),
             (4, 137962, 8192, 'dtbo_a'), (4, 346468, 8192, 'dtbo_b')]


def _admit(lun, start_sector, num_sectors, filepath, parent_vbmeta=None):
    if not 0 <= lun <= 7 or start_sector < 0 or num_sectors <= 0:
        raise AdmissionError('invalid LUN/sector range')
    if Path(filepath).stat().st_size != num_sectors * 4096:
        raise AdmissionError('file size must exactly equal the declared raw transfer')
    for plun, first, sectors, name in PROTECTED:
        if lun == plun and start_sector < first + sectors and start_sector + num_sectors > first:
            if (start_sector, num_sectors) != (first, sectors):
                raise AdmissionError('partial signed-partition writes require an admitted full-image delta plan')
            return check_image(filepath, name, parent_vbmeta)
    return None


def _program_data(s, lun, start_sector, num_sectors, data, label):
    if len(data) != num_sectors * 4096:
        raise AdmissionError('raw data length differs from sector count')
    xml = ('<?xml version="1.0" ?><data>'
           f'<program SECTOR_SIZE_IN_BYTES="4096" file_sector_offset="0" '
           f'filename={quoteattr(label)} num_partition_sectors="{num_sectors}" '
           f'partofsingleimage="false" physical_partition_number="{lun}" '
           f'readbackverify="false" sparse="false" start_sector="{start_sector}"/></data>')
    result = session(s).program_data(xml, data, label)
    print(f'{label}: OK {result["bytes"]} bytes in {result["seconds"]:.3f}s '
          f'({result["MiB_per_second"]:.2f} MiB/s)', flush=True)
    return True


def target_sha256(s, lun, start_sector, num_sectors):
    """Return a reported whole-range digest, or None after a clean unsupported reply.

    A timeout/framing error still aborts. Do not guess a delta baseline just
    because a filename was supplied by the host.
    """
    transport = session(s)
    transport.logs.clear()
    xml = (f'<data><getsha256digest SECTOR_SIZE_IN_BYTES="4096" '
           f'physical_partition_number="{lun}" start_sector="{start_sector}" '
           f'num_partition_sectors="{num_sectors}"/></data>')
    attrs, _ = transport.command(xml)
    if attrs.get('rawmode', '').lower() == 'true':
        transport.dirty = True
        raise ProtocolError('digest command unexpectedly entered raw mode')
    if attrs.get('value', '').upper() in ('NAK', 'NACK'):
        return None
    transport.ack(attrs)
    text = ' '.join([*attrs.values(), *transport.logs])
    digests = {x.lower() for x in re.findall(r'(?<![0-9a-fA-F])[0-9a-fA-F]{64}(?![0-9a-fA-F])', text)}
    return next(iter(digests)) if len(digests) == 1 else None

def program(s, lun, start_sector, num_sectors, filepath, label='', parent_vbmeta=None):
    checked = _admit(lun, start_sector, num_sectors, filepath, parent_vbmeta)
    data = Path(filepath).read_bytes()
    if checked and hashlib.sha256(data).hexdigest() != checked.sha256:
        raise AdmissionError('image changed after AVB admission')
    return _program_data(s, lun, start_sector, num_sectors, data, label)

def program_delta(s, lun, start_sector, num_sectors, new_path, ref_path, label='', overhead_bytes=65536):
    """incremental write: only sectors differing between ref(=on-disk) and new image"""
    S = 4096
    checked = _admit(lun, start_sector, num_sectors, new_path)
    new = Path(new_path).read_bytes()
    if checked and hashlib.sha256(new).hexdigest() != checked.sha256:
        raise AdmissionError('delta image changed after AVB admission')
    expect = num_sectors * S
    if len(new) != expect:
        print(f'{label}: SIZE MISMATCH new={len(new)} expect={expect} — abort (delta keeps disk==new invariant)')
        return False
    ref = Path(ref_path).read_bytes()
    if len(ref) != len(new):
        raise AdmissionError('delta reference must have the exact full image size; no zero-padding guesses')
    plan = delta_plan(new, ref, S, overhead_bytes)
    reported = target_sha256(s, lun, start_sector, num_sectors)
    if reported != hashlib.sha256(ref).hexdigest():
        print(f'{label}: target baseline unverified/mismatched; writing the admitted full image', flush=True)
        return _program_data(s, lun, start_sector, num_sectors, new, label)
    print(f'{label}: {plan["original_runs"]} changed runs -> {len(plan["runs"])} writes; '
          f'{plan["changed_sectors"]} changed, {plan["write_sectors"]} sent sectors')
    for st, cnt in plan['runs']:
        _program_data(s, lun, start_sector + st, cnt,
                      memoryview(new)[st*S:(st+cnt)*S], f'{label}@{st}+{cnt}')
    return True

def reset(s):
    return send_xml(s, '<?xml version="1.0" ?><data><power value="reset"/></data>')

def read_part(s, lun, start, nsec, outpath):
    xml = ('<?xml version="1.0" ?><data>'
           f'<read SECTOR_SIZE_IN_BYTES="4096" num_partition_sectors="{nsec}" '
           f'physical_partition_number="{lun}" sparse="false" start_sector="{start}"/></data>')
    need = nsec * 4096
    session(s).read_data(xml, need, outpath)
    print(f'read {outpath}: {need}B')
    return True

MELF = 'D:/Desktop/Git/Patch/Y700-Elden/vendor-pack/y700-hyper-cloud/images/xbl_s_devprg_ns.melf'


if __name__ == '__main__':
    from firehose_cli import main
    sys.exit(main())
