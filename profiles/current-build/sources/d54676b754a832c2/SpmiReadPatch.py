"""Use the Gen4 Linux v7 completion contract in the pinned observer reader."""
import hashlib

BASE_SHA256 = '42aa61b7b6243ee28bea5d7658614bff7b7b9552e07c9239d4405dc94060b4f3'


def apply(data):
    if hashlib.sha256(data).hexdigest() != BASE_SHA256:
        raise ValueError('Unexpected frozen BtPmic.c')
    start = data.index(b'int pb_spmi_read8(')
    end = data.index(b'\n}\n', start) + 3
    body = data[start:end]
    before = b'''        if (status & 0xeu) return PbSpmiIoError;
        if (status & 1u) {
'''
    after = b'''        /* v7 reports a completed result only with DONE; error bits alone
         * are not a completed response. No command retry or permission change. */
        if (status & 1u) {
            if (status & 0xeu) return PbSpmiIoError;
'''
    if body.count(before) != 1:
        raise ValueError('Observer read completion changed')
    return data[:start] + body.replace(before, after) + data[end:]
