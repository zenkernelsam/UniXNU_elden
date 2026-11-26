"""Host-verify the opt-007 ArmadaOS Mesa/Turnip A830 folds; no device IO.

Verifies, against the pinned Mesa tree and the exported patch set:
 1. The three folded patches (A830 chip IDs, bindless-UBO const gating, trivial
    PHI chain resolution) strict-apply with zero fuzz on the pinned Mesa source,
    and no upstream patch in the stack touches the same files (so application
    after the upstream stack is equivalent to the per-file application here).
 2. freedreno_devices.py registers all four A830 chip IDs under CHIP.A8XX with
    a8xx_base + a8xx_gen1.
 3. ASan/UBSan host C unit tests of the exact patched logic, extracted verbatim
    from the patched sources at test time: lookup_value() trivial PHI chain
    resolution (single PHI, multi-hop chain, self-cycle termination, NULL) and
    get_ubo_info()'s bindless const-offset gate (const accepted, dynamic
    rejected - with the unpatched form shown to accept dynamic, as the
    regression contrast).
Host/model evidence only.
"""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, sys

ROOT=Path(__file__).resolve().parents[1]
ADDED=['patches/mesa/freedreno-a830-chip-id-variants.patch',
       'patches/mesa/ir3-bindless-ubo-const-gating.patch',
       'patches/mesa/ir3-array-to-ssa-phi-chains.patch']
TARGETS={'patches/mesa/freedreno-a830-chip-id-variants.patch':'src/freedreno/common/freedreno_devices.py',
         'patches/mesa/ir3-bindless-ubo-const-gating.patch':'src/freedreno/ir3/ir3_nir_analyze_ubo_ranges.c',
         'patches/mesa/ir3-array-to-ssa-phi-chains.patch':'src/freedreno/ir3/ir3_array_to_ssa.c'}
CHIP_IDS=['0xffff44050000','0xffff44050001','0x44050000','0x44050001']

def run(cmd,log,cwd=None):
    proc=subprocess.run(list(map(str,cmd)),capture_output=True,cwd=cwd)
    Path(log).write_bytes(proc.stdout+proc.stderr)
    return proc.returncode

def extract_function(text,name):
    scan=0;at=-1
    while True:
        cand=text.index(name,scan)
        window=text[cand:cand+120]
        if '{' in window and (';' not in window[:window.index('{')]):
            at=cand;break
        scan=cand+1
    blank=text.rindex('\n\n',0,at)
    start=blank+2
    depth=0;began=False
    for i in range(at,len(text)):
        c=text[i]
        if c=='{':depth+=1;began=True
        elif c=='}':
            depth-=1
            if began and depth==0:return text[start:i+1]
    raise ValueError('unterminated function: '+name)

