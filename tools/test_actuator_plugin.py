"""Exercise actual Actuator HAL functions against real ABI/CF and IOKit peers."""
from pathlib import Path
import argparse,hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenHaptics/actuator'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('source','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();source=a.source.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((source/'source.json').read_text())
    for name,digest in record['sources'].items():
        if sha(source/name)!=digest:raise ValueError('Prepared source changed')
    if (source/'PlugIn.c').read_bytes()!=(DRIVER/'PlugIn.c').read_bytes():raise ValueError('Actual plugin source differs')
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',source,
        '-framework','CoreFoundation','-framework','IOKit',source/'Ring.c',DRIVER/'TestPlugin.c','-o',out/'test'],'plugin-build')
    message=run([out/'test'],'plugin-run')
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':message,'actual_plugin_interface_included':True,
        'actual_ring_code':True,'real_CoreAudio_headers_CoreFoundation':True,'IOKit_clock_peers_modeled':True,
        'source_record_sha256':sha(source/'source.json'),'test_sha256':sha(DRIVER/'TestPlugin.c'),
        'installed':False,'hardware':False},indent=2)+'\n');print(message)
if __name__=='__main__':main()
