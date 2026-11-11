"""Y700 read-only Android/Recovery power-supply collector; default is offline."""
import argparse,datetime,json,shlex,subprocess,time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
ADB=Path('D:/Desktop/Git/SchoolBox/Tools/platform-tools_ADB/adb.exe')
SERIAL='HA2A1PT2'
KEYS=('type','present','online','status','capacity','voltage_now','current_now',
      'voltage_max','current_max','input_current_limit','constant_charge_current',
      'constant_charge_current_max','charge_type','usb_type','real_type','pd_active',
      'pd_voltage_max','pd_current_max','charging_enabled','input_suspend',
      'charge_control_limit','charge_control_limit_max','charge_control_start_threshold',
      'charge_control_end_threshold','temp','health','charge_counter','charge_full','charge_full_design',
      'cycle_count','time_to_full_now','time_to_full_avg','time_to_empty_now','time_to_empty_avg')
OEM_KEYS=('charging_enable','protection_setting','protection_setting_eu','recharge_setting',
          'battery_maintenance','battery_maintenance20','input_suspend','typec_cc_orientation',
          'typec_cc_orientation2','chip_main_charge','chip_fg1','chip_fg2','chip_cp1','chip_cp2',
          'chip_pdphy','chip_qc','chip_sc7603a','chip_sc7603b','fg1_temp','fg2_temp','fg1_current','fg2_current',
          'fg1_voltage','fg2_voltage','fg1_soc','fg2_soc','sc8541_master_ibus','sc8541_slave_ibus',
          'ibus_total','pps_status','usb1_online','usb2_online','battery_parallel_cell_count')
ALL_KEYS=tuple(dict.fromkeys(KEYS+OEM_KEYS))

def read_script():
    # The only evaluated text is this fixed script. Sysfs contents are never
    # evaluated as shell code; printf arguments preserve spaces and punctuation.
    return '''for supply in /sys/class/power_supply/*; do
  [ -d "$supply" ] || continue
  name=${supply##*/}
  for key in %s; do
    [ -r "$supply/$key" ] || continue
    printf '%%s\\t%%s\\t' "$name" "$key"
    cat "$supply/$key"
  done
done
for class in /sys/class/qcom-battery; do
  [ -d "$class" ] || continue
  for key in %s; do
    [ -r "$class/$key" ] || continue
    printf 'qcom-battery\\t%%s\\t' "$key"
    cat "$class/$key"
  done
done
for chip in /sys/class/gpio/gpiochip* /sys/bus/gpio/devices/gpiochip*; do
  [ -d "$chip" ] || continue
  name=${chip##*/}
  for key in base ngpio label; do
    [ -r "$chip/$key" ] || continue
    printf 'gpio:%%s\\t%%s\\t' "$name" "$key"
    cat "$chip/$key"
  done
done
for port in /sys/class/typec/*; do
  [ -d "$port" ] || continue
  name=${port##*/}
  for key in data_role power_role power_operation_mode orientation port_type; do
    [ -r "$port/$key" ] || continue
    printf 'typec:%%s\\t%%s\\t' "$name" "$key"
    cat "$port/$key"
  done
done
'''%(' '.join(ALL_KEYS),' '.join(OEM_KEYS))

def parse_inventory(raw):
    supplies,gpio,typec={},{},{}
    for line in raw.splitlines():
        parts=line.split('\t',2)
        if len(parts)!=3:continue
        name,key,value=parts
        if name.startswith('gpio:'):
            name=name[5:];target=gpio;allowed=('base','ngpio','label')
        elif name.startswith('typec:'):
            name=name[6:];target=typec;allowed=('data_role','power_role','power_operation_mode','orientation','port_type')
        else:target=supplies;allowed=ALL_KEYS
        if key in allowed and name and all(c.isalnum() or c in '_.-:,' for c in name):
            target.setdefault(name,{})[key]=value
    mapped=[]
    for name,item in gpio.items():
        try:base=int(item['base']);count=int(item['ngpio'])
        except (ValueError,KeyError):continue
        if base>=0 and count>0 and base<=541<base+count:
            mapped.append(dict(chip=name,label=item.get('label'),base=base,ngpio=count,line=541-base))
    return dict(supplies=supplies,gpio_chips=gpio,gpio541_candidates=mapped,typec=typec)

