"""Compile/test the source-bound recipe->bounded-chunk->native-RTP-worker path.

The WaveCompiler's calibrated dual-AW86937 chunks now feed the SAME serialized
Broker/RTP worker through a pull-source clip over ONE live session. Host
coverage runs the ordering/refusal/cancel contract under ASan+UBSan on the
model GENI/AW peers; the arm64 step emits scalar objects for the touched TUs.
Synthetic carriers/LUT only: no measured calibration, no chip, no hardware.
"""
from pathlib import Path
import argparse,hashlib,json,subprocess,re,sys,shutil
ROOT=Path(__file__).resolve().parents[1];DRV=ROOT/'drivers/EldenHaptics'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',nargs='+',default=None)
    for key in ('firmware','llvm','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.firmware=a.firmware.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    cc=a.cc or (['xcrun','clang'] if sys.platform=='darwin' else ['gcc'])
    spec=json.loads((DRV/'source.json').read_text())
    if sha(a.firmware)!=spec['firmware']['sha256']:raise ValueError('Original firmware changed')
    def run(cmd,label,allow_empty=False):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; inspect retained log')
        return r.stdout.decode()
    # Prepare the haptics source dir: derived AW86937 core + vendored GENI +
    # the bus/chip/queue/rtp/broker/feed sources under test.
    h=out/'haptics';h.mkdir()
    sys.path.insert(0,str(DRV));from AwSource import derive;from GeniSource import sources
    (h/'AwChip.c').write_bytes(derive())
    for name,data in sources().items():(h/name).write_bytes(data)
    copies=['AwChip.h','AwKernelShim.h','AwRtp.h','Bus.c','Bus.h','Board.h','Platform.c','Platform.h',
            'Queue.c','Queue.h','RtpStream.c','RtpStream.h','Budget.c','Budget.h','Catalog.c','Catalog.h',
            'Mailbox.c','Mailbox.h','Broker.c','Broker.h','SampleMap.c','SampleMap.h',
            'WaveCompiler.c','WaveCompiler.h','WaveFeed.c','WaveFeed.h']
    for name in copies:shutil.copyfile(DRV/name,h/name)
    rows={p.name:sha(p) for p in h.iterdir() if p.is_file()}
    (h/'source.json').write_text(json.dumps({'sources':rows,'upstream':spec,'hardware':False},indent=2)+'\n')
    flags=[*cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',h]
    tua=['Budget.c','Catalog.c','Broker.c','Mailbox.c','Queue.c','RtpStream.c','GeniCore.c','Bus.c',
         'AwChip.c','SampleMap.c','WaveCompiler.c','WaveFeed.c']
    run([*flags,*[h/n for n in tua],DRV/'TestWaveFeed.c','-o',out/'test'],'host-build')
    msg=run([out/'test',a.firmware],'host-run')
    resource=run([a.llvm/'clang','-print-resource-dir'],'resource').strip();objects={}
    for name in tua:
        obj=out/(name.replace('.c','.o'))
        run([a.llvm/'clang','-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin',
             '-fno-stack-protector','-mgeneral-regs-only','-nostdinc','-isystem',Path(resource)/'include',
             '-Wno-error=incompatible-sysroot','-Wall','-Wextra','-Werror','-I',h,'-c',h/name,'-o',obj],name+'-native')
        dis=run([a.llvm/'llvm-objdump','--disassemble','--no-show-raw-insn',obj],name+'-asm')
        ops=re.findall(r'^\s*[0-9a-fA-F]+:\s+(.+)$',dis,re.M)
        if not ops or any(re.search(r'\b[vsdqhb][0-9]+(?:\b|\.)',s) for s in ops):raise ValueError('Non-scalar code')
        objects[name]=sha(obj)
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':msg,'objects':objects,
        'source_sha256':{n:sha(h/n) for n in copies+['source.json']},
        'recipe_to_chunk_to_rtp_feed':True,'one_live_session_per_recipe':True,'bounded_chunks_only':True,
        'no_userspace_pcm_gain_calibration':True,'native_RTP_profile_connected':True,
        'hardware_modeled':True,'measured_calibration_present':False,'hardware':False},indent=2)+'\n')
    print(msg)
if __name__=='__main__':main()
