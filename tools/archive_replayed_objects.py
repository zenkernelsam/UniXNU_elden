#!/usr/bin/env python3
"""Archive freshly rebuilt objects using a declared member/symbol layout."""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess,sys

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--capture',type=Path,required=True);p.add_argument('--results',type=Path,required=True)
    p.add_argument('--layout',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    if sys.platform!='darwin':raise SystemExit('This archive profile uses macOS ar/nm')
    root=a.results.resolve(strict=True);out=a.output.resolve();capture=json.loads(a.capture.read_text());results=json.loads((root/'results.json').read_text())
    if (root/'capture.sha256').read_text().strip()!=sha(a.capture):raise RuntimeError('Results belong to a different source capture')
    layout=json.loads(a.layout.read_text());name=layout['archive']
    if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.+-]*\.a',name):raise ValueError('Invalid archive filename')
    if out.exists():raise RuntimeError('Output must be new')
    inputs=[]
    for i in layout['jobs']:
        job=capture['jobs'][i];result=results[str(i)];source=Path(result['output']).resolve(strict=True)
        if not source.is_relative_to(root/'objects'):raise RuntimeError('Object is outside the new build')
        if not result.get('accepted') or not result.get('loaded_sections_equal') or not result.get('global_symbols_equal') or sha(source)!=result['sha256']:raise RuntimeError('Unqualified object')
        member=Path(job['original_object']).name
        if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.+-]*',member):raise ValueError('Invalid member filename')
        inputs.append((source,member))
    if len({n for _,n in inputs})!=len(inputs):raise ValueError('Duplicate archive members')
    out.mkdir(parents=True)
    for source,member in inputs:shutil.copyfile(source,out/member)
    archive=out/name
    subprocess.run(['/usr/bin/ar','rcs',str(archive),*[n for _,n in inputs]],cwd=out,env=dict(os.environ,ZERO_AR_DATE='1'),check=True)
    def symbols(flag):
        lines=subprocess.check_output(['xcrun','nm','-g',flag,'-j',str(archive)],text=True).splitlines()
        return sorted(set(s.strip() for s in lines if s.startswith('_')))
    if symbols('-U')!=layout['exports'] or symbols('-u')!=layout['imports']:raise RuntimeError('Archive symbol sets differ from the frozen layout')
    result={'archive':name,'sha256':sha(archive),'members':len(inputs),'copied_original_objects':0,
            'loadable_object_sections_equal':True,'global_and_absolute_object_bindings_equal':True,'public_symbol_sets_equal':True,'native_linked':False,'device_operation':False}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':main()
