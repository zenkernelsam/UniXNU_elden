"""Use the actual EDK LZMA tool to compress and round-trip complete rescue assets."""
from pathlib import Path
import hashlib
import json
import lzma
import struct
import subprocess

root=Path.cwd()
work=root/'out/rescue-compressed-20261106'
tool=root/'third_party/mu_wide_platforms/MU_BASECORE/BaseTools/Source/C/bin/LzmaCompress'
report=json.loads((work/'raw-assets.json').read_text())
for name,item in report['assets'].items():
    source=work/name
    data=source.read_bytes()
    assert len(data)==item['bytes'] and hashlib.sha256(data).hexdigest()==item['sha256']
    packed=work/(name+'.lzma')
    decoded=work/(name+'.roundtrip')
    print('EDK encode:',name,len(data),'bytes',flush=True)
    subprocess.run([tool,'-e','-q','-o',packed,source],check=True)
    encoded=packed.read_bytes()
    assert struct.unpack_from('<Q',encoded,5)[0]==len(data)
    subprocess.run([tool,'-d','-q','-o',decoded,packed],check=True)
    assert hashlib.sha256(decoded.read_bytes()).hexdigest()==item['sha256']
    assert lzma.decompress(encoded)==data
    item.update(compressed_bytes=len(encoded),compressed_sha256=hashlib.sha256(encoded).hexdigest(),
                EDK_tool_roundtrip=True,independent_python_roundtrip=True)
    decoded.unlink()
    (work/'compression-progress.json').write_text(json.dumps(report,indent=2)+'\n')
    print(name,'->',len(encoded),'bytes',flush=True)
report['compression_complete']=True
report['total_compressed_bytes']=sum(item['compressed_bytes'] for item in report['assets'].values())
report['encoder_sha256']=hashlib.sha256(tool.read_bytes()).hexdigest()
report['EFI_decoder_executed']=False
(work/'compression-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2),flush=True)
