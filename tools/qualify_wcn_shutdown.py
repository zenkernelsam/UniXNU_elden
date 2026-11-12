"""Compile the WCN shutdown adapters against an existing verified XNU context.

macOS/LLVM only, offline. This checks two objects and their stop symbol link;
it does not link a kernel, enable Bluetooth or qualify physical shutdown.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--prior-results',type=Path,required=True)
    p.add_argument('--sources',type=Path,required=True)
    p.add_argument('--llvm',type=Path,required=True)
    p.add_argument('--xnu-source',type=Path,required=True,help='Reconstructed XNU source for uncaptured Bluetooth kernel-control headers')
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    rows=json.loads(a.prior_results.read_text())
    selected=[v for v in rows.values() if v['source'].endswith('/CpuPerfSurvey.cpp')]
    if len(selected)!=1 or not selected[0]['accepted']: raise ValueError('Expected verified native C++ context')
    prior=selected[0]
    capture_path=a.prior_results.parent.parent/'capture.json'
    capture=json.loads(capture_path.read_text())
    if sha(capture_path)!=(a.prior_results.parent/'capture.sha256').read_text().strip():
        raise ValueError('Captured native context changed')
    jobs=[j for j in capture['jobs'] if j['file']==prior['source']]
    if len(jobs)!=1: raise ValueError('Ambiguous captured compile directory')
    virtual_root=prior['source'].split('/research/',1)[0]
    directory=Path(jobs[0]['directory']).relative_to(virtual_root).as_posix()
    if directory.startswith('research/'): directory='records/'+directory[len('research/'):]
    cwd=(a.prior_results.parent/'project'/directory).resolve(strict=True)
    if sha(Path(prior['output']))!=prior['sha256']: raise ValueError('Prior object changed')
    original=list(prior['command'])
    if '-cc1' not in original: raise ValueError('Expected captured Clang frontend command')
    if a.output.exists(): raise ValueError('Output must be new')
    a.output.mkdir(parents=True)
    files={p.relative_to(a.sources).as_posix():sha(p) for p in a.sources.rglob('*') if p.is_file()}
    old_overlay=Path(original[original.index('-ivfsoverlay')+1])
    overlay=json.loads(old_overlay.read_text())
    if overlay.get('fallthrough') is not False: raise ValueError('Expected closed source overlay')
    overlay['roots'] += [{'type':'file','name':str(p.resolve()),'external-contents':str(p.resolve())}
                         for p in a.sources.rglob('*') if p.is_file()]
    overlay_path=(a.output/'source-overlay.json').resolve()
    overlay_path.write_text(json.dumps(overlay)+'\n')
    original[original.index('-ivfsoverlay')+1]=str(overlay_path)
    extra=(a.output/'extra-headers').resolve();extra.mkdir()
    added_headers={}
    records={}
    for name in ('WcnPowerNative','NativeHost'):
        source=(a.sources/(name+'.cpp')).resolve()
        target=(a.output/(name+'.o')).resolve()
        command=[str(source) if x.endswith('/CpuPerfSurvey.cpp') else x for x in original]
        if '-main-file-name' in command: command[command.index('-main-file-name')+1]=source.name
        command[command.index('-o')+1]=str(target)
        command[2:2]=['-I',str(a.sources.resolve()),'-I',str(extra),'-D','ELDEN_BT_NATIVE_BUILD=1']
        for attempt in range(128):
            run=subprocess.run(command,cwd=cwd,capture_output=True,text=True)
            (a.output/(name+'.'+str(attempt)+'.log')).write_text(run.stdout+run.stderr)
            if not run.returncode: break
            missing=re.findall(r"fatal error: '([^']+)' file not found",run.stderr)
            if len(missing)!=1 or not missing[0].endswith('.h') or '..' in Path(missing[0]).parts: break
            relative=missing[0]
            if relative in added_headers: break
            roots=[a.xnu_source/k/relative for k in ('bsd','osfmk','iokit','libkern')]
            matches=[p.resolve() for p in roots if p.is_file() and p.resolve().is_relative_to(a.xnu_source.resolve())]
            if len(matches)!=1: break
            source_header=matches[0];dest=extra/relative;dest.parent.mkdir(parents=True,exist_ok=True)
            shutil.copyfile(source_header,dest)
            added_headers[relative]={'source':str(source_header),'sha256':sha(source_header)}
            overlay['roots'].append({'type':'file','name':str(dest),'external-contents':str(dest)})
            overlay_path.write_text(json.dumps(overlay)+'\n')
        if run.returncode: raise RuntimeError(name+' compilation failed: '+run.stderr[-4000:])
        assembly=subprocess.check_output([str(a.llvm/'llvm-objdump'),'--disassemble',str(target)],text=True)
        (a.output/(name+'.asm')).write_text(assembly)
        instructions=[m[1] for m in re.finditer(r'^\s*[0-9a-f]+:\s+[0-9a-f]{8}\s+(.+)$',assembly,re.M)]
        if not instructions: raise ValueError('No instructions parsed')
        if any(re.search(r'\b[qvdsbh]\d+(?:\b|\.)',op) for op in instructions):
            raise ValueError('New adapter uses FP/SIMD: '+name)
        symbols=subprocess.check_output([str(a.llvm/'llvm-nm'),str(target)],text=True)
        (a.output/(name+'.symbols')).write_text(symbols)
        records[name]={'source_sha256':sha(source),'object_sha256':sha(target),'command':command,
                       'instructions':len(instructions),'fp_simd':False,'symbols':symbols.splitlines()}
    power='\n'.join(records['WcnPowerNative']['symbols'])
    host='\n'.join(records['NativeHost']['symbols'])
    for name in ('_elden_wcn_request_stop','_elden_wcn_stop_requested'):
        if not re.search(r'\bT '+re.escape(name)+r'$',power,re.M): raise ValueError('Stop provider absent: '+name)
    if not re.search(r'\bU _elden_wcn_stop_requested$',host,re.M): raise ValueError('Host stop consumer absent')
    result={'objects':records,'inputs':files,'added_xnu_headers':added_headers,'prior_context_sha256':sha(a.prior_results),
            'stop_provider_consumer_match':True,'hardware_tested':False,'kernel_linked':False,
            'recovery_hook_full_native_build_pending':True,'device_operations':False}
    (a.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:result[k] for k in ('stop_provider_consumer_match','hardware_tested','kernel_linked')}))

if __name__=='__main__': main()
