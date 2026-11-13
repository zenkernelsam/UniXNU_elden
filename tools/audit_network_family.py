"""Independently inspect original networking metadata and execution bytes."""
from pathlib import Path
import argparse,hashlib,importlib.util,json,subprocess,sys
from link_v91_kernel import verify_original_executables
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    if sys.flags.optimize:raise SystemExit('Run without -O; validation assertions must remain enabled')
    ap=argparse.ArgumentParser(description=__doc__)
    for n in ('build','prelink-repository','output'):ap.add_argument('--'+n,type=Path,required=True)
    a=ap.parse_args();b=a.build;pp=ROOT/'profiles/v108-network-family/plan.json';p=json.loads(pp.read_text());r=json.loads((b/'result.json').read_text())
    pre=json.loads((ROOT/'profiles/v91/image-build/plan.json').read_text())['prelink']
    parser=b/'prelink-tool/native_prelink.py'
    original=subprocess.check_output(['git','-C',str(a.prelink_repository),'show',pre['commit']+':native_prelink.py'])
    assert parser.read_bytes()==original
    spec=importlib.util.spec_from_file_location('network_prelink_parser',parser);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
    image=(b/'kernel.macho').read_bytes();segments=m.macho(image)['segments'];info=next(s for s in segments if s['name']=='__PRELINK_INFO')
    at=m.backed_offset(segments,info['va'],info['filesize'],len(image));meta=m.unserialize_xml(image[at:at+info['filesize']])
    entries=[e for e in meta['_PrelinkInfoDictionary'] if e['CFBundleIdentifier']==p['added_component']];assert len(entries)==1
    entry=entries[0];assert entry['IOKitPersonalities']==p['personalities'] and entry['OSBundleLibraries']==p['bundle_libraries']
    k=json.loads((b/'kernel.json').read_text());expected=dict(pre['original_executable_sha256']);expected[p['added_component']]=p['executable_sha256']
    assert verify_original_executables(b/'kernel.macho',k,expected)==31
    assert set(k['components'])==set(expected)
    assert sha(b/'native.macho')==p['baseline_native_sha256']==r['native_sha256']
    assert sha(b/'kernel.macho')==r['kernel_sha256'] and sha(pp)==r['profile_sha256']
    result={'accepted_static':True,'kernel_sha256':r['kernel_sha256'],'profile_sha256':sha(pp),'original_apple_executables_hashed':31,'native_unchanged':True,'metadata_parser_sha256':sha(parser),'network_personalities':entry['IOKitPersonalities'],'bundle_libraries':entry['OSBundleLibraries'],'hardware_tested':False}
    with a.output.open('x') as f:json.dump(result,f,indent=2);f.write('\n')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
