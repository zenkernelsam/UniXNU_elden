"""Derive Elden GPU/GMU OPP facts from the pinned TB322FC kernel, not PKX110."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess

PIN='6f97250ead155fc3b980856323e99a4623dae73d'
PATHS=('arch/arm64/boot/dts/qcom/sm8750.dtsi','include/dt-bindings/power/qcom,rpmhpd.h','drivers/gpu/drm/msm/adreno/a6xx_catalog.c')
def scope(text,start):
 at=text.index('{',start)+1;begin=at;depth=1
 while depth and at<len(text):
  depth+=(text[at]=='{')-(text[at]=='}');at+=1
 if depth:raise ValueError('Unterminated fixed source block')
 return text[begin:at-1]
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--linux',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 raw={name:subprocess.check_output(['git','-C',str(a.linux),'show',PIN+':'+name]) for name in PATHS}
 dt,levels,catalog=(raw[n].decode() for n in PATHS)
 constants={name:int(value) for name,value in re.findall(r'^#define\s+(RPMH_REGULATOR_LEVEL_\w+)\s+(\d+)\s*$',levels,re.M)}
 def opps(label):
  block=scope(dt,dt.index(label+':'));rows=[]
  for m in re.finditer(r'\bopp-(\d+)\s*\{([^{}]*)\}',block):
   text=m[2];hz=int(re.search(r'opp-hz\s*=\s*/bits/\s*64\s*<(\d+)>',text)[1])
   if hz!=int(m[1]):raise ValueError('OPP label/frequency mismatch')
   level=re.search(r'opp-level\s*=\s*<(\w+)>',text)[1]
   def integer(name,default=0):
    v=re.search(re.escape(name)+r'\s*=\s*<(0x[0-9a-f]+|\d+)>',text);return int(v[1],0) if v else default
   rows.append({'hz':hz,'level':constants[level],'bandwidth_kbps':integer('opp-peak-kBps'),
                'supported_mask':integer('opp-supported-hw',15),'acd':integer('qcom,opp-acd-level')})
  rows.sort(key=lambda r:r['hz'])
  if not rows or len({r['hz'] for r in rows})!=len(rows):raise ValueError('Empty/ambiguous OPP table')
  return rows
 gpu,gmu=opps('gpu_opp_table'),opps('gmu_opp_table')
 start=catalog.index('.chip_ids = ADRENO_CHIP_IDS(0x44050000)')
 end=catalog.index('.chip_ids = ADRENO_CHIP_IDS(',start+1);entry=catalog[start:end]
 body=re.search(r'\.speedbins = ADRENO_SPEEDBINS\((.*?)\)',entry,re.S)[1]
 bins=[{'fuse':int(x,0),'index':int(y)} for x,y in re.findall(r'\{\s*(0x[0-9a-f]+|\d+),\s*(\d+)\s*\}',body)]
 if len(gpu)>15 or len(gmu)>3 or {b['index'] for b in bins}!={0,1,2,3}:raise ValueError('Tables exceed HFI capacity or bin contract changed')
 if any(not (r['supported_mask']&15) or r['supported_mask']&~15 for r in gpu):raise ValueError('Unrecognized speed-bin bits')
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
 result={'commit':PIN,'source_sha256':{n:hashlib.sha256(b).hexdigest() for n,b in raw.items()},'gpu':gpu,'gmu':gmu,'speed_bins':bins,
         'description_chip_id':'0x44050000','source_gmu_chip_id':'0x08000100','maximum_slices':3,
         'selected_frequency':None,'hardware_read':False,'note':'Data only; zero/off entry is added by the runtime. This does not choose a safe live frequency or change a vote.'}
 lines=['/* Generated facts from TB322FC Linux '+PIN+'; no selected runtime policy. */','#pragma once','#include <stdint.h>',
        'typedef struct {uint64_t hz;uint32_t level,bandwidth_kbps,supported_mask,acd;} EldenGpuOpp;']
 for name,rows in [('Gpu',gpu),('Gmu',gmu)]:
  lines+=['static const EldenGpuOpp elden'+name+'Opps[] = {']
  lines += ['  {%dULL,%du,%du,%du,0x%xu},'%(r['hz'],r['level'],r['bandwidth_kbps'],r['supported_mask'],r['acd']) for r in rows]
  lines+=['};']
 lines+=['typedef struct {uint32_t fuse,index;} EldenGpuSpeedBin;','static const EldenGpuSpeedBin eldenGpuSpeedBins[] = {']
 lines+=['  {0x%xu,%du},'%(r['fuse'],r['index']) for r in bins];lines+=['};']
 (out/'EldenGpuOpps.h').write_text('\n'.join(lines)+'\n');(out/'inputs.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({'gpu_opps':len(gpu),'gmu_opps':len(gmu),'speed_bins':bins,'live_policy_selected':False}))
if __name__=='__main__':main()
