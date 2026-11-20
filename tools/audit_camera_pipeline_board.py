"""Verify exact Gen4 CAMSS resources/ops and board endpoints from bound Git bytes."""
from pathlib import Path
import argparse,hashlib,json,re
ROOT=Path(__file__).resolve().parents[1];DRIVER=ROOT/'drivers/EldenCamera'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def block(text,marker):
    start=text.index(marker);at=text.index('{',start);end=at+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    spec=json.loads((DRIVER/'pipeline-source.json').read_text());files={}
    for row in spec['files']:
        path=DRIVER/'upstream'/row['file']
        if sha(path)!=row['sha256'] or path.stat().st_size!=row['bytes']:raise ValueError('Exact source changed: '+row['file'])
        files[row['file']]=path.read_text()
    source=files['camss.c'];soc=files['sm8750.dtsi'];board=files['sm8750-lenovo-elden.dts'];checks=[]
    def check(value,reason):
        if not value:raise ValueError('Gen4 camera source contract differs: '+reason)
        checks.append(reason)
    resources=block(source,'static const struct camss_resources sm8750_resources =')
    for field,value in (('version','CAMSS_8750'),('csiphy_res','csiphy_res_8750'),('csid_res','csid_res_8750'),('vfe_res','vfe_res_8750')):
        check(re.search(r'\.'+field+r'\s*=\s*'+value+ r'\b',resources),'sm8750 actual '+field)
    check('.hw_ops = &csid_ops_980' in block(source,'static const struct camss_subdev_resources csid_res_8750[]'),'CSID980')
    check('.hw_ops = &vfe_ops_gen4' in block(source,'static const struct camss_subdev_resources vfe_res_8750[]'),'VFEgen4')
    check('if (camss->res->version == CAMSS_8750 && i != k)' in source,'paired CSID/VFE IDs')
    check('dma_set_mask_and_coherent(dev, 0xffffffff)' in source,'32bit streaming/coherent DMA')
    isp=block(soc,'camss: isp@ad27000')
    matches=re.search(r'reg\s*=\s*(.*?);',isp,re.S)[1];maps=re.findall(r'<0x0\s+(0x[0-9a-f]+)\s+0x0\s+(0x[0-9a-f]+)>',matches)
    names=re.findall(r'"(.*?)"',re.search(r'reg-names\s*=\s*(.*?);',isp,re.S)[1]);check(len(maps)==len(names)==19,'all19resource mappings')
    mapping={name:{'physical':int(pa,16),'bytes':int(size,16)} for name,(pa,size) in zip(names,maps)}
    irq_values=[int(n) for n in re.findall(r'<GIC_SPI\s+(\d+)\s+IRQ_TYPE_EDGE_RISING>',re.search(r'interrupts\s*=\s*(.*?);',isp,re.S)[1])]
    irq_names=re.findall(r'"(.*?)"',re.search(r'interrupt-names\s*=\s*(.*?);',isp,re.S)[1]);check(len(irq_values)==len(irq_names)==16,'all16IRQnames')
    irqs={n:v+32 for n,v in zip(irq_names,irq_values)}
    expected={'csiphy3':(0xadaf000,0x2000,480),'csiphy4':(0xadb1000,0x2000,154),
        'csid0':(0xad27000,0x2b00,633),'csid1':(0xad2a000,0x2b00,635),'csid2':(0xad2d000,0x2b00,463),
        'csid_lite0':(0xad6d000,0xa00,637),'csid_lite1':(0xad72000,0xa00,408),
        'vfe0':(0xac86000,0x10000,465),'vfe1':(0xac96000,0x10000,468),'vfe2':(0xaca6000,0x10000,489),
        'vfe_lite0':(0xad6e000,0x3000,638),'vfe_lite1':(0xad73000,0x3000,409)}
    for name,(pa,size,irq) in expected.items():check(mapping[name]['physical']==pa and mapping[name]['bytes']==size and irqs[name]==irq,name+' exact map/INTID')
    check('iommus = <&apps_smmu 0x1c00 0x00>;' in isp,'source CAMSS SID1c00')
    paths={}
    for sensor,marker,phy,reset,mclk,rotation,link in ((0,'rear_camera: camera@56',3,11,3,90,672000000),(1,'front_camera: camera@31',4,7,4,270,336000000)):
        camera=block(board,marker);endpoints=block(board,'&camss {')
        check(f'CAM_BIST_MCLK_CC_MCLK{mclk}_CLK' in camera,'sensor'+str(sensor)+' exact clock')
        check(f'<&tlmm {reset} GPIO_ACTIVE_LOW>' in camera and f'rotation = <{rotation}>' in camera,'sensor'+str(sensor)+' reset/rotation')
        check(f'link-frequencies = /bits/ 64 <{link}>' in camera and 'data-lanes = <1 2 3 4>' in camera,'sensor'+str(sensor)+' link/lanes')
        check('reg = <'+str(phy)+'>;' in endpoints,'sensor'+str(sensor)+' PHY endpoint')
        paths[str(sensor)]={'PHY':phy,'reset':reset,'MCLK':mclk,'rotation':rotation,'link_hz':link,'receiver_lane_positions':[0,1,2,3]}
    phy=files['camss-csiphy-3ph-1-0.c'];actual=phy[phy.index('case CAMSS_8750:',phy.index('static int csiphy_init')):]
    check('lane_regs_2_4_0' in actual and 'regs->offset = 0x1000;' in actual and 'regs->common_status_offset = 0x138;' in actual,'PHY2.4exacttable/status')
    vfe=files['camss-vfe-gen4.c'];check('writel(addr >> 8' in vfe and 'IS_ALIGNED(buf->addr[0], 256)' in vfe,'256byteaddressunits')
    check('WM_CGC_OVERRIDE_ALL' in vfe and '(0x7FFFFFF)' in vfe,'original globalCGC scope')
    check('#define CAMSS_INIT_BUF_COUNT 2' in files['camss.h'],'twoinitialbufferqueue')
    check('#define CSID_RESET_TIMEOUT_MS 500' in files['camss-csid.h'],'actual500msresettimeout')
    result={'passed':True,'checks':checks,'check_count':len(checks),'repository':spec['repository'],'commit':spec['commit'],
        'maps':mapping,'INTIDs':irqs,'sensor_paths':paths,'register_ops':'CSIPHY2.4/CSID980/VFEgen4','dma_mask_bits':32,'hardware':False}
    a.output.write_text(json.dumps(result,indent=2)+'\n');print('PASS exact TB322FC CAMSS resource/topology/ops contract: '+str(len(checks))+' checks')
if __name__=='__main__':main()
