"""Exercise actual Elden OPP/HFI code with synthetic rails, then compile iOS17.

With --linux, additionally verifies the generated OPP inputs match the pinned
TB322FC Linux DTS 1-to-1 (14 GPU OPPs, 3 GMU OPPs, 4 speed-bin fuses, register
apertures, IRQ and SMMU stream bindings) and exercises negative/boundary
rejections of the parity validators. Data-only evidence; no live policy."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, sys
from replay_xnu_objects import global_symbols, sections
from build_gpu_offline import audit_native
ROOT=Path(__file__).resolve().parents[1]
PIN='6f97250ead155fc3b980856323e99a4623dae73d'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def scope(text,start):
 at=text.index('{',start)+1;begin=at;depth=1
 while depth and at<len(text):
  depth+=(text[at]=='{')-(text[at]=='}');at+=1
 if depth:raise ValueError('Unterminated fixed source block')
 return text[begin:at-1]

def parse_dts_opps(dt,label,constants):
 block=scope(dt,dt.index(label+':'));rows=[]
 for m in re.finditer(r'\bopp-(\d+)\s*\{([^{}]*)\}',block):
  text=m[2];hz=int(re.search(r'opp-hz\s*=\s*/bits/\s*64\s*<(\d+)>',text)[1])
  level=re.search(r'opp-level\s*=\s*<(\w+)>',text)[1]
  def integer(name,default=0):
   v=re.search(re.escape(name)+r'\s*=\s*<(0x[0-9a-f]+|\d+)>',text);return int(v[1],0) if v else default
  rows.append({'hz':hz,'level':constants[level],'bandwidth_kbps':integer('opp-peak-kBps'),
               'supported_mask':integer('opp-supported-hw',15),'acd':integer('qcom,opp-acd-level')})
 rows.sort(key=lambda r:r['hz'])
 return rows

def node_block(text,label):
 m=re.search(r'(?m)^\s*'+re.escape(label)+r':\s*[^\n{]+\{',text)
 if not m:raise ValueError('DTS label missing: '+label)
 start=m.end();depth=1;at=start
 while depth and at<len(text):
  depth+=(text[at]=='{')-(text[at]=='}');at+=1
 return text[m.start():at]

def reg_tuples(block):
 m=re.search(r'(?m)^\s*reg\s*=\s*([^;]+);',block,re.S)
 if not m:return []
 groups=re.findall(r'<([^<>]+)>',m[1])
 return [(int(g.split()[1],0),int(g.split()[3],0)) for g in groups]

def validate_apertures(rows):
 """Reject malformed aperture tables: zero size, misalignment, partial overlap, wrong count.
 Sub-window containment is legitimate hardware layout (cx_dbgc sits inside the main
 aperture's span), but a partial overlap is never valid."""
 if len(rows)!=3:return False
 if any(size==0 or base==0 or base%0x1000 for base,size in rows):return False
 spans=sorted(rows)
 for i,(b1,s1) in enumerate(spans):
  for b2,s2 in spans[i+1:]:
   if b1+s1<=b2 or b2+s2<=b1:continue
   contained=(b1<=b2 and b2+s2<=b1+s1)or(b2<=b1 and b1+s1<=b2+s2)
   if not contained:return False
 return True

def validate_speed_bins(bins):
 """Exactly the four known fuse/index pairs, indexes a permutation of 0..3."""
 if len(bins)!=4:return False
 if {b['fuse'] for b in bins}!={190,221,232,242}:return False
 return sorted(b['index'] for b in bins)==[0,1,2,3]

def validate_opp_table(rows,count):
 """Strict structural contract: exact count, strictly ascending unique hz, mask bits within the 4-bin space."""
 if len(rows)!=count:return False
 if len({r['hz'] for r in rows})!=len(rows):return False
 if rows!=sorted(rows,key=lambda r:r['hz']):return False
 if any(not (r['supported_mask']&15) or r['supported_mask']&~15 for r in rows):return False
 if any(r['hz']<=0 or r['hz']>1100000000 for r in rows):return False
 return True

def dts_parity(linux,generated,out):
 inputs=json.loads((generated/'inputs.json').read_text())
 names={'arch/arm64/boot/dts/qcom/sm8750.dtsi':None,'include/dt-bindings/power/qcom,rpmhpd.h':None}
 blobs={}
 if (linux/'.git').exists():
  for name in names:
   blobs[name]=subprocess.check_output(['git','-C',str(linux),'show',PIN+':'+name])
 else:
  # Plain-directory export of the pinned files; accept only bytes whose sha256 matches
  # the derivation's recorded source hashes, so the hash binding is preserved.
  recorded=inputs.get('source_sha256') or {}
  for name in names:
   data=(linux/name).read_bytes()
   if recorded.get(name) and hashlib.sha256(data).hexdigest()!=recorded[name]:
    raise ValueError('Exported DTS file differs from the derivation pin hash: '+name)
   blobs[name]=data
 dt=blobs['arch/arm64/boot/dts/qcom/sm8750.dtsi'].decode()
 levels=blobs['include/dt-bindings/power/qcom,rpmhpd.h'].decode()
 constants={name:int(value) for name,value in re.findall(r'^#define\s+(RPMH_REGULATOR_LEVEL_\w+)\s+(\d+)\s*$',levels,re.M)}
 gpu=parse_dts_opps(dt,'gpu_opp_table',constants);gmu=parse_dts_opps(dt,'gmu_opp_table',constants)
 checks={}
 checks['gpu_opp_count']=len(gpu)==14
 checks['gmu_opp_count']=len(gmu)==3
 checks['gpu_rows_match_dts']=inputs['gpu']==gpu
 checks['gmu_rows_match_dts']=inputs['gmu']==gmu
 checks['speed_bins_exact']=sorted((b['fuse'],b['index']) for b in inputs['speed_bins'])==[(190,0),(221,1),(232,2),(242,3)]
 # Register apertures / IRQ / SMMU bindings as written in the pinned DTS.
 gpun=node_block(dt,'gpu');gmun=node_block(dt,'gmu');smmun=node_block(dt,'adreno_smmu');gpuccn=node_block(dt,'gpucc')
 apertures={
  'gpu':reg_tuples(gpun),'gmu':reg_tuples(gmun),'adreno_smmu':reg_tuples(smmun),'gpucc':reg_tuples(gpuccn)}
 expected_gpu=[(0x03d00000,0x6c000),(0x03d9e000,0x2000),(0x03d61000,0x800)]
 checks['gpu_apertures_exact']=apertures['gpu']==expected_gpu
 checks['gmu_aperture_exact']=apertures['gmu']==[(0x03d37000,0x68000)]
 checks['smmu_aperture_exact']=apertures['adreno_smmu']==[(0x03da0000,0x40000)]
 checks['gpucc_aperture_exact']=apertures['gpucc']==[(0x03d90000,0x9800)]
 checks['gpu_apertures_wellformed']=validate_apertures(apertures['gpu'])
 gpu_irq=re.search(r'interrupts\s*=\s*<GIC_SPI\s+(\d+)',gpun)
 checks['gpu_irq_300']=bool(gpu_irq)and gpu_irq[1]=='300'
 gmu_irqs=re.findall(r'GIC_SPI\s+(\d+)',re.search(r'interrupts\s*=\s*([^;]+);',gmun,re.S)[1])
 checks['gmu_irqs_304_305']=gmu_irqs==['304','305']
 streams=re.findall(r'<&adreno_smmu\s+(\d+)\s+0x0>',gpun)
 checks['gpu_smmu_streams_0_1']=streams==['0','1']
 gstreams=re.findall(r'<&adreno_smmu\s+(\d+)\s+0x0>',gmun)
 checks['gmu_smmu_stream_5']=gstreams==['5']
 # Negative/boundary cases: the validators must reject every mutated input.
 negative={}
 negative['unknown_speed_bin_fuse']=not validate_speed_bins([{'fuse':252,'index':0}]+[b for b in inputs['speed_bins'][1:]])
 negative['missing_speed_bin']=not validate_speed_bins(inputs['speed_bins'][:3])
 negative['duplicate_opp_hz']=not validate_opp_table(gpu[:1]+gpu[:1]+gpu[2:],14)
 negative['opp_count_wrong']=not validate_opp_table(gpu[:13],14)
 negative['opp_mask_out_of_range']=not validate_opp_table([dict(r,supported_mask=0x10) for r in gpu],14)
 negative['opp_mask_zero_bins']=not validate_opp_table([dict(r,supported_mask=0) for r in gpu],14)
 negative['opp_hz_out_of_range']=not validate_opp_table([dict(gpu[0],hz=1200000000)]+gpu[1:],14)
 negative['overlapping_aperture']=not validate_apertures([(0x03d00000,0x6c000),(0x03d05000,0x6c000),(0x03d61000,0x800)])
 negative['zero_size_aperture']=not validate_apertures([(0x03d00000,0),(0x03d9e000,0x2000),(0x03d61000,0x800)])
 negative['misaligned_aperture']=not validate_apertures([(0x03d00010,0x6c000),(0x03d9e000,0x2000),(0x03d61000,0x800)])
 negative['aperture_count_wrong']=not validate_apertures(apertures['gpu'][:2])
 negative['nonpermutation_bin_indexes']=not validate_speed_bins([{'fuse':190,'index':0},{'fuse':221,'index':0},{'fuse':232,'index':2},{'fuse':242,'index':3}])
 positive={'validate_speed_bins_accepts_exact':validate_speed_bins(inputs['speed_bins']),
           'validate_opp_table_accepts_dts':validate_opp_table(gpu,14)and validate_opp_table(gmu,3),
           'validate_apertures_accepts_dts':validate_apertures(apertures['gpu'])}
 passed=all(checks.values())and all(negative.values())and all(positive.values())
 return {'passed':passed,'checks':checks,'negative':negative,'positive':positive,
         'apertures':{k:[['0x%x'%b,'0x%x'%s] for b,s in v] for k,v in apertures.items()},
         'gpu_opp_rows':len(gpu),'gmu_opp_rows':len(gmu),'live_policy_selected':False,'hardware_qualified':False}

def main():
 p=argparse.ArgumentParser(description=__doc__)
 for n in ('tree','generated','output'):p.add_argument('--'+n,type=Path,required=True)
 p.add_argument('--linux',type=Path,help='TB322FC kernel clone; enables the DTS parity and boundary phase')
 p.add_argument('--driver',type=Path,default=ROOT/'drivers/EldenGpu');p.add_argument('--cc',default='clang')
 p.add_argument('--objdump',default='llvm-objdump');a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 fw=a.tree/'kits/AdrenoFirmware';soc=a.tree/'kits/Sm8750Platform';driver=a.driver
 includes=[v for q in (driver,a.generated,fw,soc) for v in ('-I',str(q))]
 sources=[driver/'EldenGpuVotes.c',driver/'TestVotes.c',fw/'AdrenoHfiVotes.c',fw/'AdrenoHfiStartup.c',fw/'AdrenoHfi.c']
 sdk_flags=[]
 if sys.platform=='darwin':sdk_flags=['-isysroot',subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()]
 host=out/'votes';cmd=[a.cc,*sdk_flags,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-DADRENO_HFI_HOST_TEST',
                     '-fsanitize=address,undefined',*includes,*map(str,sources),'-o',str(host)]
 def run(c,n):
  r=subprocess.run(list(map(str,c)),capture_output=True);(out/n).write_bytes(r.stdout+r.stderr);r.check_returncode();return r.stdout
 run(cmd,'host-build.log');output=run([host],'host-run.log').decode()
 if 'PASS: 50 Elden OPP/HFI cases' not in output:raise ValueError('Expected actual test result absent')
 resource=subprocess.check_output([a.cc,'-print-resource-dir'],text=True).strip();obj=out/'EldenGpuVotes.o'
 ios_sysroot=[]
 if sys.platform=='darwin':
  ios_sysroot=['-isysroot',subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'],text=True).strip()]
 native=[a.cc,'-target','arm64-apple-ios17.0','-std=c11','-O2','-ffreestanding','-fno-builtin','-fno-stack-protector','-mgeneral-regs-only',
         *ios_sysroot,'-nostdinc','-isystem',str(Path(resource)/'include'),'-Wall','-Wextra','-Werror',*includes,'-c',driver/'EldenGpuVotes.c','-o',obj]
 run(native,'native-build.log');audit_native(obj.read_bytes());s=global_symbols(obj)
 exports=[k for k,v in s.items() if v[0]&0xe];imports=[k for k,v in s.items() if not v[0]&0xe]
 if exports!=['_elden_gpu_build_startup']:raise ValueError('Unexpected exports')
 asm=run([a.objdump,'-d','--no-show-raw-insn',obj],'native.asm').decode()
 if re.search(r'^\s*[0-9a-f]+:.*\b[qvdshb]\d+(?:\b|\.)',asm,re.M):raise ValueError('Unexpected SIMD')
 if any(k.endswith('/__mod_init_func') for k in sections(obj)):raise ValueError('Unexpected initializer')
 report={'passed':True,'host_result':output.strip(),'object_sha256':sha(obj),'exports':exports,'imports':imports,
         'source_sha256':{str(x):sha(x) for x in sources+[a.generated/'EldenGpuOpps.h']},'host_command':list(map(str,cmd)),
         'native_command':list(map(str,native)),'hardware':False,'live_voltage_or_frequency_selected':False}
 if a.linux:
  parity=dts_parity(a.linux.resolve(),a.generated.resolve(),out)
  report['dts_parity']=parity
  (out/'dts-parity.json').write_text(json.dumps(parity,indent=2)+'\n')
  if not parity['passed']:raise ValueError('DTS parity/boundary phase failed; see dts-parity.json')
 (out/'result.json').write_text(json.dumps(report,indent=2)+'\n');print(output,end='')
if __name__=='__main__':main()
