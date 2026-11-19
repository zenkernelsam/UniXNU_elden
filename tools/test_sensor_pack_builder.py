"""Test real fixed serializer/importer; all positive inputs stay host-only."""
from pathlib import Path
import argparse,copy,hashlib,importlib.util,io,json,struct,tarfile,tempfile
ROOT=Path(__file__).resolve().parents[1]
def sha(b):return hashlib.sha256(b).hexdigest()
def module(path):
    s=importlib.util.spec_from_file_location('sensor_builder',path);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    builder=module(ROOT/'tools/build_sensor_pack.py');dump=out/'host-only-dump';dump.mkdir()
    files={'/vendor/etc/sensors/sns_reg_config':b'MODELED CONFIG\n','/mnt/vendor/persist/sensors/calibration':b'MODELED REGISTRY'+b'x'*200000}
    tarpath=dump/'capture.tar'
    def tar_bytes(entries):
        b=io.BytesIO()
        with tarfile.open(fileobj=b,mode='w') as tar:
            for name,data,kind in entries:
                member=tarfile.TarInfo(name);member.size=len(data)
                if kind=='symlink':member.type=tarfile.SYMTYPE;member.linkname='../outside';member.size=0;tar.addfile(member)
                else:tar.addfile(member,io.BytesIO(data))
        return b.getvalue()
    tarpath.write_bytes(tar_bytes([(n[1:],b,'file') for n,b in files.items()]))
    ident={k+'_sha256':sha(('HOSTONLY '+k).encode()) for k in ('device','build','adsp','adsp_dtb')}
    record={'schema':1,'board':'lenovo-tb322fc','soc':'SM8750','provenance':'host-fixture','identity':ident,'library_path':'/vendor/dsp/sensors;/vendor/lib/rfsa/adsp',
        'sources':{'capture.tar':{'type':'tar','bytes':tarpath.stat().st_size,'sha256':sha(tarpath.read_bytes())}},
        'files':[{'path':n,'kind':'registry' if n.startswith('/mnt') else 'configuration','source':'capture.tar','member':n[1:],'bytes':len(b),'sha256':sha(b)} for n,b in files.items()]}
    capture=out/'host-only-capture.json';capture.write_text(json.dumps(record),encoding='utf-8')
    r=builder.build(dump,capture,out/'positive-host-only',True)
    assert r['host_fixture'] and not r['production_header_emitted'] and not (out/'positive-host-only/EldenSensorPackInputs.h').exists()
    assert len((out/'positive-host-only/seal.host-only.bin').read_bytes())==240
    blob=(out/'positive-host-only/pack.bin').read_bytes();assert blob[:8]==b'SSCPACK1'
    count,names,total,checksum,reserved=struct.unpack_from('<IIQII',blob,8);assert total==len(blob) and not reserved and sum(blob[32:32+24*count+names])&0xffffffff==checksum
    checks=4;scenarios=1;records=[]
    def refuses(label,data,source_changes=None,host=True):
        nonlocal checks,scenarios
        path=out/('bad-'+label+'.json');path.write_text(json.dumps(data),encoding='utf-8')
        destination=out/('rejected-'+label)
        try:builder.build(dump,path,destination,host)
        except (ValueError,FileNotFoundError,tarfile.TarError) as error:
            assert not destination.exists();records.append({'scenario':label,'refused':True,'reason':str(error)});checks+=1;scenarios+=1
        else:raise AssertionError('Invalid input accepted: '+label)
    refuses('host-provenance',record,host=False)
    wrong=copy.deepcopy(record);wrong['board']='lenovo-tb321fu';refuses('Gen3',wrong)
    wrong=copy.deepcopy(record);wrong['soc']='SM8650';refuses('wrong-soc',wrong)
    wrong=copy.deepcopy(record);wrong['identity']['adsp_sha256']='0'*64;refuses('missing-adsp',wrong)
    wrong=copy.deepcopy(record);wrong['files'][0]['sha256']='1'*64;refuses('file-hash',wrong)
    wrong=copy.deepcopy(record);wrong['sources']['capture.tar']['sha256']='1'*64;refuses('archive-hash',wrong)
    wrong=copy.deepcopy(record);wrong['files'].append(copy.deepcopy(wrong['files'][0]));refuses('duplicate-file',wrong)
    wrong=copy.deepcopy(record);wrong['files'][0]['path']='/vendor/etc/../sensors/x';refuses('path-traversal',wrong)
    wrong=copy.deepcopy(record);wrong['files'][0]['path']='//vendor/etc/sensors/x';refuses('network-path',wrong)
    wrong=copy.deepcopy(record);wrong['files']=wrong['files'][:1];refuses('missing-registry',wrong)
    wrong=copy.deepcopy(record);wrong['library_path']='/system/lib;/vendor/dsp';refuses('wrong-library',wrong)
    wrong=copy.deepcopy(record);wrong['library_path']='/vendor/dsp;/vendor/../dsp';refuses('library-traversal',wrong)
    original=tarpath.read_bytes()
    for label,extra in [('archive-duplicate',(record['files'][0]['member'],b'same','file')),
                         ('archive-symlink',('vendor/etc/sensors/link',b'','symlink')),
                         ('archive-traversal',('../vendor/etc/sensors/out',b'x','file'))]:
        b=tar_bytes([(n[1:],v,'file') for n,v in files.items()]+[extra]);tarpath.write_bytes(b)
        wrong=copy.deepcopy(record);wrong['sources']['capture.tar'].update(bytes=len(b),sha256=sha(b));refuses(label,wrong)
    tarpath.write_bytes(original)
    # Second build uses exact original serializer again, producing identical
    # bytes and namespace/registry seal while retaining host-only classification.
    second=builder.build(dump,capture,out/'repeat-host-only',True);assert second['seal']==r['seal'];checks+=1;scenarios+=1
    result={'passed':True,'scenarios':scenarios,'checks':checks,'actual_original_serializer':True,'source_input_hash_verification':True,
            'all_positive_inputs_host_only':True,'production_candidate_generated':False,'negative_scenarios':records,'positive_seal':r['seal'],'hardware':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps({'passed':True,'scenarios':scenarios,'checks':checks,'host_fixture':True}))
if __name__=='__main__':main()
