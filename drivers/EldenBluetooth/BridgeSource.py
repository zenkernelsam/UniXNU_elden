"""Reproduce Elden's qualified bridge edits from the pinned Pagani source."""
import hashlib
from BridgeCompletion import prepare as complete

def prepare(c_bytes, h_bytes):
    if hashlib.sha256(c_bytes).hexdigest()!='36f976a64ad1b7b5cc6030b7582175afbebe2a38bc6c48e7e9e51eb47624b5eb':
        raise ValueError('Unexpected original bridge source')
    if hashlib.sha256(h_bytes).hexdigest()!='4a6543cc06257a32c8e9f0fbb863039cd82542aa26a6f59acc85d7e9c622c504':
        raise ValueError('Unexpected original bridge header')
    text=c_bytes.decode()
    def replace(old,new):
        nonlocal text
        if text.count(old)!=1:raise ValueError('Bridge edit is ambiguous')
        text=text.replace(old,new)
    replace('    if (opcode == PbOpErroneousData) stack_ready(b, now);',
            '    b->pending_opcode = opcode; /* ready requires a successful real reply */')
    replace('    b->pending_at = now;\n    if (opcode == PbOpReset)',
            '    if (!b->pending || b->pending_opcode != opcode) return 1; /* stale reply never settles another command */\n'
            '    b->pending_at = now;\n'
            '    if (!status && f[1] == PbEvtCommandComplete && opcode == PbOpErroneousData) stack_ready(b, now);\n'
            '    if (opcode == PbOpReset)')
    replace('    } else if (b->client) {','    } else if (b->client || b->pending || b->own_reset || ring_used(&b->to_uart)) {')
    replace('    const uint32_t bytes = ring_peek(&b->to_uart, b->out);',
            '    uint32_t bytes = ring_peek(&b->to_uart, b->out);\n'
            '    if (bytes && b->out[0] == PbH4Cmd && b->pending &&\n'
            '        (uint16_t)(b->out[1] | (uint16_t)b->out[2] << 8) != PbOpHostCompleted) bytes = 0;')
    replace('        if (!got) break;',
            '        if (got > sizeof b->in) {lock(b); trip(b, PbGuardWrite); unlock(b); break;}\n        if (!got) break;')
    replace('    else if (ring_used(&b->to_uart)) sleep_ms = 0;',
            '    else if (ring_used(&b->to_uart)) sleep_ms = b->pending ? PbBridgeBusyPollMs : 0;')
    header=h_bytes.decode()
    if header.count('    uint32_t pending;')!=1:raise ValueError('Bridge header edit is ambiguous')
    header=header.replace('    uint32_t pending;','    uint16_t pending_opcode;\n    uint32_t pending;')
    if hashlib.sha256(header.encode()).hexdigest()!='762c5dd0335f2c062ba1a191868ddce597280793da0eb1a148a5f76f73f91c24':
        raise ValueError('Derived bridge header differs')
    return complete(text.encode()),header.encode()
