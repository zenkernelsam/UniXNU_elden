"""Test actual touch coordinator with Protocol/pen/flow and explicit OS peers."""
from pathlib import Path
import argparse,hashlib,json,subprocess,re
ROOT=Path(__file__).resolve().parents[1];WAKE=ROOT/'drivers/EldenWake';TOUCH=ROOT/'drivers/EldenNvt36536';PEN=ROOT/'drivers/EldenPen'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('base-native','output'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();a.base_native=a.base_native.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    capture=json.loads((a.base_native/'capture.json').read_text());jobs=[(n,r) for n,r in capture['files'].items() if n.endswith('/EldenNvtFirmware.h')]
    if len(jobs)!=1:raise ValueError('Exact original NVT firmware header required')
    name,row=jobs[0];old=name.split('/research/')[0];header=a.base_native/'build/project/records'/name.split('/research/',1)[1]
    if sha(header)!=row['sha256']:raise ValueError('Captured firmware header changed')
    text=header.read_text();data=bytes(int(v,16) for v in re.findall(r'0x([0-9a-fA-F]{2})',text));firmware=out/'stock-NVT-private.bin';firmware.write_bytes(data)
    def run(cmd,label):
        r=subprocess.run(list(map(str,cmd)),capture_output=True);(out/(label+'.log')).write_bytes(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(label+' failed; retained log')
        return r.stdout.decode()
    flags=['-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',WAKE,'-I',TOUCH,'-I',PEN]
    objects=[];inputs={}
    for path in [TOUCH/'Protocol.c',TOUCH/'PointReport.c',PEN/'Delivery.c',WAKE/'Flow.c',WAKE/'Gesture.c',WAKE/'TestSleepProtocolPeer.c']:
        obj=out/(path.stem+'.o');run(['xcrun','clang','-std=c11',*flags,'-c',path,'-o',obj],path.stem+'-build');objects.append(obj);inputs[str(path.relative_to(ROOT))]=sha(path)
    run(['xcrun','clang++','-std=c++17',*flags,WAKE/'TestTouchCoordinator.cpp',*objects,'-o',out/'test'],'coordinator-build')
    msg=run([out/'test',firmware],'coordinator-run')
    for path in [WAKE/'TouchCoordinator.inc',WAKE/'TouchFields.inc',WAKE/'TouchPower.h',WAKE/'TestTouchCoordinator.cpp',PEN/'Delivery.h']:
        inputs[str(path.relative_to(ROOT))]=sha(path)
    (out/'result.json').write_text(json.dumps({'passed':True,'host_output':msg,'sources':inputs,'actual_touch_coordinator':True,
        'actual_NVT_protocol_firmware_restore':True,'actual_pen_nonterminal_delivery':True,'actual_wake_flow':True,
        'firmware_header_sha256':row['sha256'],'firmware_sha256':sha(firmware),'OS_DMA_GIC_power_HID_modeled':True,
        'hardware':False,'real_board_power_provider':False},indent=2)+'\n');print(msg)
if __name__=='__main__':main()
