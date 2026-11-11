#!/usr/bin/env python3
"""Regenerate the private v91 NT36536 header from the exact stock firmware."""
from pathlib import Path
import argparse,hashlib,json

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--firmware',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    data=a.firmware.read_bytes();h=hashlib.sha256(data).hexdigest()
    if len(data)!=253952 or h!='413216d0e8738078414fa681262439ab6d1c181f02974215e0394f102fcc08dc':raise SystemExit('Stock NT36536 firmware identity mismatch')
    if a.output.exists():raise SystemExit('Output must be new')
    text='// Generated stock firmware; never commit this file.\nstatic const uint8_t elden_nvt_firmware[]={\n'+'\n'.join(','.join('0x%02x'%v for v in data[i:i+32])+',' for i in range(0,len(data),32))+'\n};\n'
    raw=text.encode();digest=hashlib.sha256(raw).hexdigest()
    if digest!='8a7d6b35b058c69228e214cd8fa21f665a98e25bb44193ff4df544adf9c96dca':raise SystemExit('Generated header differs from v91')
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(raw)
    print(json.dumps({'firmware_sha256':h,'header_sha256':digest,'matches_v91':True,'device_operation':False},indent=2))

if __name__=='__main__':main()
