"""Source-bound QMI/SSC codec extraction, excluding donor hardware and fake data.
Explicit parser hardening rejects truncation, duplicates and uint64 overflow.
"""
from pathlib import Path
import hashlib,json,re
HERE=Path(__file__).resolve().parent
def derive():
    spec=json.loads((HERE/'source.json').read_text());data={}
    for row in spec['files']:
        raw=(HERE/'upstream'/row['file']).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=row['sha256']:raise ValueError('SSC donor source differs')
        data[row['file']]=raw.decode()
    source=data['T970Ssc.c'];header=data['T970Ssc.h']
    before=source[:source.index('/* ---- QRTR ---- */')];at=before.index('static void put32(');end=before.index('\n',at)
    before=before[:at]+before[end+1:]
    source=before+source[source.index('/* ---- QMI ---- */'):]
    header=header[:header.index('/* ---- QRTR (')]+header[header.index('/* ---- QMI ---- */'):]
    source=source.replace('"T970Ssc.h"','"SscPrivate.h"').replace('t970_','ssc_').replace('T970','Ssc')
    header=header.replace('T970_SSC_H','ELDEN_MOTION_SSC_PRIVATE_H').replace('t970_','ssc_').replace('T970','Ssc')
    for key in ('source','header'):
        text=source if key=='source' else header
        text=re.sub(r'(?m)^(size_t|int|uint32_t|int32_t) (ssc_\w+)',r'static \1 \2',text)
        if key=='source':source=text
        else:header=text
    deltas=[]
    def patch(before,after,reason):
        nonlocal source
        if source.count(before)!=1:raise ValueError('Expected unique SSC anchor: '+reason)
        source=source.replace(before,after);deltas.append(reason)
    patch('const uint8_t b = d[(*at)++];','const uint8_t b = d[(*at)++];\n        if (shift == 63 && (b & 0xfe)) return -1;','uint64 varint overflow')
    patch('f->field = (unsigned)(key >> 3);','if (!(key >> 3) || (key >> 3) > 0x1fffffff) return -1;\n    f->field = (unsigned)(key >> 3);','protobuf field bounds')
    patch('size_t at = 0; Field f; int rc;\n    s->low = s->high = 0;',
          'size_t at = 0; Field f; int rc; unsigned seen = 0;\n    s->low = s->high = 0;','SUID seen ledger')
    patch('if (f.field == 1 && f.wire == 1) s->low = f.value;\n        else if (f.field == 2 && f.wire == 1) s->high = f.value;',
          'if (f.field == 1 && f.wire == 1) {if(seen&1)return -1;seen|=1;s->low=f.value;}\n'
          '        else if (f.field == 2 && f.wire == 1) {if(seen&2)return -1;seen|=2;s->high=f.value;}\n'
          '        else if(f.field==1||f.field==2)return -1;','SUID duplicates/wire')
    patch('return rc;\n}\nstatic int ssc_sns_events', 'return rc < 0 || seen != 3 ? -1 : 0;\n}\nstatic int ssc_sns_events','complete SUID required')
    patch('SscSuid from = {0, 0};','SscSuid from = {0, 0}; unsigned suid_seen = 0;','message SUID ledger')
    patch('if (f.field == 1 && f.wire == 2 && suid(f.bytes, f.length, &from) < 0) return -2;',
          'if (f.field == 1) {if(f.wire!=2||suid_seen++||suid(f.bytes,f.length,&from)<0)return -2;}','one message SUID')
    patch('if (f.field == 1) {if(f.wire!=2||suid_seen++||suid(f.bytes,f.length,&from)<0)return -2;}',
          '{if(f.field==2&&f.wire!=2)return -1;\n        if (f.field == 1) {if(f.wire!=2||suid_seen++||suid(f.bytes,f.length,&from)<0)return -2;}}','event container wire checked')
    patch('if (rc < 0) return -1;\n    at = 0;', 'if (rc < 0 || suid_seen != 1) return -1;\n    at = 0;','message SUID required')
    patch('uint32_t id = 0; uint64_t timestamp = 0;', 'unsigned seen = 0; uint32_t id = 0; uint64_t timestamp = 0;','event seen ledger')
    patch('if (g.field == 1 && g.wire == 5) id = (uint32_t)g.value;\n            else if (g.field == 2 && g.wire == 1) timestamp = g.value;\n            else if (g.field == 3 && g.wire == 2) {payload = g.bytes; plen = g.length;}',
          'if (g.field == 1 && g.wire == 5) {if(seen&1)return -3;seen|=1;id=(uint32_t)g.value;}\n'
          '            else if (g.field == 2 && g.wire == 1) {if(seen&2)return -3;seen|=2;timestamp=g.value;}\n'
          '            else if (g.field == 3 && g.wire == 2) {if(seen&4)return -3;seen|=4;payload=g.bytes;plen=g.length;}\n'
          '            else if(g.field>=1&&g.field<=3)return -3;','event duplicates/wire')
    patch('if (erc < 0) return -3;', 'if (erc < 0 || seen != 7) return -3;','complete event required')
    patch('if (type_cap) type[0] = 0;', 'unsigned type_seen=0; if (type_cap) type[0] = 0;','discovery type ledger')
    patch('size_t n = f.length < type_cap - 1 ? f.length : type_cap - 1;',
          'if(type_seen++||!f.length||f.length>=type_cap)return -1;\n'
          '            for(size_t i=0;i<f.length;i++)if(!f.bytes[i])return -1;\n'
          '            size_t n = f.length;','discovery no truncation/NUL')
    patch('if (count < max && suid(f.bytes, f.length, &suids[count]) < 0) return -2;',
          'if(count>=max||suid(f.bytes,f.length,&suids[count])<0)return -2;','discovery overflow rejected')
    patch('            ++count;\n        }\n    }\n    if (rc < 0) return -1;\n    return (int)(count < max ? count : max);\n}\nstatic int ssc_sns_sensor_event',
          '            ++count;\n        } else if(f.field==1||f.field==2)return -1;\n    }\n    if (rc < 0) return -1;\n    return (int)(count < max ? count : max);\n}\nstatic int ssc_sns_sensor_event','discovery known field wires')
    patch('if (rc < 0) return -1;\n    return (int)(count < max ? count : max);\n}\nstatic int ssc_sns_sensor_event',
          'if (rc < 0 || type_seen != 1 || !count) return -1;\n    return (int)count;\n}\nstatic int ssc_sns_sensor_event','complete discovery required')
    patch('size_t at = 0; Field f; int rc; unsigned count = 0;\n    *status = -1;',
          'size_t at = 0; Field f; int rc; unsigned count = 0, status_seen = 0;\n    *status = -1;','sample status ledger')
    patch('else if (f.field == 1 && f.wire == 2) {\n            for', 'else if (f.field == 1 && f.wire == 2) {\n            if(f.length%4)return -1;\n            for','packed float tail rejected')
    patch('else if (f.field == 2 && f.wire == 0) *status = (int32_t)f.value;',
          'else if (f.field == 2 && f.wire == 0) {if(status_seen++||f.value>3)return -1;*status=(int32_t)f.value;}\n'
          '        else if(f.field==1||f.field==2)return -1;','sample duplicate/status/wire bounds')
    patch('if (rc < 0) return -1;\n    return (int)(count < max ? count : max);\n}',
          'if (rc < 0 || count > max || status_seen != 1) return -1;\n    return (int)count;\n}','sample no silent truncation')
    return source+'\n#include "Core.h"\n'+(HERE/'CorePublic.inc').read_text(),header,deltas
