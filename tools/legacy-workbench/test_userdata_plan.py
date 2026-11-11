"""Reject ambiguous planning inputs while preserving the real backup geometry."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib
from userdata_plan import parse

root=Path(__file__).resolve().parents[1]
original=(root/'Backup/B_edl_stock/lun0/gpt_main0.bin').read_bytes()
parts=parse(original);assert parts['userdata']['sectors']==242865747
header=4096;table=struct.unpack_from('<Q',original,header+72)[0]*4096
count,stride=struct.unpack_from('<II',original,header+80)
occupied=[table+i*stride for i in range(count) if original[table+i*stride:table+i*stride+16]!=bytes(16)]
def fix(data):
    struct.pack_into('<I',data,header+88,zlib.crc32(data[table:table+count*stride]))
    struct.pack_into('<I',data,header+16,0)
    struct.pack_into('<I',data,header+16,zlib.crc32(data[header:header+92]))
    return bytes(data)
def reject(data,needle):
    try:parse(data);raise AssertionError('accepted '+needle)
    except ValueError as error:assert needle in str(error),(needle,str(error))
a,b=occupied[:2]
bad=bytearray(original);bad[b+56:b+128]=bad[a+56:a+128];reject(fix(bad),'ambiguous')
bad=bytearray(original);bad[b+16:b+32]=bad[a+16:a+32];reject(fix(bad),'GUID')
bad=bytearray(original);bad[b+32:b+48]=bad[a+32:a+48];reject(fix(bad),'overlap')
bad=bytearray(original);struct.pack_into('<Q',bad,a+32,1);reject(fix(bad),'usable')
bad=bytearray(original);bad[a+56]^=1;reject(bytes(bad),'CRC')
with tempfile.TemporaryDirectory() as directory:
    target=Path(directory)/'forbidden.json'
    for size in ('0','-256'):
        result=subprocess.run([sys.executable,str(root/'edl_work/userdata_plan.py'),'--gpt',
             str(root/'Backup/B_edl_stock/lun0/gpt_main0.bin'),'--size-gib',size,'--output',str(target)],capture_output=True)
        assert result.returncode and not target.exists()
report=dict(real_backup_geometry_unchanged=True,malformed_input_rejections=7,hardware_tested=False)
(root/'edl_work/stock-f2fs-20261106/gpt-plan-tests.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
