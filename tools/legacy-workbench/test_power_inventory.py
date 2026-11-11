"""Offline checks of the exact OEM collector; does not invoke ADB."""
import json,subprocess
from pathlib import Path
from collect_power_evidence import parse_inventory,read_script,ALL_KEYS

def main():
    script=read_script()
    assert 'fg1_temp' in script and 'fg2_temp' in script
    assert 'sc8541_master_ibus' in script and 'sc8541_slave_ibus' in script
    assert len(ALL_KEYS)==len(set(ALL_KEYS))
    raw=('battery\tcurrent_now\t-470000\nbattery\tcapacity\t65\n'
         'qcom-battery\tfg1_temp\t275\nqcom-battery\tfg2_temp\t281\n'
         'qcom-battery\tfg1_current\t200\nqcom-battery\tfg2_current\t270\n'
         'gpio:gpiochip0\tbase\t512\ngpio:gpiochip0\tngpio\t232\n'
         'gpio:gpiochip0\tlabel\ttest TLMM label\n'
         'typec:port0\tdata_role\t[device] host\n'
         'typec:port0\tpower_role\t[sink] source\n'
         'evil/../../x\tcapacity\t90\ngpio:gpiochip1\tbase\t$(whoami)\n')
    r=parse_inventory(raw)
    assert r['supplies']['battery']['current_now']=='-470000'
    assert r['supplies']['qcom-battery']['fg1_temp']=='275'
    assert r['supplies']['qcom-battery']['fg2_temp']=='281'
    assert r['gpio541_candidates']==[dict(chip='gpiochip0',label='test TLMM label',base=512,ngpio=232,line=29)]
    assert r['typec']['port0']['data_role']=='[device] host'
    assert 'evil/../../x' not in r['supplies']
    # Shell parse only; syntax validation neither reads a device nor executes it.
    result=subprocess.run(['wsl','-d','Ubuntu-22.04','-u','root','--','sh','-n'],input=script.encode(),capture_output=True)
    assert result.returncode==0,result.stderr.decode(errors='replace')
    out=Path(__file__).resolve().parent/'battery-telemetry-test-20261108';out.mkdir(exist_ok=True)
    (out/'collector.json').write_text(json.dumps(dict(accepted=True,read_only=True,device_called=False,
        dual_fg_temperature_included=True,power_inputs_separate=True,gpio541_resolved_from_base=True,
        typec_roles_preserved=True,shell_syntax=True),indent=2)+'\n')
    print('PASS OEM inventory parser, dual gauges, GPIO541 mapping and shell syntax; no ADB')
if __name__=='__main__':main()
