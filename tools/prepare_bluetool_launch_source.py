"""Derive the exact helper launch exception over the v122 exit-stack source."""
from pathlib import Path
from prepare_bt_exit_stack_source import derive as exit_source
ROOT=Path(__file__).resolve().parents[1]
def derive():
    text=exit_source().decode()
    marker='static int elden_icon_cpu_owner_launch(proc_t p,struct image_params *imgp,int error,os_reason_t reason)\n{'
    if text.count(marker)!=1:raise ValueError('Owner-launch boundary differs')
    helper=(ROOT/'drivers/EldenBluetooth/runtime/OwnerLaunch.inc').read_text()
    return text.replace(marker,helper+'\n'+marker+'\n    if(elden_bt_owner_launch(p,imgp,error,reason))return 1;').encode()
