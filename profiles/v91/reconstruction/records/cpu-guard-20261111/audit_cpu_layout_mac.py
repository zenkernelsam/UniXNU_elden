"""Extract compiler-derived CPU/dispatch layouts from the new build headers."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
parser=argparse.ArgumentParser();parser.add_argument('--guard',action='store_true');options=parser.parse_args()
r=Path('/Users/ciscohe/Desktop/UniXNU_elden');assert Path.cwd()==r
p=r/('research/cpu-guard-20261111' if options.guard else 'research/cpu-full-20261111');t=p/'tree';o=p/'audit';o.mkdir(exist_ok=True)
src=o/'layout.c';src.write_text('''#include <pexpert/arm64/board_config.h>
#include <arm/cpu_data_internal.h>
#include <arm/machine_routines.h>
#include <kern/sched.h>
const unsigned long long elden_cpu_layout[] = {
 MAX_CPUS, MAX_CPU_CLUSTERS, sizeof(cpu_data_entry_t),
 sizeof(ml_topology_cpu_t), sizeof(ml_topology_cluster_t),
 __builtin_offsetof(struct sched_dispatch_table, multiple_psets_enabled),
 sizeof(((struct sched_dispatch_table *)0)->multiple_psets_enabled)
};
''')
record=json.loads((t/'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/osfmk/DEVELOPMENT/startup.o.json').read_text().strip().rstrip(','))
cmd=[];args=record['arguments'];i=0
while i<len(args):
    a=args[i]
    if a==record['file']:cmd.append(str(src))
    elif a in ('-o','-MF','-MT','-MQ','-MJ','-serialize-diagnostics'):i+=1
    elif a not in ('-c','-MD','-MMD','-MP'):cmd.append(a)
    i+=1
cmd+=['-S','-o',str(o/'layout.s'),'-mgeneral-regs-only']
with (o/'layout.log').open('w') as log:subprocess.run(cmd,cwd=record['directory'],stdout=log,stderr=subprocess.STDOUT,check=True)
assembly=(o/'layout.s').read_text();start=assembly.index('_elden_cpu_layout:');tail=assembly[start:]
values=[int(x,0) for x in re.findall(r'^\s*\.quad\s+(0x[0-9a-fA-F]+|[0-9]+)',tail,re.M)]
assert len(values)==7,values
names=['max_cpus','max_clusters','cpu_entry_bytes','topology_cpu_bytes','topology_cluster_bytes','clutch_multiple_offset','clutch_multiple_bytes']
report=dict(zip(names,values));assert values[:3]==[8,2,16]
root=t/'.cache/xnu-work'
report['source_headers']={n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in ['pexpert/pexpert/arm64/board_config.h','pexpert/pexpert/arm64/BCM2837.h','osfmk/arm64/machine_routines.c','osfmk/kern/sched_clutch.c']}
(o/'layout.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
