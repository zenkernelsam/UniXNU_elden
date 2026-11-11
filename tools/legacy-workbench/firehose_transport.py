"""Buffered Firehose XML/raw transport; a COM device can carry USB bulk data.

One command at a time. Keep received bytes across XML/raw transitions; never
discard asynchronous input or assume Serial.write consumed the whole buffer.
"""
from collections import deque
from pathlib import Path
import time
import xml.etree.ElementTree as ET


class ProtocolError(RuntimeError):
    pass


def write_all(port, data, timeout=120):
    view = memoryview(data)
    done, deadline = 0, time.monotonic() + timeout
    while done < len(view):
        n = port.write(view[done:])
        if n is None or n <= 0 or n > len(view) - done:
            raise ProtocolError('invalid/zero serial write result')
        done += n
        if time.monotonic() > deadline:
            raise ProtocolError('serial write deadline exceeded')
    return done


class Firehose:
    def __init__(self, port):
        self.port = port
        self.rx = bytearray()
        self.responses = deque()
        self.logs = deque(maxlen=32)
        self.max_payload = 256 * 1024
        self.dirty = False

    def _receive(self, deadline, maximum=1 << 20):
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise ProtocolError('Firehose receive deadline exceeded')
        pending = self.port.in_waiting
        # read(4096) waits for ALL 4096 bytes or the serial timeout, even when
        # a complete short ACK is available. Wait for one byte, then drain
        # only the bytes the driver already reports, with a bounded timeout.
        previous = self.port.timeout
        self.port.timeout = min(0.25, remaining)
        try:
            data = self.port.read(min(pending, maximum) if pending else 1)
        finally:
            self.port.timeout = previous
        self.rx.extend(data)

    def _documents(self):
        while True:
            start = self.rx.find(b'<data')
            end = self.rx.find(b'</data>', max(0, start))
            if start < 0 or end < 0:
                if len(self.rx) > 1 << 20:
                    raise ProtocolError('unterminated Firehose XML exceeds 1 MiB')
                return
            end += 7
            document = bytes(self.rx[start:end])
            del self.rx[:end]
            try:
                root = ET.fromstring(document)
            except ET.ParseError as e:
                raise ProtocolError('malformed Firehose XML') from e
            for child in root:
                if child.tag == 'response':
                    self.responses.append((dict(child.attrib), document))
                elif child.tag == 'log':
                    self.logs.append(child.attrib.get('value', ''))
            # A raw read payload may already follow this ACK in the same
            # serial read. Do not search ahead for its eventual trailing XML
            # and accidentally consume the intervening binary bytes.
            if self.responses:
                return

    def response(self, timeout=120):
        deadline = time.monotonic() + timeout
        while not self.responses:
            self._documents()
            if self.responses:
                break
            self._receive(deadline)
        return self.responses.popleft()

    def drain(self):
        # During writes only: parse and retain final ACK/NAK that can arrive
        # immediately with the last raw data. Never do this during raw reads.
        pending = self.port.in_waiting
        if pending:
            previous = self.port.timeout
            self.port.timeout = 0
            try:
                self.rx.extend(self.port.read(pending))
            finally:
                self.port.timeout = previous
        self._documents()

    def command(self, xml):
        if self.dirty:
            raise ProtocolError('raw transfer incomplete: reset the device before sending further XML')
        if self.responses:
            raise ProtocolError('unconsumed response before new command')
        write_all(self.port, xml.encode('utf-8') + b'\0')
        return self.response()

    @staticmethod
    def ack(attributes, raw=None):
        if attributes.get('value', '').upper() != 'ACK':
            raise ProtocolError('target rejected command: ' + str(attributes))
        actual = attributes.get('rawmode', '').lower()
        if raw is True and actual != 'true':
            raise ProtocolError('target did not enter raw mode')
        if raw is False and actual == 'true':
            raise ProtocolError('target remained in raw mode')

    def program_data(self, xml, data, label='', progress=None):
        attrs, _ = self.command(xml)
        self.ack(attrs, raw=True)
        self.dirty = True
        start, sent = time.monotonic(), 0
        view = memoryview(data)
        try:
            while sent < len(view):
                size = min(self.max_payload, len(view) - sent)
                write_all(self.port, view[sent:sent + size])
                sent += size
                self.drain()
                if progress:
                    progress(sent, len(view))
            attrs, _ = self.response()
            self.ack(attrs, raw=False)
        except Exception:
            # Do not try another command/reset XML against a device that
            # might still be consuming a raw payload debt.
            raise
        self.dirty = False
        elapsed = time.monotonic() - start
        return {'label': label, 'bytes': sent, 'seconds': elapsed,
                'MiB_per_second': sent / max(elapsed, 1e-9) / (1 << 20)}

    def read_data(self, xml, count, outpath):
        attrs, _ = self.command(xml)
        self.ack(attrs, raw=True)
        self.dirty = True
        path = Path(outpath)
        partial = path.with_name(path.name + '.partial')
        need = count
        deadline = time.monotonic() + 120
        with partial.open('wb') as f:
            while need:
                if not self.rx:
                    self._receive(deadline, min(1 << 20, need))
                if not self.rx:
                    continue
                # XML terminator may be followed by a NUL, then raw bytes.
                # The protocol framing terminator is consumed by command;
                # raw zeros belong to the payload and cannot be stripped here.
                n = min(need, len(self.rx))
                f.write(self.rx[:n])
                del self.rx[:n]
                need -= n
                deadline = time.monotonic() + 120
        attrs, _ = self.response()
        self.ack(attrs, raw=False)
        self.dirty = False
        partial.replace(path)
        return count


def session(port):
    value = getattr(port, '_minifh_session', None)
    if value is None:
        value = Firehose(port)
        port._minifh_session = value
    return value


def delta_plan(new, reference, sector=4096, overhead_bytes=65536):
    if len(new) != len(reference) or not new or len(new) % sector:
        raise ValueError('delta requires equal nonempty, complete-sector images')
    changed, runs, begin = 0, [], None
    for i in range(len(new) // sector):
        different = new[i * sector:(i + 1) * sector] != reference[i * sector:(i + 1) * sector]
        if different:
            changed += 1
            if begin is None:
                begin = i
        elif begin is not None:
            runs.append((begin, i))
            begin = None
    if begin is not None:
        runs.append((begin, len(new) // sector))
    original_runs = len(runs)
    merged = []
    for start, end in runs:
        if merged and (start - merged[-1][1]) * sector <= overhead_bytes:
            merged[-1] = (merged[-1][0], end)
        else:
            merged.append((start, end))
    transferred = sum(end - start for start, end in merged)
    return {'sector_bytes': sector, 'image_bytes': len(new), 'changed_sectors': changed,
            'original_runs': original_runs, 'runs': [(start, end - start) for start, end in merged],
            'write_sectors': transferred, 'extra_equal_sectors': transferred - changed,
            'run_overhead_bytes': overhead_bytes}
