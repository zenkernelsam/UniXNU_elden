"""Check exact Gen4 graph and every actual source-derived helper against Git pins."""
from pathlib import Path
import argparse,hashlib,json,re,sys
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    sys.path.insert(0,str(DRIVER));from ClockSource import derive,block
    generated,g,functions=derive();spec=json.loads((DRIVER/'clock-source.json').read_text());files={r['file']:(DRIVER/'upstream'/r['file']).read_text() for r in spec['files']}
    checks=[]
    def check(v,name):
        if not v:raise ValueError('Camera clock source mismatch: '+name)
        checks.append(name)
    check(spec['commit']=='6f97250ead155fc3b980856323e99a4623dae73d','exact Gen4/SM8750 commit')
    check(len(g['clock_ids'])==39 and len(g['roots'])==21 and len(g['branches'])==37 and len(g['domains'])==4,'complete source graph counts')
    for row in g['clock_ids']:
        if row['branch'] is not None:
            b=g['branches'][row['branch']];check(b['root']==row['root'],'original DT branch/root '+row['name'])
    for r in g['roots']:
        for f in r['frequencies']:
            check(f['hid']<32 and f['selector']<8 and f['m']<=255 and f['n']<=255,r['name']+'/'+str(f['hz'])+' exact F encoding')
    soc=files['sm8750.dtsi'];pins=files['pinctrl-sm8750.c'];msm=files['pinctrl-msm.c'];gdsc=files['gdsc.c']
    check('reg = <0x0 0x0f100000 0x0 0x102000>' in block(soc,'tlmm: pinctrl@f100000'),'actual TLMM mapping')
    check('.egpio_present = 11' in pins and '.egpio_enable = 12' in pins and '.egpio_func = 11' in pins,'actual eGPIO readback/claim fields')
    check('.oe_bit = 9' in pins and '.mux_bit = 2' in pins and '.pull_bit = 0' in pins and '.drv_bit = 6' in pins,'actual pin config layout')
    check('val |= BIT(g->egpio_enable)' in msm,'original AP eGPIO ownership')
    check('MSM_PULL_UP\t\t3' in msm and 'arg = (arg / 2) - 1;' in msm,'actual2mA/pullup encoding')
    body=block(msm,'static int msm_gpio_direction_output(')
    check(body.index('msm_writel_io(')<body.index('msm_writel_ctl('),'actual GPIO latch-before-OE ordering')
    check('#define GDSC_POWER_UP_COMPLETE\t\tBIT(16)' in gdsc and '#define GDSC_POWER_DOWN_COMPLETE\tBIT(15)' in gdsc,'actual CFG domain power bits')
    check('reg = sc->gdscr + CFG_GDSCR_OFFSET;' in gdsc,'domain status uses CFG rather than genericbit31')
    check(len(functions)==23,'actual23 original helper bodies')
    for row in functions:check(len(row['original_sha256'])==64 and len(row['derived_sha256'])==64,'actual function body hash '+row['signature'])
    for group,count in [('phy',6),('csid',5),('vfe',5)]:check(len(g['CAMSS_clock_plans'][group])==count,group+' exact per-instance clock/rate plans')
    board=(DRIVER/'Board.c').read_text();core=(DRIVER/'Clock.c').read_text()
    check('ClockBoard.inc' in board and 'validate_clock_list' in board and 'if(!hz)return -22' in board,'actual board requires explicit complete rates')
    check('elden_camera_clock_rate_supported(r->clock,hz)' in board,'source-supported complete rate plan before physical acquire')
    check('elden_camera_pipeline_resources(config,physical,bytes,intids)' in board,'actual full mode/RDI preflight before power')
    check('cleanup_mask' in board and 'EldenCciCleanup' in board and 'observe_cleanup' in board,'only original serialized STOP uses cleanup clocks')
    check('r->dep_attempted=1' in core and 'r->changed=1' in core and 'original_status' in core,'actual attempted ownership and settled domain snapshots')
    check('clk_branch2_check_halt(&br->branch,false)' in core,'actual halt proof after original void disable')
    report={'passed':True,'checks':checks,'check_count':len(checks),'source_commit':spec['commit'],'graph':g,'actual_helper_body_hashes':functions,
        'generated_sha256':{n:hashlib.sha256(b).hexdigest() for n,b in generated.items()},'hardware':False}
    a.output.write_text(json.dumps(report,indent=2)+'\n');print('PASS exact camera clock/board-source graph and helper identity: '+str(len(checks))+' checks')
if __name__=='__main__':main()
