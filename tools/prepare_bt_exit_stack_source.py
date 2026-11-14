"""Derive the exact exit-observer source for the next Bluetooth diagnosis."""
from pathlib import Path
import argparse,hashlib,json
ROOT=Path(__file__).resolve().parents[1]
def derive():
    source=ROOT/'profiles/current-build/sources/beea14cef600c760/EldenIconCpuCommon.inc'
    data=source.read_bytes()
    if hashlib.sha256(data).hexdigest()!='beea14cef600c7607a9d381cde30647ec806ec580624c22c37ead94715144dec':raise ValueError('Original exit observer changed')
    marker=b'int elden_icon_cpu_exit_selected(proc_t p)'
    if data.count(marker)!=1:raise ValueError('Exit observer not unique')
    data=data.replace(marker,(ROOT/'drivers/EldenBluetooth/diagnostics/ExitStack.inc').read_bytes()+b'\n'+marker)
    before=b'        if (!proc_getcdhash(p, bt_actual) && !memcmp(bt_actual, bt_expected, sizeof(bt_expected)))\n            return __atomic_add_fetch(&bt_count, 1u, __ATOMIC_RELAXED) <= 16;'
    after=b'        if (!proc_getcdhash(p, bt_actual) && !memcmp(bt_actual, bt_expected, sizeof(bt_expected))) {\n            unsigned allowed=__atomic_add_fetch(&bt_count, 1u, __ATOMIC_RELAXED) <= 16;\n            if(allowed)elden_bt_exit_stack(p);\n            return allowed;\n        }'
    if data.count(before)!=1:raise ValueError('Exact bounded identity gate differs')
    return data.replace(before,after)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():raise ValueError('Fresh output required')
    data=derive();a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_bytes(data)
    print(hashlib.sha256(data).hexdigest())
if __name__=='__main__':main()
