"""Read-only verification of the prepared packet. Never connects to a device."""
import argparse
import hashlib
import json
from pathlib import Path
from avb_guard import check_image,metadata
from root_image_preflight import decode_header

ROOT=Path(__file__).resolve().parents[1]


def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        while block:=stream.read(8<<20):h.update(block)
    return h.hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--packet',type=Path,default=ROOT/'edl_work/prerequisites-20261106/packet.json')
    a=p.parse_args();packet=json.loads(a.packet.read_text(encoding='utf-8'));results=[];errors=[]
    stock_props=metadata(ROOT/'signing/phase5_resign/boot_signed.img')['props']
    for item in packet['artifacts']:
        try:
            path=(ROOT/item['path']).resolve()
            if ROOT not in path.parents or not path.is_file():raise ValueError('artifact outside project or missing')
            if path.stat().st_size!=item['bytes'] or digest(path)!=item['sha256']:raise ValueError('size/SHA mismatch')
            if item['type']=='boot':
                admitted=check_image(path,'boot_a')
                if admitted.index!=1738713600 or admitted.location!=0 or metadata(path)['props']!=stock_props:
                    raise ValueError('stock AVB template differs')
            if item['type']=='root-prepared':
                with path.open('rb') as stream:header=stream.read(4096)
                root=decode_header(header,item['planned_partition_bytes'])
                if root['image_bytes']!=item['bytes'] or item.get('ready_to_flash') is not False:raise ValueError('root staging status/format differs')
            results.append(item['role']);print('PASS '+item['role'],flush=True)
        except Exception as error:errors.append(dict(role=item['role'],error=str(error)))
    for proof in packet['proofs']:
        try:
            path=(ROOT/proof['path']).resolve()
            if ROOT not in path.parents or digest(path)!=proof['sha256']:raise ValueError('evidence file changed or missing')
        except Exception as error:errors.append(dict(proof=proof['path'],error=str(error)))
    report=dict(passed=not errors,artifacts=results,proofs=len(packet['proofs']),errors=errors,
                device_access=False,new_images_hardware_tested=False)
    a.packet.with_name('packet-check.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))
    if errors:raise SystemExit(1)


if __name__=='__main__':main()
