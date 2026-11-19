"""Verify the exact Armada TB322FC GPIO/PDC pin/type/version contract in Git blobs."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[2];SPEC=ROOT/'drivers/EldenWake/irq-source.json'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--linux',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);s=json.loads(SPEC.read_text());text={}
    for row in s['files']:
        b=subprocess.check_output(['git','show',s['commit']+':'+row['path']],cwd=a.linux)
        if len(b)!=row['bytes'] or hashlib.sha256(b).hexdigest()!=row['sha256']:raise ValueError('Original source differs: '+row['path'])
        text[Path(row['path']).name]=b.decode()
    pin=text['pinctrl-sm8750.c'];dts=text['sm8750.dtsi'];board=text['sm8750-lenovo-elden.dts'];pdc=text['qcom-pdc.c']
    table=pin[pin.index('sm8750_pdc_map[]'):pin.index('static const struct msm_pinctrl_soc_data')]
    pairs=[(int(a),int(b)) for a,b in re.findall(r'\{\s*(\d+)\s*,\s*(\d+)\s*\}',table)]
    if len([p for p in pairs if p[0]==162])!=1 or dict(pairs)[162]!=109:raise ValueError('Exact GPIO162 PDC map differs')
    block=dts[dts.index('pdc: interrupt-controller@'):dts.index('tsens0:',dts.index('pdc: interrupt-controller@'))]
    ranges=re.search(r'qcom,pdc-ranges\s*=\s*(.*?);',block,re.S)
    rows=[tuple(map(int,t.split())) for t in re.findall(r'<(.*?)>',ranges[1])]
    matches=[parent+109-first for first,parent,n in rows if first<=109<first+n]
    if matches!=[620]:raise ValueError('Exact PDC109 GIC SPI differs')
    if not re.search(r'interrupts\s*=\s*<162 IRQ_TYPE_EDGE_FALLING>',board):raise ValueError('Board touch edge differs')
    for name,value in [('intr_target_bit',8),('intr_wakeup_present_bit',6),('intr_wakeup_enable_bit',7)]:
        if not re.search(r'\.'+name+r'\s*=\s*'+str(value)+r',',pin):raise ValueError('Exact TLMM field differs: '+name)
    for name,value in [('PDC_EDGE_FALLING','0b010'),('IRQ_i_CFG_IRQ_ENABLE','3')]:
        if not re.search(r'\b'+name+r'\s*(?:=\s*)?'+value+r'\b',pdc):raise ValueError('Original PDC field differs')
    if 'pdc_version < PDC_VERSION_3_2' not in pdc or 'PDC_VERSION(3, 2, 0)' not in pdc:raise ValueError('Original version gate differs')
    if 'pdc_type |= (old_pdc_type & ~IRQ_i_CFG_TYPE_MASK);' not in pdc or 'irq_chip_set_parent_state(d, IRQCHIP_STATE_PENDING, false);' not in pdc:
        raise ValueError('Original type/pending preservation differs')
    if 'IRQ_DOMAIN_FLAG_QCOM_PDC_WAKEUP' not in text['irq.h'] or 'Line must be masked at TLMM' not in text['irq.h']:
        raise ValueError('Direct-PDC versus TLMM-summary policy differs')
    result={'passed':True,'sources':s,'actual_gpio_PDC_GIC_range_verified':True,'source_checks':16,'hardware':False,
        'actual_route_expected_only':True,'Sheng_board_addresses_selected':False,'fixed_Git_blobs_not_working_tree':True}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS: exact TB322FC GPIO162/PDC109/SPI620/INTID652, falling inversion and version/ownership contract')
if __name__=='__main__':main()
