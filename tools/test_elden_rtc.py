"""Test actual RTC/PMIC and source-bound platform clock methods with host peers."""
from pathlib import Path
import argparse,hashlib,json,subprocess,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenRtc'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--source',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);results={}
    def run(cmd,name):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(name+'.log')).write_bytes(r.stdout+r.stderr);r.check_returncode();return r.stdout.decode()
    flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',a.source]
    for name,files in [('core',[a.source/'Core.c',DRIVER/'TestCore.c']),('spmi',[a.source/'Spmi.c',a.source/'TransactionGate.c',DRIVER/'TestSpmi.c'])]:
        run(['xcrun','clang',*flags,*files,'-o',out/name],name+'-build');results[name]=run([out/name],name+'-run')
    # Extract the genuine platform registration/date/get/set block. Other
    # IOKit configuration/nub methods are retained separately and not modeled.
    text=(a.source/'ArmDTPlatform.cpp').read_text();start=text.index('extern "C" {\nextern const char version[];')
    end=text.index('// Serialized empty name lists:',start)
    test=out/'TestPlatform.cpp'
    test.write_text('''#include <cstdint>
#include <cassert>
#include <cstring>
class ArmDTPlatform {public:long getGMTTimeOfDay();void setGMTTimeOfDay(long);};
extern "C" const char version[]="Darwin Kernel Version 23.6.0: Sat Nov 16 02:01:13 UTC 2026; root:test";
'''+text[start:end]+'''
static uint64_t supplied,set;static int result;
static int read(uint64_t *out){*out=supplied;return result;}
static void write(uint64_t now){set=now;}
int main(){ArmDTPlatform p;auto floor=p.getGMTTimeOfDay();assert(floor>1700000000);armdt_platform_clock_register(read,write);
 supplied=floor+100;assert(p.getGMTTimeOfDay()==(long)supplied);result=-1;assert(p.getGMTTimeOfDay()==floor);
 result=0;supplied=floor-1;assert(p.getGMTTimeOfDay()==floor);p.setGMTTimeOfDay(1800000000);assert(set==1800000000);p.setGMTTimeOfDay(0);assert(set==1800000000);
 armdt_platform_clock_register(nullptr,nullptr);assert(p.getGMTTimeOfDay()==floor);return 0;}
''')
    run(['xcrun','clang++','-std=c++17',*flags[1:],test,'-o',out/'platform'],'platform-build');run([out/'platform'],'platform-run')
    results['platform']='actual upstream registration/date/get/set methods pass; OS configuration outside scope'
    (out/'result.json').write_text(json.dumps({'passed':True,'results':results,'source_record_sha256':hashlib.sha256((a.source/'source.json').read_bytes()).hexdigest(),
        'PMIC_peers_modeled':True,'hardware':False},indent=2)+'\n');print(''.join(results.values()))
if __name__=='__main__':main()
