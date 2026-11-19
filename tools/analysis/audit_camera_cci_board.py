"""Audit exact TB322FC physical CCI controllers, internal masters/queues and timing."""
from pathlib import Path
import argparse,hashlib,json,re,subprocess
ROOT=Path(__file__).resolve().parents[2];SPEC=ROOT/'drivers/EldenCamera/cci-source.json'
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--linux',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False);s=json.loads(SPEC.read_text())
    def blob(name):return subprocess.check_output(['git','show',s['commit']+':'+name],cwd=a.linux)
    source=blob(s['path'])
    if len(source)!=s['bytes'] or hashlib.sha256(source).hexdigest()!=s['sha256']:raise ValueError('Original CCI source differs')
    dts=blob('arch/arm64/boot/dts/qcom/sm8750.dtsi');board=blob('arch/arm64/boot/dts/qcom/sm8750-lenovo-elden.dts')
    t=source.decode();text=dts.decode();sensor=board.decode();controllers=[]
    for row in s['controllers']:
        block=re.search(r'cci'+str(row['controller'])+r': cci@[^\n]+\{(.*?)\n\t\t\};',text,re.S)
        if not block:raise ValueError('Original CCI node missing')
        b=block[1];regs=re.search(r'reg\s*=\s*<0x0 (0x[0-9a-f]+) 0x0 (0x[0-9a-f]+)>',b)
        irq=re.search(r'interrupts\s*=\s*<GIC_SPI (\d+) IRQ_TYPE_EDGE_RISING>',b)
        if not regs or not irq or (int(regs[1],0),int(regs[2],0),int(irq[1]))!=(row['physical'],row['bytes'],row['irq_SPI']):raise ValueError('Exact controller address/IRQ differs')
        if '"qcom,sm8750-cci", "qcom,msm8996-cci"' not in b or 'assigned-clock-rates = <37500000>' not in b or 'clock-frequency = <400000>' not in b:
            raise ValueError('Exact board compatible/clock differs')
        controllers.append({'controller':row['controller'],'physical':row['physical'],'INTID':int(irq[1])+32,'internal_master':0})
    for name,required in [('cci_i2c_write','u8 queue = QUEUE_0;'),('cci_i2c_read','u8 queue = QUEUE_1;')]:
        at=t.index('static int '+name+'(');end=t.index('\n}',at)
        if required not in t[at:end]:raise ValueError('Exact queue selection differs')
    if '{ .compatible = "qcom,msm8996-cci", .data = &cci_v2_data}' not in t:raise ValueError('CCI compatible profile differs')
    at=t.index('static const struct cci_data cci_v2_data =');end=t.index('static const struct cci_data cci_msm8953_data',at);v2=t[at:end]
    at=v2.index('.params[I2C_MODE_FAST]');end=v2.index('.params[I2C_MODE_FAST_PLUS]',at);fast=v2[at:end]
    expected={'thigh':38,'tlow':56,'tsu_sto':40,'tsu_sta':40,'thd_dat':22,'thd_sta':35,'tbuf':62,'scl_stretch_en':0,'trdhld':6,'tsp':3}
    for k,value in expected.items():
        if not re.search(r'\.'+k+r'\s*=\s*'+str(value)+r'\b',fast):raise ValueError('CCI exact timing differs: '+k)
    for label,slave in [('cci0_i2c0',0x56),('cci1_i2c0',0x31)]:
        block=re.search(r'&'+label+r'\s*\{(.*?)\n\};',sensor,re.S)
        if not block or not re.search(r'reg\s*=\s*<'+hex(slave)+r'>',block[1]):raise ValueError('Sensor controller0 mapping differs')
    result={'passed':True,'exact_CCI_profile_source':s,'controllers':controllers,'v2_fast_timing':expected,
        'read_queue':1,'write_queue':0,'source_identity_only':True,'hardware':False,
        'source_hashes':{'sm8750.dtsi':hashlib.sha256(dts).hexdigest(),'sm8750-lenovo-elden.dts':hashlib.sha256(board).hexdigest()}}
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n');print('PASS: exact Gen4 CCI0/rear +CCI1/front, both internal master0, original read1/write0 and37.5MHz/v2timing')
if __name__=='__main__':main()
