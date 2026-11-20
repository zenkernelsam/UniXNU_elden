"""Verify exact Gen4 shared PMIC/rails, pins and frozen camera-clock topology."""
from pathlib import Path
import argparse,hashlib,json,re
from audit_camera_pipeline_board import block
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    files={};spec=json.loads((DRIVER/'power-source.json').read_text())
    for record in (spec,json.loads((DRIVER/'pipeline-source.json').read_text())):
        if record['commit']!='6f97250ead155fc3b980856323e99a4623dae73d':raise ValueError('Not exact frozen Gen4')
        for row in record['files']:
            path=DRIVER/'upstream'/row['file']
            if sha(path)!=row['sha256'] or path.stat().st_size!=row['bytes']:raise ValueError('Frozen source changed: '+row['file'])
            files[row['file']]=path.read_text()
    board=files['sm8750-lenovo-elden.dts'];soc=files['sm8750.dtsi'];checks=[]
    def check(value,why):
        if not value:raise ValueError('Camera Gen4 power topology differs: '+why)
        checks.append(why)
    bus=block(board,'&i2c10 {');wl=block(bus,'pmic@2f {');bl=block(board,'backlight: backlight@76')
    check('compatible = "willsemi,wl2868c"' in wl and 'reg = <0x2f>' in wl,'WL2868C address0x2f')
    check('backlight: backlight@76' in bus and 'reg = <0x76>' in bl,'AW99706 shares original I2C10')
    check('reset-gpios = <&tlmm 126 GPIO_ACTIVE_LOW>' in wl,'global PMIC reset126 active-low; not owned sensor reset')
    check('enable-gpios = <&tlmm 88 GPIO_ACTIVE_HIGH>' in bl,'original backlight HWEN88')
    check('i2c10: i2c@888000' in soc,'original I2C10 SE2 base888000')
    expected={1:(1128000,79),2:(1288000,99),3:(2816000,164),4:(2824000,165),5:(2808000,163),7:(1800000,37)}
    for index,(uv,selector) in expected.items():
        rail=block(wl,'camera_ldo'+str(index)+': ldo'+str(index))
        check(f'regulator-min-microvolt = <{uv}>' in rail and f'regulator-max-microvolt = <{uv}>' in rail,'LDO'+str(index)+' actual min/max')
        minimum=496000 if index<3 else 1504000
        check(uv-minimum==selector*8000,'LDO'+str(index)+' exact linear selector'+str(selector))
    lens=block(board,'rear_lens: lens@c')
    check('vddio-supply = <&camera_ldo7>' in lens and 'vdd-supply = <&camera_ldo5>' in lens,'lens owns LDO5 and ALSO shares LDO7')
    for marker,dvdd,avdd in [('rear_camera: camera@56',1,4),('front_camera: camera@31',2,3)]:
        camera=block(board,marker)
        check('dovdd-supply = <&camera_ldo7>' in camera and f'dvdd-supply = <&camera_ldo{dvdd}>' in camera and
              f'avdd-supply = <&camera_ldo{avdd}>' in camera,marker+' exact rails')
    wl_source=files['wl2868c-regulator.c']
    check('#define WL2868C_LDO_ENABLE\t0x0e' in wl_source and '#define WL2868C_SYS_ENABLE\tBIT(7)' in wl_source,'actual enable/SYS bits')
    check('.enable_time = 150' in wl_source and '.uV_step = 8000' in wl_source,'original150us enable/8mV voltage step')
    check('if (id != 0x82)' in wl_source,'real ID0x82')
    pins=files['pinctrl-sm8750.c']
    for pin,fn in [(8,'qup2_se2'),(9,'qup2_se2'),(92,'cam_mclk'),(93,'cam_aon_mclk4'),(113,'cci_i2c_sda'),(114,'cci_i2c_scl'),(117,'cci_i2c_sda'),(118,'cci_i2c_scl')]:
        check(re.search(r'PINGROUP\('+str(pin)+r',\s*'+fn+r'\s*,',pins),f'GPIO{pin} {fn} actual mux1')
    for marker in ('cci0_0_default:','cci1_0_default:'):
        state=block(board,marker);check(state.count('drive-strength = <2>')==2 and state.count('bias-pull-up')==2,marker+' actual2mA/pullup')
    for name,pa,size in [('camcc',0xade0000,0x20000),('cambistmclkcc',0x1760000,0x6000)]:
        cc=block(soc,name+': clock-controller@'+format(pa,'x'));check(f'reg = <0x0 0x{pa:x} 0x0 0x{size:x}>' in cc,name+' exact controller mapping')
        check('RPMHPD_MMCX' in cc and 'rpmhpd_opp_low_svs' in cc,name+' requires actual RPMh power votes')
    clocks={};cam=files['camcc-sm8750.c'];bist=files['cambistmclkcc-sm8750.c']
    for source,prefix,indices,rcgs,branches in [(cam,'cam_cc_cci_',(0,1),(0x1126c,0x11288),(0x11284,0x112a0)),
                                              (bist,'cam_bist_mclk_cc_mclk',(3,4),(0x4054,0x4070),(0x406c,0x4088))]:
        for index,rcg,branch in zip(indices,rcgs,branches):
            name=prefix+str(index)+'_clk';root=block(source,'static struct clk_rcg2 '+name+'_src =');gate=block(source,'static struct clk_branch '+name+' =')
            check(f'.cmd_rcgr = 0x{rcg:x}' in root and '.ops = &clk_rcg2_shared_ops' in root,name+' exact shared RCG')
            check(f'.halt_reg = 0x{branch:x}' in gate and f'.enable_reg = 0x{branch:x}' in gate and '.enable_mask = BIT(0)' in gate,name+' exact CBCR')
            clocks[name]={'root':rcg,'branch':branch}
    check('F(37500000, P_CAM_CC_PLL0_OUT_EVEN, 16, 0, 0)' in cam,'CCI37.5MHz/actual PLL0even600MHz/div16')
    check('{ P_CAM_CC_PLL0_OUT_EVEN, 2 }' in cam,'CCI original parent select2')
    check('F(19200000, P_BI_TCXO, 1, 0, 0)' in bist and '{ P_BI_TCXO, 0 }' in bist,'MCLK19.2MHz/originalXO parent0/div1')
    domain=block(cam,'static struct gdsc cam_cc_titan_top_gdsc =')
    check('.gdscr = 0x1134c' in domain and '.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE' in domain,'TITAN_TOP uses actual CFG GDSC status/retention')
    top=block(cam,'static struct clk_branch cam_cc_cam_top_ahb_clk =')
    check('.halt_reg = 0x113ac' in top and 'cam_cc_slow_ahb_clk_src' in top,'originaltopAHB branch/slowAHB parent')
    rcg=files['clk-rcg2.c'];branch=files['clk-branch.c']
    check('for (count = 500; count > 0; count--)' in rcg and 'udelay(1)' in rcg,'original RCG update500us bound')
    check('int count = 200;' in branch and 'while (count-- > 0)' in branch and 'udelay(1)' in branch,'original CBCR200us bound')
    result={'passed':True,'commit':spec['commit'],'checks':checks,'check_count':len(checks),'rails':expected,'clocks':clocks,
        'shared_LDO7_consumers':['rear_sensor','front_sensor','rear_lens'],'physical_qualification':False,'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS exact Gen4 shared PMIC/rail/lens/pins/clock topology: '+str(len(checks))+' checks')
if __name__=='__main__':main()
