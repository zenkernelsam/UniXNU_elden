"""Extract actual TB322FC CSIPHY2.4/CSID980/VFEgen4 register and queue bodies."""
from pathlib import Path
import hashlib,json,re
HERE=Path(__file__).resolve().parent
def function(t,signature):
    start=t.index(signature);end=t.index('{',start)+1;depth=1
    while depth:depth+=(t[end]=='{')-(t[end]=='}');end+=1
    return t[start:end]
def access(text,name,prefix):
    pattern=re.compile(r'\b'+name+r'\(');out='';at=0
    while (m:=pattern.search(text,at)):
        end=m.end();depth=1
        while depth:depth+=(text[end]=='(')-(text[end]==')');end+=1
        inner=text[m.end():end-1]
        if name.startswith('readl'):
            if not inner.startswith(prefix+'->base +'):raise ValueError('Actual pipeline read address changed')
            value='pipe_'+prefix+'_read('+prefix+', '+inner[len(prefix+'->base +'):].strip()+')'
        else:
            depth=0;split=None
            for i,c in enumerate(inner):
                depth+=(c=='(')-(c==')')
                if c==',' and not depth:split=i
            if split is None:raise ValueError('Actual pipeline write shape changed')
            data,address=inner[:split],inner[split+1:].strip()
            if not address.startswith(prefix+'->base +'):raise ValueError('Actual pipeline write address changed')
            value='pipe_'+prefix+'_write('+prefix+', '+address[len(prefix+'->base +'):].strip()+', '+data+')'
        out+=text[at:m.start()]+value;at=end
    return out+text[at:]
