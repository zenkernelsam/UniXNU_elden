"""Recompile only the hash-bound pmap unit over a qualified v105 native link."""
from pathlib import Path
import argparse, hashlib, io, json, os, re, subprocess, sys, tarfile
from rt_memory_patch import derive
from replay_xnu_objects import global_symbols
from link_v91_kernel import verify_original_executables

ROOT = Path(__file__).resolve().parents[1]
PROFILE = ROOT / 'profiles/v106-rt-memory/plan.json'


def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p): return json.loads(p.read_text())
def write(p, value): p.write_text(json.dumps(value, indent=2) + '\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('baseline', 'core', 'prelink-repository', 'original-kernelcache', 'llvm', 'output'):
        p.add_argument('--' + n, type=Path, required=True)
    a = p.parse_args(); out = a.output.resolve(); base = a.baseline.resolve(); plan = read(PROFILE)
    if out.exists(): raise ValueError('New output directory required')
    for name in ('native', 'kernel'):
        if sha(base / (name + '.macho')) != plan['baseline_' + name + '_sha256']:
            raise ValueError('Wrong v105 baseline')
    if sha(ROOT / 'tools/rt_memory_patch.py') != plan['patcher_sha256']:
        raise ValueError('Pmap patcher changed')
    cap = read(a.core / 'capture.json'); results = read(a.core / 'build/results.json')
    index = plan['core_job']; job = cap['jobs'][index]; prior = results[str(index)]
    if not prior['accepted'] or not job['file'].endswith('/arm/pmap/pmap.c'):
        raise ValueError('Wrong pmap build unit')
    if sha(Path(prior['output'])) != prior['sha256'] or prior.get('postprocess'):
        raise ValueError('Parent object changed or postprocessed')
    source = Path(cap['files'][job['file']]['path']); derived = derive(source.read_bytes())
    if hashlib.sha256(derived).hexdigest() != plan['derived_source_sha256']:
        raise ValueError('Derived pmap source differs')
    out.mkdir(parents=True); (out / 'source').mkdir(); (out / 'objects').mkdir()
    newsource = out / 'source/pmap.c'; newsource.write_bytes(derived)
    command = list(prior['command']); position = command.index('-ivfsoverlay') + 1
    overlay = read(Path(command[position]))
    if overlay.get('fallthrough') is not False: raise ValueError('Expected closed compiler overlay')
    selected = [e for e in overlay['roots'] if e['name'] == prior['source']]
    if len(selected) != 1 or sha(Path(selected[0]['external-contents'])) != sha(source):
        raise ValueError('Wrong source binding')
    original = selected[0]['external-contents']
    for e in overlay['roots']:
        if e['external-contents'] == original: e['external-contents'] = str(newsource)
    ov = out / 'source-overlay.json'; write(ov, overlay); command[position] = str(ov)
    obj = out / 'objects/pmap.o'; command[command.index('-o') + 1] = str(obj)
    for flag in ('-dependency-file', '-serialize-diagnostic-file'):
        if flag in command: command[command.index(flag) + 1] = str(out / (flag[1:] + '.out'))
    virtual_root = '/Users/ciscohe/Desktop/UniXNU_elden/'
    directory = job['directory']
    if not directory.startswith(virtual_root+'research/'):
        raise ValueError('Unexpected captured compile directory')
    relative = 'records/' + directory[len(virtual_root+'research/'):]
    cwd = (a.core/'build/project'/relative).resolve(strict=True)
    with (out / 'compile.log').open('x') as log:
        subprocess.run(command, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, check=True)
    old_symbols, new_symbols = global_symbols(Path(prior['output'])), global_symbols(obj)
    def bindings(symbols):
        return {n: (v[0], v[1], v[3]) for n, v in symbols.items()}
    if bindings(old_symbols) != bindings(new_symbols): raise ValueError('Pmap binding set changed')
    assembly = subprocess.check_output([str(a.llvm/'llvm-objdump'), '-d', '--reloc', str(obj)], text=True)
    (out/'pmap.asm').write_text(assembly)
    def vector_ops(asm):
        result = {}; current = ''
        for line in asm.splitlines():
            label = re.match(r'^[0-9a-f]+\s+<(.+)>:', line)
            if label: current = label[1]
            op = re.match(r'^\s*[0-9a-f]+:\s+([0-9a-f]{8})\s+(.+)$', line)
            if op and re.search(r'\b[qvdshb]\d+(?:\b|\.)', op[2]):
                result.setdefault(current, []).append(op.groups())
        return result
    old_asm = subprocess.check_output([str(a.llvm/'llvm-objdump'), '-d', '--reloc', prior['output']], text=True)
    old_vectors, new_vectors = vector_ops(old_asm), vector_ops(assembly)
    if old_vectors != new_vectors or '_wimg_to_pte' in new_vectors:
        raise ValueError('New/changed vector instructions in pmap or RT helper')
    if not re.search(r'<_wimg_to_pte>:', assembly): raise ValueError('Missing RT helper code')
    write(out/'vector-audit.json', {'existing_vectors_unchanged': True,
        'existing_vector_count': sum(map(len, new_vectors.values())), 'rt_helper_scalar': True})
    filelist = (base/'link.filelist').read_text().splitlines()
    if filelist.count(prior['output']) != 1: raise ValueError('Pmap not unique in baseline file list')
    other_inputs = {}
    for path in filelist:
        if not Path(path).is_file(): raise ValueError('Missing linked object')
        other_inputs[path] = sha(Path(path))
    updated = [str(obj) if s == prior['output'] else s for s in filelist]
    (out/'link.filelist').write_text('\n'.join(updated)+'\n')
    link = read(base/'link-command.json'); before = list(link)
    if link.count(str(base/'native.macho')) != 1: raise ValueError('Ambiguous link output')
    link = [s.replace(str(base/'link.filelist'), str(out/'link.filelist')).replace(str(base/'native.macho'), str(out/'native.macho')) for s in link]
    archive_inputs = {}
    for s in before:
        for path in re.findall(r'/[^,]+\.a', s):
            if Path(path).is_file(): archive_inputs[path] = sha(Path(path))
    write(out/'link-command.json', link)
    env = dict(os.environ, CCC_OVERRIDE_OPTIONS='+-Wno-unknown-warning-option'); env.pop('SDKROOT', None)
    with (out/'link.log').open('x') as log:
        subprocess.run(link, cwd=out, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    for path, digest in {**other_inputs, **archive_inputs}.items():
        if sha(Path(path)) != digest: raise ValueError('Inherited input changed during link')
    def exports(p): return {n for n, v in global_symbols(p).items() if v[0] & 0x0e}
    if exports(out/'native.macho') != exports(base/'native.macho'): raise ValueError('Native exports changed')
    pre = read(ROOT/'profiles/v91/image-build/plan.json')['prelink']
    if sha(a.original_kernelcache) != pre['original_kernelcache_sha256']: raise ValueError('Wrong Apple input')
    kit=out/'prelink-tool'; kit.mkdir()
    raw=subprocess.check_output(['git','-C',str(a.prelink_repository),'archive',pre['commit']])
    with tarfile.open(fileobj=io.BytesIO(raw)) as t: t.extractall(kit,filter='data')
    cmd=[sys.executable,str(kit/'emit_prelink.py'),'--kernel',str(out/'native.macho'),'--prelinked',str(a.original_kernelcache),'--slide='+pre['slide'],'--output',str(out/'kernel.macho')]
    for n in pre['roots']: cmd+=['--root',n]
    for n in pre['early_security_initializers']: cmd+=['--early-security-init',n]
    with (out/'prelink.log').open('x') as log: subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
    verify_original_executables(out/'kernel.macho',read(out/'kernel.json'),pre['original_executable_sha256'])
    result={'profile':'v106-rt-memory','profile_sha256':sha(PROFILE),'kernel_sha256':sha(out/'kernel.macho'),'native_sha256':sha(out/'native.macho'),
        'baseline_kernel_sha256':plan['baseline_kernel_sha256'],'recompiled_core_objects':1,'changed_job':index,'source_sha256':sha(newsource),
        'pmap_object_sha256':sha(obj),'pmap_bindings_preserved':True,'existing_pmap_vectors_unchanged':True,'rt_helper_scalar':True,'unchanged_core_inputs':other_inputs,
        'unchanged_archive_inputs':archive_inputs,'original_apple_executables_hashed':30,'hardware_tested':False}
    write(out/'result.json',result); print(json.dumps({k:v for k,v in result.items() if not isinstance(v,dict)},indent=2))


if __name__ == '__main__': main()
