"""Reuse fixed same-OS17/SM8750 SSC wire code without board/router/RPC defaults."""
from pathlib import Path
import hashlib,json,re
HERE=Path(__file__).resolve().parent

def derive():
    record=json.loads((HERE/'ssc-source.json').read_text());data={}
    for row in record['files']:
        raw=(HERE/'upstream'/row['file']).read_bytes()
        if len(raw)!=row['bytes'] or hashlib.sha256(raw).hexdigest()!=row['sha256']:
            raise ValueError('Qualified SSC input changed: '+row['path'])
        data[row['file']]=raw.decode()
    # Pin the semantic contract, including framework event IDs which differ
    # from old T970 constants. Full original bodies remain exact after renaming.
    header=data['PaganiSensorsSsc.h']
    for anchor in ('SSC_QMI_SERVICE 0x190u','SSC_MSG_ATTR_EVENT = 128',
                   'SSC_MSG_ERROR_EVENT = 130','SSC_ATTR_PHYSICAL_SENSOR = 21',
                   'SSC_ATTR_RANGES = 11','SSC_STREAM_STREAMING = 0'):
        if header.count(anchor)!=1:raise ValueError('SSC schema anchor differs: '+anchor)
    original={}
    for source,dest in [('PaganiSensorsSsc.c','SscWire.c'),('PaganiSensorsSsc.h','SscWire.h'),
                        ('PaganiSensorsPb.c','SscPb.c'),('PaganiSensorsPb.h','SscPb.h')]:
        text=data[source].replace('"SensorsSsc.h"','"SscWire.h"').replace('"SensorsPb.h"','"SscPb.h"')
        text=re.sub(r'\bssc_', 'elden_ssc_wire_',text)
        text=re.sub(r'\bspb_', 'elden_ssc_pb_',text)
        text=text.replace('SENSORS_SSC_H','ELDEN_SSC_WIRE_H').replace('SENSORS_PB_H','ELDEN_SSC_PB_H')
        original[dest]=text
    body=data['PaganiSensorsSsc.c']
    start=body.index('typedef struct {\n    uint32_t kind;')
    end=body.index('\nvoid ssc_attrs_clear',start)
    strict=body[start:end].replace('spb_','elden_ssc_pb_')
    changes=[]
    def once(before,after,reason):
        nonlocal strict
        if strict.count(before)!=1:raise ValueError('Attribute anchor differs: '+reason)
        strict=strict.replace(before,after);changes.append(reason)
    once('if (field != 1 || wire != SPB_LEN) {',
         'if (field == 1 && wire != SPB_LEN) return -1;\n        if (field != 1 || wire != SPB_LEN) {',
         'known repeated datum wire')
    once('            d.kind = field;',
         '            if (d.kind) return -1; /* proto2 oneof must be unambiguous */\n            d.kind = field;',
         'duplicate oneof rejected')
    once('else {if (elden_ssc_pb_skip(&one, wire)) return -1; continue;}',
         'else {if (field >= 1 && field <= 5) return -1; if (elden_ssc_pb_skip(&one, wire)) return -1; continue;}',
         'known oneof wire')
    once('d.sint = v != 0;', 'if (v > 1) return -1; d.sint = (int64_t)v;', 'strict boolean')
    once('        if (d.kind && *count < max) out[(*count)++] = d;',
         '        if (!d.kind || *count >= max) return -1;\n        out[(*count)++] = d;',
         'no dropped datum or truncated list')
    original['SscAttrPrivate.inc']=strict
    return original,changes