def derive():
    spec=json.loads((HERE/'pipeline-source.json').read_text());texts={}
    for row in spec['files']:
        b=(HERE/'upstream'/row['file']).read_bytes()
        if len(b)!=row['bytes'] or hashlib.sha256(b).hexdigest()!=row['sha256']:raise ValueError('Exact CAMSS source changed: '+row['file'])
        texts[row['file']]=b.decode().replace('\r\n','\n')
    phy=texts['camss-csiphy-3ph-1-0.c'];csid=texts['camss-csid-980.c'];vfe=texts['camss-vfe-gen4.c'];queue=texts['camss-vfe.c']
    license='/* SPDX-License-Identifier: GPL-2.0 */\n/* Source-derived from Qualcomm/Linaro/Linux CAMSS; original files/licenses retained. */\n'
    out={};records={}
    out['PhyRegisters.inc']=license+phy[phy.index('#define CSIPHY_3PH_LNn_CFG1'):phy.index('/* 5nm 2PH')]
    out['PhyLaneTable.inc']=license+function(phy,'static const struct\ncsiphy_lane_regs lane_regs_2_4_0[] =')+';\n'
    out['CsidRegisters.inc']=license+csid[csid.index('/* Reset and Command Registers */'):csid.index('static void __csid_full_aup_rup_trigger')]
    out['VfeRegisters.inc']=license+vfe[vfe.index('#define BUS_REG_BASE'):vfe.index('static void vfe_wm_start')]
    enum=function(texts['camss.h'],'enum camss_version {')+';\n';out['CamssVersion.inc']=license+enum
    decode=texts['camss-csid-gen3.h'];out['CsidDecode.inc']=license+'\n'.join(line for line in decode.splitlines() if line.startswith('#define DECODE_FORMAT') or line.startswith('#define PLAIN_FORMAT'))+'\n'
    groups={
      'PhyMethods.inc':[(phy,'static void csiphy_reset(', 'csiphy'),(phy,'static void csiphy_gen2_config_lanes(', 'csiphy'),
                        (phy,'static void csiphy_lanes_enable(', 'csiphy'),(phy,'static void csiphy_lanes_disable(', 'csiphy')],
      'CsidMethods.inc':[(csid,signature,'csid') for signature in ('static void __csid_full_aup_rup_trigger(', 'static void __csid_aup_update(',
          'static void __csid_rup_update(', 'static void __csid_aup_rup_clear(', 'static void __csid_configure_rx(',
          'static void __csid_configure_rx_vc(', 'static void __csid_ctrl_rdi(', 'static void __csid_configure_rdi_pix_store(',
          'static void __csid_configure_rdi_stream(', 'static void csid_configure_stream(', 'static void csid_subdev_reg_update(',
          'static u64 csid_read_timestamp(', 'static irqreturn_t csid_isr(', 'static int csid_reset(')],
      'VfeMethods.inc':[(vfe,signature,'vfe') for signature in ('static void vfe_wm_start(', 'static void vfe_wm_stop(', 'static void vfe_wm_update(',
                        'static void vfe_reg_update(', 'static inline void vfe_reg_update_clear(')],
      'VfeQueueMethods.inc':[(queue,signature,'vfe') for signature in ('void vfe_frame_start(', 'void vfe_buf_done(', 'int vfe_queue_buffer_v2(')]
    }
    for name,functions in groups.items():
        bodies=[]
        for text,signature,prefix in functions:
            original=function(text,signature);body=original
            for op in ('readl_relaxed','writel_relaxed','readl','writel'):body=access(body,op,prefix)
            changes=['owned error-propagating '+prefix+' read/write accessors']
            if signature=='static void csiphy_lanes_enable(':
                anchor='\tif (csiphy_is_gen2(csiphy->camss->res->version))\n\t\tcsiphy_gen2_config_lanes(csiphy, settle_cnt);\n\telse\n\t\tcsiphy_gen1_config_lanes(csiphy, cfg, settle_cnt);'
                if body.count(anchor)!=1:raise ValueError('Qualified Gen2 lane selection changed')
                body=body.replace(anchor,'\t(void)lane_mask;\n\tcsiphy_gen2_config_lanes(csiphy, settle_cnt);')
                changes.append('only validated SM8750 uses original Gen2 table; unrelated Gen1 excluded')
            if signature=='static u64 csid_read_timestamp(':
                before='\tdo {\n\t\thi = pipe_csid_read(csid, offset + 4);\n\t\tlo = pipe_csid_read(csid, offset);\n\t} while (hi != pipe_csid_read(csid, offset + 4));\n\n\treturn (u64)hi << 32 | lo;'
                after='\tfor(unsigned attempt=0;attempt<32;attempt++){\n\t\thi=pipe_csid_read(csid,offset+4);lo=pipe_csid_read(csid,offset);\n\t\tif(hi==pipe_csid_read(csid,offset+4))return (u64)hi<<32|lo;\n\t}\n\tpipe_failure(csid->camss->core,-110);return 0;'
                if body.count(before)!=1:raise ValueError('Actual timestamp high-low-high changed')
                body=body.replace(before,after);changes.append('bound real high-low-high coherence to32tries; refusal retains DMA')
            if signature=='static int csid_reset(':
                body=body.replace('dev_err(csid->camss->dev, "CSID reset timeout\\n");','')
                changes.append('diagnostic text omitted; actual timeout and reset result retained')
            if signature=='static irqreturn_t csid_isr(':
                body=body.replace('\tstruct csid_device *csid = dev;','\t(void)irq;\n\tstruct csid_device *csid = dev;')
            if signature=='static void vfe_wm_update(':
                body=body.replace('\twm = RDI_WM(wm);','\t(void)line;\n\twm = RDI_WM(wm);')
            if signature=='static void csiphy_lanes_disable(':
                body=body.replace('\tstruct csiphy_device_regs *regs = csiphy->regs;','\t(void)cfg;\n\tstruct csiphy_device_regs *regs = csiphy->regs;')
            if signature in ('void vfe_frame_start(','void vfe_buf_done(','int vfe_queue_buffer_v2('):
                body=body.replace('void vfe_frame_start(','static void vfe_frame_start(').replace('void vfe_buf_done(','static void vfe_buf_done(').replace('int vfe_queue_buffer_v2(','static int vfe_queue_buffer_v2(')
                changes.append('actual queue callbacks stage retained frame metadata; no framework success emulation')
            bodies.append(body);records[signature]={'original_sha256':hashlib.sha256(original.encode()).hexdigest(),
               'derived_sha256':hashlib.sha256(body.encode()).hexdigest(),'changes':changes}
        out[name]=license+'\n\n'.join(bodies)+'\n'
    return {name:text.encode() for name,text in out.items()},records
