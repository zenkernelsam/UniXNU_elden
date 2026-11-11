"""Reuse upstream's actual-source topology/Clutch regressions on the Y700 draft.

Only evaluates string-concatenation AST nodes from the upstream test fixtures;
does not run its build orchestration or device commands.
"""
from pathlib import Path
import ast,hashlib,json,re,subprocess,shutil
r=Path(__file__).resolve().parents[1];p=r/'research/cpu-affinity-20261111'
up=r/'research/UniXNU-Pagani_SM8750/scripts/test-format2.py';tree=ast.parse(up.read_text())
expressions=[]
for n in tree.body:
    if isinstance(n,ast.Assign) and any(isinstance(x,ast.Name) and x.id=='source' for x in n.targets):expressions.append(n.value)
def concat(n,env):
    if isinstance(n,ast.Constant) and isinstance(n.value,str):return n.value
    if isinstance(n,ast.Name):return env[n.id]
    if isinstance(n,ast.BinOp) and isinstance(n.op,ast.Add):return concat(n.left,env)+concat(n.right,env)
    raise ValueError('Only literal/name string concatenation is permitted')
def names(n):return {x.id for x in ast.walk(n) if isinstance(x,ast.Name)}
lookup_expr=next(x for x in expressions if names(x)=={'macros','lookup'})
clutch_expr=next(x for x in expressions if names(x)=={'create','enabled','steal'})
for name in ('processor.c','sched_prim.c'):
    shutil.copyfile(p/'baseline/osfmk/kern'/name,p/'candidate/osfmk/kern'/name)
out=p/'host';out.mkdir(exist_ok=True);records=[]
for variant in ('baseline','candidate'):
    x=p/variant;machine=(x/'osfmk/arm64/machine_routines.c').read_text()
    macros='\n'.join(re.findall(r'^#define MPIDR_(?:CPU|CLUSTER)_ID.*$',machine,re.M))
    lookup=re.search(r'int\nml_get_cpu_number\(uint32_t phys_id\)\n\{.*?\n\}',machine,re.S).group()
    topology=concat(lookup_expr,dict(macros=macros,lookup=lookup))
    clutch=(x/'osfmk/kern/sched_clutch.c').read_text()
    dispatch=clutch.split('const struct sched_dispatch_table sched_clutch_dispatch = {',1)[1].split('};',1)[0]
    multiple=re.search(r'\.multiple_psets_enabled\s*=\s*(TRUE|FALSE)',dispatch).group(1)
    processor=(x/'osfmk/kern/processor.c').read_text();sched=(x/'osfmk/kern/sched_prim.c').read_text()
    create=re.search(r'processor_set_t\npset_create\(.*?\n\}',processor,re.S).group()
    enabled=re.search(r'bool\nsched_steal_thread_enabled\(.*?\n\}',sched,re.S).group()
    steal=re.search(r'static thread_t\nsched_clutch_steal_thread\([^;]*?\)\n\{.*?\n\}',clutch,re.S).group()
    for kind,source in [('topology',topology),('clutch',concat(clutch_expr,dict(create=create,enabled=enabled,steal=steal)))]:
        file=out/(variant+'-'+kind+'.c');file.write_text(source,encoding='utf-8',newline='\n')
        linux='/mnt/d/Desktop/Git/Patch/Y700-Elden/research/cpu-affinity-20261111/host/'
        binary=linux+file.stem
        command=['wsl','-d','Ubuntu-22.04','--','gcc','-std=c11','-Wall','-Wextra','-Werror',
                 '-fsanitize=address,undefined','-fno-pie','-no-pie','-DSELECTED_MULTIPLE='+multiple,linux+file.name,'-o',binary]
        subprocess.run(command,capture_output=True,check=True)
        # Negative controls deliberately hit assert; disable core dump generation.
        run=subprocess.run(['wsl','-d','Ubuntu-22.04','--','bash','-c','ulimit -c 0; exec '+binary],capture_output=True)
        (out/(file.stem+'.log')).write_bytes(run.stdout+run.stderr)
        expected=variant=='candidate'
        assert (run.returncode==0)==expected,(variant,kind,run.returncode)
        if not expected:assert b'Assertion' in run.stderr or b'assertion' in run.stderr,run.stderr
        records.append(dict(variant=variant,test=kind,source_sha256=hashlib.sha256(file.read_bytes()).hexdigest(),
                            exit_code=run.returncode,expected_pass=expected,matched_expectation=True))
report=dict(upstream_test_sha256=hashlib.sha256(up.read_bytes()).hexdigest(),records=records,
            native_thread_scheduling_tested=False,hardware_tested=False,active_candidate_changed=False)
(p/'host-report.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
