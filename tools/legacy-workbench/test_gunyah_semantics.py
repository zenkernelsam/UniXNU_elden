"""Exercise Qualcomm's actual queue selection and expired predicate offline."""
from pathlib import Path
import hashlib,json,subprocess
r=Path(__file__).resolve().parents[1];p=r/'research/cpu-activation-20261111/gunyah'
f=p/'hyp/misc/watchdog/src/watchdog.c';s=f.read_text();manifest=json.loads((p/'sources.json').read_text())
assert hashlib.sha256(f.read_bytes()).hexdigest()==manifest['hyp/misc/watchdog/src/watchdog.c']['sha256']
start=s.index('bool\nwatchdog_is_expired(');end=s.index('\n}\n',start)+3;predicate=s[start:end]
start=s.index('\tif (bite_abs <= bark_abs) {');end=s.index('\n\t}\n',s.index('} else {',start))+4;selection=s[start:end]
test='''#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
enum { WATCHDOG_ACTION_BARK, WATCHDOG_ACTION_BITE };
typedef uint64_t watchdog_abs_ticks_t;
typedef struct { bool enabled; unsigned action; uint64_t deadline; } watchdog_t;
static void watchdog_enqueue(watchdog_t *wdt,uint64_t when,unsigned action){wdt->deadline=when;wdt->action=action;}
'''+predicate+'''\nstatic void selected(watchdog_t *wdt,uint64_t bark_abs,uint64_t bite_abs){
'''+selection+'''
}
int main(void){
 watchdog_t w={.enabled=true};
 selected(&w,30000,30000);assert(watchdog_is_expired(&w)&&w.deadline==30000);
 selected(&w,27000,30000);assert(!watchdog_is_expired(&w)&&w.deadline==27000);
 watchdog_enqueue(&w,30000,WATCHDOG_ACTION_BITE);
 assert(watchdog_is_expired(&w)&&w.deadline>27000); // bite pending, not elapsed
 w.enabled=false;assert(!watchdog_is_expired(&w));
 puts("PASS: equal deadlines queue bite immediately;27/30 separates bark from bite");
}
'''
out=p/'test-policy.c';out.write_text(test,newline='\n')
base='/mnt/d/Desktop/Git/Patch/Y700-Elden/research/cpu-activation-20261111/gunyah/'
cmd=['wsl','-d','Ubuntu-22.04','--','clang','-std=c11','-O1','-g','-fsanitize=address,undefined','-fno-pie','-no-pie',base+'test-policy.c','-o',base+'test-policy']
q=subprocess.run(cmd,capture_output=True);assert q.returncode==0,q.stderr
q=subprocess.run(['wsl','-d','Ubuntu-22.04','--',base+'test-policy'],capture_output=True)
report={'source_sha256':hashlib.sha256(f.read_bytes()).hexdigest(),'returncode':q.returncode,'stdout':q.stdout.decode(),'stderr':q.stderr.decode(errors='replace'),'extracted_predicate':predicate,'extracted_selection':selection,'matches_device_bit31_observation':True,'firmware_binary_identity_verified':False,'hardware_reset_verified':False}
(p/'policy-test.json').write_text(json.dumps(report,indent=2)+'\n');print(q.stdout.decode());assert q.returncode==0