def run(*arguments,timeout=15,input_text=None):
    result=subprocess.run([ADB,'-s',SERIAL,*arguments],capture_output=True,timeout=timeout,
                          input=None if input_text is None else input_text.encode('utf-8'))
    text=result.stdout.decode('utf-8',errors='replace')
    if result.returncode:raise RuntimeError(result.stderr.decode('utf-8',errors='replace'))
    return text

def root_command():
    if 'uid=0' in run('shell','id'):return ['sh']
    if 'uid=0' in run('shell','su','-c','id'):return ['su','-c','sh']
    raise RuntimeError('root shell is required for the complete read-only power inventory')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--collect',action='store_true')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--label',default='unspecified',help='Physical cable arrangement; stored as text only')
    parser.add_argument('--samples',type=int,default=1)
    parser.add_argument('--interval',type=float,default=5,help='Seconds between reads, at most30')
    args=parser.parse_args()
    if not args.collect:
        print('Offline ready: --collect reads only Y700 power_supply/dual-FG/typec/GPIO base metadata and dumpsys battery; no reboot or writes.');return
    if not 1<=args.samples<=60 or not 0<=args.interval<=30:parser.error('samples1..60, interval0..30seconds')
    if not args.output:parser.error('--collect requires a fresh --output under this project edl_work')
    out=args.output.resolve()
    if not out.is_relative_to(ROOT/'edl_work') or out.exists():parser.error('output must be a new project edl_work directory')
    if run('get-state').strip()!='device':raise RuntimeError('Y700 ADB is not ready')
    serial=run('shell','getprop','ro.serialno').strip()
    if serial!=SERIAL:raise RuntimeError('Y700 hardware serial differs')
    product=run('shell','getprop','ro.product.device').strip()
    if product not in ('TB322FC','elden','elden_prc_wifi'):raise RuntimeError('Y700 product differs')
    prefix=root_command()
    script=read_script();samples=[];transcripts=[]
    # One fully quoted remote command preserves the su -c boundary. This stock
    # adbd does not reliably forward EOF into exec-out su -c sh via stdin.
    remote=('su -c ' if prefix[0]=='su' else 'sh -c ')+shlex.quote(script)
    for i in range(args.samples):
        start=time.monotonic_ns();utc=datetime.datetime.now(datetime.timezone.utc).isoformat()
        raw=run('exec-out',remote,timeout=30);sample=parse_inventory(raw)
        if not sample['supplies']:raise RuntimeError('no power-supply attributes observed')
        sample.update(utc=utc,host_start_ns=start,read_duration_ms=(time.monotonic_ns()-start)//1000000)
        samples.append(sample);transcripts.append(raw)
        if i+1<args.samples:time.sleep(args.interval)
    supplies=samples[-1]['supplies']
    battery=run('shell','dumpsys','battery')
    report=dict(serial=serial,utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                read_only=True,root_mode=prefix[0],product=product,label=args.label,supplies=supplies,samples=samples,
                interpretation='Raw kernel units and signs; qualify using cable changes and capacity/voltage trend before inferring net charge.')
    out.mkdir(parents=True)
    (out/'power-supplies.tsv').write_text(raw,encoding='utf-8')
    (out/'sample-transcripts.json').write_text(json.dumps(transcripts,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    (out/'dumpsys-battery.txt').write_text(battery,encoding='utf-8')
    (out/'power.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(output=str(out),label=args.label,samples=len(samples),supplies=sorted(supplies),
                         gpio541=samples[-1]['gpio541_candidates'],read_only=True),ensure_ascii=False,indent=2))

if __name__=='__main__':main()