def extract_condition(text):
    at=text.index('ir3_bindless_resource(instr->src[0])')
    line_start=text.index('if',at)
    cond_start=text.index('(',line_start)
    depth=0
    for i in range(cond_start,len(text)):
        if text[i]=='(':depth+=1
        elif text[i]==')':
            depth-=1
            if depth==0:return text[cond_start+1:i]
    raise ValueError('condition not found')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('tree','mesa','output'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args()
    tree=a.tree.resolve();mesa=a.mesa.resolve();out=a.output.resolve()
    out.mkdir(parents=True,exist_ok=False)
    result={'phases':{},'device_access':False}
    pins=json.loads((tree/'scripts/data/adreno-compiler-inputs.json').read_text())

    # Phase 1a: the three additions are the tail of the recorded patch series, in order.
    tail=pins['patches'][-3:]
    ok=tail==ADDED
    result['phases']['series_tail']={'pass':ok,'tail':tail}
    if not ok:raise SystemExit('patch series tail differs: '+json.dumps(tail))
    patch_bytes={n:(tree/n).read_bytes() for n in ADDED}
    result['patch_sha256']={n:hashlib.sha256(b).hexdigest() for n,b in patch_bytes.items()}

    # Phase 1b: no earlier stack patch touches any of the three target files,
    # so per-file application on pristine source equals application after the stack.
    overlap=[]
    for n in pins['patches'][:-3]:
        text=(tree/n).read_text(errors='replace')
        for target in TARGETS.values():
            if target in text:overlap.append((n,target))
    result['phases']['overlap_free']={'pass':not overlap,'overlap':overlap}
    if overlap:raise SystemExit('upstream stack overlaps patch targets: '+json.dumps(overlap))

    # Phase 1c: strict zero-fuzz apply on the pristine pinned sources.
    scratch=out/'apply-scratch';scratch.mkdir()
    run(['git','init','-q',scratch],out/'git-init.log')
    patched={}
    for name,target in TARGETS.items():
        head=subprocess.run(['git','-C',str(mesa),'show','HEAD:'+target],capture_output=True)
        if head.returncode:raise SystemExit('pinned Mesa source missing: '+target)
        dest=scratch/target;dest.parent.mkdir(parents=True,exist_ok=True)
        dest.write_bytes(head.stdout)
        base=hashlib.sha256(head.stdout).hexdigest()
        rc=run(['git','apply','--check',tree/name],out/(Path(name).stem+'-check.log'),cwd=scratch)
        if rc:raise SystemExit('strict apply check failed: '+name)
        rc=run(['git','apply',tree/name],out/(Path(name).stem+'-apply.log'),cwd=scratch)
        if rc:raise SystemExit('strict apply failed: '+name)
        data=dest.read_bytes()
        patched[target]=data.decode()
        result['phases']['strict_apply_'+Path(name).stem]={'pass':True,'base_sha256':base,
            'patched_sha256':hashlib.sha256(data).hexdigest(),'changed':data!=head.stdout}

    # Phase 2: A830 chip-ID registration in the patched generator.
    devices=patched['src/freedreno/common/freedreno_devices.py']
    block_at=devices.index('0xffff44050000')
    add_start=devices.rindex('add_gpus',0,block_at)
    info_end=devices.index('num_ccu',block_at)
    block=devices[add_start:info_end]
    ids=sorted(set(re.findall(r'chip_id=(0x[0-9a-fA-F]+)',block)))
    reg={'pass':ids==sorted(CHIP_IDS) and 'CHIP.A8XX' in block and 'a8xx_base' in block and 'a8xx_gen1' in block,
         'chip_ids':ids,'a8xx':'CHIP.A8XX' in block,'a8xx_base':'a8xx_base' in block,'a8xx_gen1':'a8xx_gen1' in block}
    result['phases']['chip_id_registration']=reg
    if not reg['pass']:raise SystemExit('chip-ID registration incomplete: '+json.dumps(reg))

    # Phase 3a: lookup_value behavioral unit test, exact body extracted from the patched source.
    ssa=patched['src/freedreno/ir3/ir3_array_to_ssa.c']
    body=extract_function(ssa,'lookup_value')
    harness='#include <stdio.h>\n#include <stdlib.h>\n' \
        'struct ir3_register;\n' \
        'struct ir3_instruction { int opc; struct ir3_register *data; };\n' \
        'struct ir3_register { struct ir3_instruction *instr; };\n' \
        '#define OPC_META_PHI 1\n' + body + '''
static struct ir3_register *mkreg(struct ir3_instruction *instr){
   struct ir3_register *r=calloc(1,sizeof *r);r->instr=instr;return r;}
int main(void){
   /* NULL input */
   if(lookup_value(NULL)!=NULL)return 1;
   /* non-phi register passes through */
   struct ir3_instruction *real=calloc(1,sizeof *real);real->opc=2;real->data=NULL;
   struct ir3_register *rdst=mkreg(real);
   if(lookup_value(rdst)!=rdst)return 2;
   /* single trivial phi */
   struct ir3_instruction *phi1=calloc(1,sizeof *phi1);phi1->opc=OPC_META_PHI;phi1->data=rdst;
   struct ir3_register *r1=mkreg(phi1);
   if(lookup_value(r1)!=rdst)return 3;
   /* multi-hop chain: phi2 -> phi1 -> real */
   struct ir3_instruction *phi2=calloc(1,sizeof *phi2);phi2->opc=OPC_META_PHI;phi2->data=r1;
   struct ir3_register *r2=mkreg(phi2);
   if(lookup_value(r2)!=rdst)return 4;
   /* three-hop chain */
   struct ir3_instruction *phi3=calloc(1,sizeof *phi3);phi3->opc=OPC_META_PHI;phi3->data=r2;
   struct ir3_register *r3=mkreg(phi3);
   if(lookup_value(r3)!=rdst)return 5;
   /* direct self-cycle terminates and returns the phi itself */
   struct ir3_instruction *selfp=calloc(1,sizeof *selfp);selfp->opc=OPC_META_PHI;
   struct ir3_register *rs=mkreg(selfp);selfp->data=rs;
   if(lookup_value(rs)!=rs)return 6;
   puts("lookup_value: all cases pass");
   return 0;
}
'''
    (out/'test_lookup_value.c').write_text(harness)
    rc=run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
            out/'test_lookup_value.c','-o',out/'test_lookup_value'],out/'lookup-compile.log')
    if rc:raise SystemExit('lookup_value harness compile failed; see lookup-compile.log')
    rc=run([out/'test_lookup_value'],out/'lookup-run.log')
    result['phases']['lookup_value_unit']={'pass':rc==0,'run_rc':rc,
        'cases':['null','non_phi_passthrough','single_phi','two_hop','three_hop','self_cycle']}
    if rc:raise SystemExit('lookup_value harness failed rc='+str(rc))

    # Phase 3b: get_ubo_info bindless const-offset gate - exact condition extracted from both
    # the pristine (regression contrast) and patched sources, compiled and truth-tabled.
    pristine=subprocess.run(['git','-C',str(mesa),'show','HEAD:src/freedreno/ir3/ir3_nir_analyze_ubo_ranges.c'],
                            capture_output=True,text=True).stdout
    cond_new=extract_condition(patched['src/freedreno/ir3/ir3_nir_analyze_ubo_ranges.c'])
    cond_old=extract_condition(pristine)
    result['conditions']={'pristine':cond_old,'patched':cond_new}
    harness2='#include <stdlib.h>\n#include <stdio.h>\n' \
        'typedef struct { unsigned const_tag; } nir_src;\n' \
        'typedef struct nir_intrinsic_instr { nir_src src[4]; } nir_intrinsic_instr;\n' \
        'static int nir_src_is_const(nir_src s){return s.const_tag!=0;}\n' \
        'static int gate_new(nir_intrinsic_instr *instr,nir_intrinsic_instr *rsrc){\n' \
        '   (void)instr;(void)rsrc;return (' + '('+cond_new+')' + ')?1:0;}\n' \
        'static int gate_old(nir_intrinsic_instr *instr,nir_intrinsic_instr *rsrc){\n' \
        '   (void)instr;(void)rsrc;return (' + '('+cond_old+')' + ')?1:0;}\n' \
        'static nir_intrinsic_instr *mk(unsigned c0,unsigned c1){\n' \
        '   nir_intrinsic_instr *x=calloc(1,sizeof *x);x->src[0].const_tag=c0;x->src[1].const_tag=c1;return x;}\n' \
        'int main(void){\n' \
        '   nir_intrinsic_instr *instr_dyn=mk(0,0),*rsrc_c=mk(1,0);\n' \
        '   if(gate_new(instr_dyn,rsrc_c)!=0)return 11;\n' \
        '   nir_intrinsic_instr *instr_con=mk(0,1);\n' \
        '   if(gate_new(instr_con,rsrc_c)!=1)return 12;\n' \
        '   if(gate_new(instr_con,NULL)!=0)return 13;\n' \
        '   nir_intrinsic_instr *rsrc_nc=mk(0,0);\n' \
        '   if(gate_new(instr_con,rsrc_nc)!=0)return 14;\n' \
        '   if(gate_old(instr_dyn,rsrc_c)!=1)return 15;\n' \
        '   puts("get_ubo_info gate: all cases pass");\n' \
        '   return 0;\n' \
        '}\n'
    (out/'test_ubo_gate.c').write_text(harness2)
    rc=run(['xcrun','clang','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
            out/'test_ubo_gate.c','-o',out/'test_ubo_gate'],out/'ubo-compile.log')
    if rc:raise SystemExit('ubo gate harness compile failed; see ubo-compile.log')
    rc=run([out/'test_ubo_gate'],out/'ubo-run.log')
    result['phases']['ubo_gate_unit']={'pass':rc==0,'run_rc':rc,
        'cases':['dynamic_offset_rejected','const_offset_accepted','null_rsrc_rejected',
                 'nonconst_base_rejected','unpatched_accepts_dynamic_contrast']}
    if rc:raise SystemExit('ubo gate harness failed rc='+str(rc))

    result['pass']=True
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: turnip patch verification; host/model evidence only')
if __name__=='__main__':main()
