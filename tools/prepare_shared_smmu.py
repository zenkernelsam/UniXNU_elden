"""Prepare exact sharedSMMU original4K/touch graph, no device access."""
from pathlib import Path
import argparse,hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenSmmu';UP=DRIVER/'upstream'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    for name in ('Shared.c','Shared.h','BootRoute.c','BootRoute.h','Irqs.h','Peripheral.c','Peripheral.h','SharedNative.h','SharedNative.cpp','SharedNativeMethods.inc'):shutil.copyfile(DRIVER/name,out/name)
    text=(out/'Shared.h').read_text().replace('"upstream/EldenWifiSmmu4KTableMemory.h"','"EldenWifiSmmu4KTableMemory.h"');(out/'Shared.h').write_text(text,encoding='utf-8',newline='\n')
    text=(out/'Peripheral.h').read_text().replace('"../EldenCamera/NativeDma.h"','"NativeDma.h"').replace('"../EldenMotion/NativeMemory.h"','"NativeMemory.h"');(out/'Peripheral.h').write_text(text,encoding='utf-8',newline='\n')
    for name in ('NativeDma.h','Pipeline.h','Core.h'):shutil.copyfile(ROOT/'drivers/EldenCamera'/name,out/name)
    for name in ('NativeMemory.h','SensorRpc.h'):shutil.copyfile(ROOT/'drivers/EldenMotion'/name,out/name)
    for name in ('NativeDma.h','NativeMemory.h'):
        text=(out/name).read_text().replace('"../EldenSmmu/upstream/EldenWifiSmmu4KTableMemory.h"','"EldenWifiSmmu4KTableMemory.h"');(out/name).write_text(text,encoding='utf-8',newline='\n')
    sys.path.insert(0,str(DRIVER));from TouchSource import route
    (out/'DmaRoute.c').write_bytes(route());shutil.copyfile(ROOT/'drivers/EldenNvt36536/DmaRoute.h',out/'DmaRoute.h')
    with (out/'DmaRoute.h').open('a',encoding='utf-8') as f:f.write('\n#ifdef __cplusplus\nextern "C" {\n#endif\nint elden_touch_route_expected(const EldenTouchDmaRouteOwner *,uint64_t,unsigned,uint32_t,uint32_t *);\n#ifdef __cplusplus\n}\n#endif\n')
    shutil.copyfile(ROOT/'package/EldenTouchBoard.h',out/'EldenTouchBoard.h');shutil.copyfile(ROOT/'soc/sm8750/include/EldenTouchResources.h',out/'EldenTouchResources.h')
    shutil.copyfile(DRIVER/'BootRoute.h',out/'EldenSmmuBootRoute.h')
    table=json.loads((UP/'source.json').read_text())
    for row in table['files'].values():
        source=UP/row['output']
        if sha(source)!=row['output_sha256']:raise ValueError('Qualified original4K source changed')
        if source.suffix in ('.c','.h') or source.name=='LICENSE':shutil.copyfile(source,out/source.name)
    report={'sources':{p.name:sha(p) for p in out.iterdir() if p.is_file()},'table_source':table,'exact_TB322FC_batch_touch_coexistence':True,'hardware':False}
    (out/'source.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8',newline='\n');print('Prepared actual shared batch/4K/touchSMMU core; physical earlyowner mandatory')
if __name__=='__main__':main()
