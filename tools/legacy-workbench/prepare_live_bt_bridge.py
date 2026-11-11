"""Produce scoped canonical bridge fixes for the actual Elden transport."""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parents[1]
def prepare(destination):
    ref=ROOT/'research/wcn-native-20261108/pagani'
    destination=Path(destination);destination.mkdir(parents=True,exist_ok=True)
    for name in ['BtBridge.c','BtBridge.h','BtStandIn.c','BtStandIn.h']:
        source=(ref/name).read_text(encoding='utf-8')
        if name=='BtBridge.h':
            old='    uint32_t pending;'
            assert source.count(old)==1
            source=source.replace(old,'    uint16_t pending_opcode;\n'+old)
        if name=='BtBridge.c':
            def replace(a,b):
                nonlocal source
                assert source.count(a)==1,a
                source=source.replace(a,b)
            replace('    if (opcode == PbOpErroneousData) stack_ready(b, now);',
                    '    b->pending_opcode = opcode; /* ready requires a successful real reply */')
            replace('    b->pending_at = now;\n    if (opcode == PbOpReset)',
                    '    if (!b->pending || b->pending_opcode != opcode) return 1; /* stale reply never settles another command */\n'
                    '    b->pending_at = now;\n'
                    '    if (!status && f[1] == PbEvtCommandComplete && opcode == PbOpErroneousData) stack_ready(b, now);\n'
                    '    if (opcode == PbOpReset)')
            replace('    } else if (b->client) {',
                    '    } else if (b->client || b->pending || b->own_reset || ring_used(&b->to_uart)) {')
            replace('    const uint32_t bytes = ring_peek(&b->to_uart, b->out);',
                    '    uint32_t bytes = ring_peek(&b->to_uart, b->out);\n'
                    '    if (bytes && b->out[0] == PbH4Cmd && b->pending &&\n'
                    '        (uint16_t)(b->out[1] | (uint16_t)b->out[2] << 8) != PbOpHostCompleted) bytes = 0;')
            replace('        if (!got) break;',
                    '        if (got > sizeof b->in) {lock(b); trip(b, PbGuardWrite); unlock(b); break;}\n'
                    '        if (!got) break;')
            replace('    else if (ring_used(&b->to_uart)) sleep_ms = 0;',
                    '    else if (ring_used(&b->to_uart)) sleep_ms = b->pending ? PbBridgeBusyPollMs : 0;')
        (destination/name).write_text(source,encoding='utf-8',newline='\n')
    record={'revision':'c888efd3ce1b817d13f28783a77cf689cb2255c9',
            'files':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in destination.glob('Bt*')},
            'changes':['ready after successful real reply','one outstanding HCI command',
                       'stale reply cannot settle pending','reconnect waits for own reset',
                       'UART overlength/error bounds','pending command queue sleeps']}
    (destination/'derived.json').write_text(json.dumps(record,indent=2)+'\n')
    return record
if __name__=='__main__':
    print(json.dumps(prepare(ROOT/'research/wcn-native-20261108/bridge-prepared'),indent=2))
