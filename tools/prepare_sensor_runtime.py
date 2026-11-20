"""Prepare native early memory/listener/SSC lifecycle from frozen component inputs."""
from pathlib import Path
import argparse,hashlib,json,shutil
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenMotion'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('qrtr-source','memory-source','pack-source','endpoint-source','ssc-source','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);parents={}
    for name in ('qrtr_source','memory_source','pack_source','endpoint_source','ssc_source'):
        folder=getattr(a,name).resolve(strict=True);record=json.loads((folder/'source.json').read_text());parents[str(folder)]=sha(folder/'source.json')
        for filename,digest in record['sources'].items():
            source=folder/filename
            if sha(source)!=digest:raise ValueError('Qualified runtime parent changed: '+filename)
            destination=out/(name+'-'+filename) if filename.startswith('LICENSE') else out/filename
            if destination.exists() and destination.read_bytes()!=source.read_bytes():
                if filename in ('NativeMemory.h','NativeSsc.h'):continue # same prepared import/current final owner contract
                raise ValueError('Parent source collision: '+filename)
            shutil.copyfile(source,destination)
    owned=('SensorRuntime.h','SensorRuntimeNative.cpp','SensorRuntimeMethods.inc','MemoryBinding.h','MemoryBinding.c',
           'NativeSsc.h','NativeSscMethods.inc')
    for name in owned:shutil.copyfile(DRIVER/name,out/name)
    record={'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'parents':parents,'current_owned_overlays':list(owned),
        'actual_prepared_memory_listener_SSC_runtime':True,'existing_QRTR_ADSP_owner_only':True,
        'real_physical_owner_capture_calibration_installed':False,'automatic_runtime_start':False,'hardware':False}
    (out/'source.json').write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8',newline='\n');print('Prepared native early-memory -> listener -> SSC lifecycle; all real authorities still required')
if __name__=='__main__':main()
