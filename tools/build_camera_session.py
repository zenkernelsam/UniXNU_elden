"""Compile the original camera capture session against the prepared manifest."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
from replay_xnu_objects import sections,global_symbols
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
# Existing camera leaf APIs the session composition may import; nothing else.
LEAVES={
 '_elden_camera_dma_capture_bind','_elden_camera_dma_capture_prepare','_elden_camera_dma_capture_probe',
 '_elden_camera_dma_capture_queue_initial','_elden_camera_dma_capture_start','_elden_camera_dma_capture_service',
 '_elden_camera_dma_capture_frame','_elden_camera_dma_capture_release','_elden_camera_dma_capture_stop',
 '_elden_camera_dma_capture_shutdown','_elden_camera_output_open','_elden_camera_output_close',
 '_elden_camera_output_enable_irq','_elden_camera_output_pump_irq','_elden_camera_output_stop',
 '_elden_camera_output_pump','_elden_camera_output_acquire','_elden_camera_output_release',
 '_elden_camera_output_memory','_elden_camera_output_info','_elden_camera_output_status',
 '_elden_camera_service_publish','_elden_camera_service_withdraw','_elden_camera_service_output',
 '_elden_camera_service_withdraw_output'}
SESSION={'_elden_camera_capture_session_bind','_elden_camera_capture_session_begin','_elden_camera_capture_session_publish',
 '_elden_camera_capture_session_serve','_elden_camera_capture_session_stop','_elden_camera_capture_session_shutdown'}
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('source','llvm','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();a.source=a.source.resolve(strict=True);a.llvm=a.llvm.resolve(strict=True)
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    record=json.loads((a.source/'source.json').read_text())
    if record.get('actual_camera_capture_session_source') is not True:raise ValueError('Session source manifest profile absent')
    for name,digest in record['sources'].items():
        if sha(a.source/name)!=digest:raise ValueError('Actual prepared session source changed: '+name)
    def run(command,label):
        r=subprocess.run(list(map(str,command)),capture_output=True,timeout=180);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; original log retained')
        return r.stdout.decode()
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();obj=out/'CaptureSession.o'
    cmd=[a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector',
        '-mgeneral-regs-only','-nostdinc','-isystem',Path(resource)/'include','-Wall','-Wextra','-Werror',
        '-I',a.source,'-c',a.source/'CaptureSession.c','-o',obj]
    run(cmd,'CaptureSession-build')
    asm=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],'CaptureSession-asm')
    ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',asm,re.M)
    if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Capture session must remain scalar')
    if any(n.endswith('/__mod_init_func') for n in sections(obj)):raise ValueError('Automatic session startup')
    syms=global_symbols(obj);exported={n for n,v in syms.items() if v[0]&0xe};imported={n for n,v in syms.items() if not v[0]&0xe}
    if exported!=SESSION:raise ValueError('Unexpected session exports: '+repr(sorted(exported^SESSION)))
    if not imported<=LEAVES:raise ValueError('Unexpected session imports: '+repr(sorted(imported-LEAVES)))
    result={'capture_session_object':obj.name,'object_sha256':sha(obj),'source_manifest_sha256':sha(a.source/'source.json'),
            'compile_command':list(map(str,cmd)),'exports':sorted(exported),'imports':sorted(imported),
            'scalar':True,'automatic_startup':False,'linked_native_image':False,'hardware':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n',newline='\n');print('Built original camera capture session object '+obj.name)
if __name__=='__main__':main()
