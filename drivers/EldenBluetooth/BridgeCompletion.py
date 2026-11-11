"""Qualify and repair the frozen Pagani-derived bridge's completion envelope.

Only initialization readiness and reset require a status return here; other
command return parameters remain command-specific. No synthetic success replies.
"""
import hashlib

BEFORE_SHA256 = 'ba7f7767e422fb1b63c59fda7c784d4a3f0eb62da1a87edcd53212e1d8d66048'
OLD = '''    if (f[1] == PbEvtCommandComplete && bytes >= 6) {
        opcode = (uint16_t)(f[4] | (uint16_t)f[5] << 8);
        if (bytes >= 7) status = f[6];
    } else if (f[1] == PbEvtCommandStatus && bytes >= 7) {
        opcode = (uint16_t)(f[5] | (uint16_t)f[6] << 8);
        status = f[3];
'''
NEW = '''    if (f[1] == PbEvtCommandComplete) {
        /* H4 framing only checks the advertised length. A completed frame can
         * still lack the mandatory command header or this command's status. */
        if (bytes < 6) { trip(b, PbGuardCommand); return 1; }
        opcode = (uint16_t)(f[4] | (uint16_t)f[5] << 8);
        if (bytes >= 7) status = f[6];
        else if (opcode == PbOpErroneousData || opcode == PbOpReset) {
            trip(b, PbGuardCommand); return 1;
        }
        /* Opcode zero remains a valid credit notification with no status.
         * Other return parameters are interpreted by their native consumer. */
    } else if (f[1] == PbEvtCommandStatus) {
        if (bytes < 7) { trip(b, PbGuardCommand); return 1; }
        opcode = (uint16_t)(f[5] | (uint16_t)f[6] << 8);
        status = f[3];
'''

def prepare(data):
    if hashlib.sha256(data).hexdigest() != BEFORE_SHA256:
        raise ValueError('Changed Bluetooth bridge input needs review')
    text=data.decode('utf-8')
    if text.count(OLD)!=1:
        raise ValueError('Completion envelope must match exactly once')
    return text.replace(OLD,NEW).encode('utf-8')
