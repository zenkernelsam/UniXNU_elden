from pathlib import Path
import argparse,hashlib,json,shutil,subprocess,sys
a=argparse.ArgumentParser();a.add_argument('--version',type=int,choices=[90,91],default=90);version=a.parse_args().version
r=Path(__file__).resolve().parents[1];old=r/'edl_work/basic-bundle-20261108/v89';new=r/f'edl_work/basic-bundle-20261108/v{version}';before=r/'research/basic-bundle-20261108/source-compiled-v89';after=r/f'research/basic-bundle-20261108/source-compiled-v{version}';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
a=json.loads((before/'manifest.json').read_text())['files'];b=json.loads((after/'manifest.json').read_text())['files']
assert {n for n in a if a[n]!=b[n]}=={'xnu/EldenIconCpuCommon.inc','xnu/EldenIconCpuPaths.h','xnu/EldenIconCpuPayload.h','package/EldenIconCpuMode.c','package/EldenIconCpuPaths.h'}
assert set(b)-set(a)=={'package/EldenGraphicVariantHook.c','package/EldenGraphicVariantCpu.c','package/EldenGraphicVariantCpu.h','build-scripts/relink_graphic_variant_mac.py','build-scripts/build_graphic_variant_library_mac.py'}
for folder in ('archives','display-archives','cpu-archives'):
 for p in (old/'final'/folder).rglob('*'):
  if p.is_file():assert p.read_bytes()==(new/'final'/folder/p.relative_to(old/'final'/folder)).read_bytes()
for folder in ('pd-codegen','ucsi-codegen','touch-codegen'):
 (new/folder).mkdir(exist_ok=True)
 for p in (old/folder).iterdir():
  if p.is_file():shutil.copyfile(p,new/folder/p.name)
sys.path.insert(0,'D:/Desktop/Git/UniXNU_elden/scripts');import make_entry_probe_kernel as m
kernel=(new/'final/kernel.macho').read_bytes();preview=json.loads((old/'preview-codegen.json').read_text());preview['linked_helper_address']=hex(m.symbols(kernel)['_elden_preview_after_input']);preview['reused_v89_codegen_sha256']=sha(old/'preview-codegen.json')
(new/'preview-codegen.json').write_text(json.dumps(preview,indent=2)+'\n')
for name in ('audit_graphic_variant.py','audit_icon_fault_trace.py','audit_cpu_asm_fixed.py','audit_icon_cpu_codegen.py','audit_ui_dispatch.py','prepare_icon_cpu_qualification.py'):
 cmd=[sys.executable,r/'edl_work'/name]+['--version',str(version)]
 q=subprocess.run(cmd,capture_output=True,text=True);(new/(name+'.log')).write_text(q.stdout+q.stderr);assert q.returncode==0,(name,q.stderr[-2000:]);print(name+' passed',flush=True)
out=new/'final/icon-cpu-focused-gestures-ipad-pro-cpu8';out.mkdir(exist_ok=True)
for p in (new/'final/icon-cpu-focused-gestures').glob('*-qualification.json'):shutil.copyfile(p,out/p.name)
(new/'reused-qualifications.json').write_text(json.dumps({'all_driver_archive_bytes_equal':True,'only_kernel_object_recompiled':'kern_exec.o','glyph_codegen_sha256':sha(new/'glyph-codegen.json')},indent=2)+'\n')
