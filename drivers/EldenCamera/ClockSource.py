"""Derive exact Gen4 camera clock graph and original RCG/branch/GDSC helpers."""
from pathlib import Path
from fractions import Fraction
import hashlib,json,re
from PowerSource import function
HERE=Path(__file__).resolve().parent

def block(text,signature):
    start=text.index(signature);at=text.index('{',start);end=at+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]

def field(text,name,default=None):
    m=re.search(r'\.'+re.escape(name)+r'\s*=\s*(0x[0-9a-f]+|\d+)\b',text)
    if m:return int(m[1],0)
    if default is not None:return default
    raise ValueError('Missing original field: '+name)

def graph(files):
    soc=files['sm8750.dtsi'];camss=block(soc,'camss: isp@ad27000')
    refs=re.findall(r'<&(camcc|gcc)\s+([A-Z0-9_]+)>',re.search(r'\bclocks\s*=\s*(.*?);',camss,re.S)[1])
    names=re.findall(r'"([^"]+)"',re.search(r'\bclock-names\s*=\s*(.*?);',camss,re.S)[1])
    if len(refs)!=len(names) or len(names)!=35:raise ValueError('Exact Gen4 all35 CAMSS clock names differ')
    # Add exact CCI0/1 and sensor MCLK3/4. All original receiver-clock aliases
    # remain represented; only explicitly selected clocks can be acquired.
    refs += [('camcc','CAM_CC_CCI_0_CLK'),('camcc','CAM_CC_CCI_1_CLK'),('cambistmclkcc','CAM_BIST_MCLK_CC_MCLK3_CLK'),('cambistmclkcc','CAM_BIST_MCLK_CC_MCLK4_CLK')]
    names += ['cci0','cci1','mclk3','mclk4'];roots=[];branches=[];clockids=[];all_siblings=[]
    key_to_root={};key_to_branch={}
    for controller in ('camcc','cambistmclkcc'):
        source=files[controller+'-sm8750.c']
        for m in re.finditer(r'static struct clk_branch\s+(\w+)\s*=\s*\{',source):
            b=block(source,m[0]);parent=re.search(r'&(\w+)\.clkr\.hw',b)
            if parent:all_siblings.append({'controller':controller,'name':m[1],'root_name':parent[1],'reg':field(b,'enable_reg'),'mask':1})
    for name,(controller,constant) in zip(names,refs):
        if controller=='gcc':
            clockids.append({'name':name,'constant':constant,'controller':controller,'branch':None,'root':None});continue
        source=files[controller+'-sm8750.c']
        aliases=dict(re.findall(r'\[([A-Z0-9_]+)\]\s*=\s*&(\w+)\.clkr',source))
        if constant not in aliases:raise ValueError('Actual clock alias missing: '+constant)
        branch_name=aliases[constant];b=block(source,'static struct clk_branch '+branch_name+' =')
        if '.halt_check = BRANCH_HALT' not in b or '.enable_mask = BIT(0)' not in b or '.ops = &clk_branch2_ops' not in b:
            raise ValueError('Unhandled clock gate semantics: '+branch_name)
        reg=field(b,'enable_reg');halt=field(b,'halt_reg')
        parent=re.search(r'&(\w+)\.clkr\.hw',b)
        root=None;root_name=None
        if parent:
            root_name=parent[1];key=(controller,root_name)
            if key not in key_to_root:
                rcg=block(source,'static struct clk_rcg2 '+root_name+' =')
                if '.ops = &clk_rcg2_shared_ops' not in rcg:raise ValueError('Nonshared camera RCG: '+root_name)
                pmap_name=re.search(r'\.parent_map\s*=\s*(\w+)',rcg)[1]
                pmap=block(source,'static const struct parent_map '+pmap_name+'[] =')
                parents=[(token,int(cfg,0)) for token,cfg in re.findall(r'\{\s*(P_[A-Z0-9_]+),\s*(0x[0-9a-f]+|\d+)\s*\}',pmap)]
                freq_name=re.search(r'\.freq_tbl\s*=\s*(\w+)',rcg)[1]
                tbl=block(source,'static const struct freq_tbl '+freq_name+'[] =')
                freqs=[]
                for freq,src,div,m,n in re.findall(r'F\((\d+),\s*(P_[A-Z0-9_]+),\s*([0-9.]+),\s*(\d+),\s*(\d+)\)',tbl):
                    selector=next(cfg for token,cfg in parents if token==src);hid=Fraction(div)*2-1
                    if hid.denominator!=1:raise ValueError('Nonexact HID divider')
                    freqs.append({'hz':int(freq),'selector':selector,'hid':int(hid),'m':int(m),'n':int(n),'source':src})
                if not freqs:raise ValueError('Missing actual frequency table: '+root_name)
                root=len(roots);key_to_root[key]=root
                roots.append({'name':root_name,'controller':controller,'reg':field(rcg,'cmd_rcgr'),
                    'mnd_width':field(rcg,'mnd_width'),'hid_width':field(rcg,'hid_width'),'safe_selector':field(rcg,'safe_src_index',0),
                    'parents':parents,'frequencies':freqs,
                    'siblings':[v for v in all_siblings if v['controller']==controller and v['root_name']==root_name]})
            root=key_to_root[key]
        else:
            # Original branch may have an external interface/no local RCG.
            # The real original dependency provider must supply/hold its clock.
            if '.parent_data' not in b and '.parent_hws' not in b:raise ValueError('Undeclared original clock parent: '+branch_name)
        key=(controller,branch_name)
        if key not in key_to_branch:
            key_to_branch[key]=len(branches);branches.append({'name':branch_name,'controller':controller,'reg':reg,'halt':halt,'root':root,
                'hwcg_reg':field(b,'hwcg_reg',0),'hwcg_bit':field(b,'hwcg_bit',0)})
        clockids.append({'name':name,'constant':constant,'controller':controller,'branch':key_to_branch[key],'root':root})
    domains=[]
    for name in ('cam_cc_titan_top_gdsc','cam_cc_tfe_0_gdsc','cam_cc_tfe_1_gdsc','cam_cc_tfe_2_gdsc'):
        b=block(files['camcc-sm8750.c'],'static struct gdsc '+name+' =')
        if '.flags = POLL_CFG_GDSCR | RETAIN_FF_ENABLE' not in b or '.pwrsts = PWRSTS_OFF_ON' not in b:raise ValueError('Actual domain semantics differ')
        domains.append({'name':name,'reg':field(b,'gdscr'),'wait_value':(field(b,'en_rest_wait_val')<<20)|(field(b,'en_few_wait_val')<<16)|(field(b,'clk_dis_wait_val')<<12)})
    def entries(text,name):
        array=block(text,name);start=array.index('{')+1;out=[];level=0;at=start
        for i in range(start,len(array)-1):
            if array[i]=='{':
                if level==0:at=i
                level+=1
            elif array[i]=='}':
                level-=1
                if level==0:out.append(array[at:i+1])
        return out
    source=files['camss.c'];plans={}
    for kind,name in [('phy','csiphy_res_8750'),('csid','csid_res_8750'),('vfe','vfe_res_8750')]:
        plans[kind]=[]
        for entry in entries(source,'static const struct camss_subdev_resources '+name+'[] ='):
            clocks=re.findall(r'"([^"]+)"',re.search(r'\.clock\s*=\s*\{(.*?)\}',entry,re.S)[1])
            tables=re.search(r'\.clock_rate\s*=\s*\{(.*?)\},\s*\.reg',entry,re.S)[1]
            allowed=[[int(v) for v in re.findall(r'\d+',body)] for body in re.findall(r'\{([^{}]+)\}',tables)]
            if len(clocks)!=len(allowed):raise ValueError('Actual CAMSS rate/clock table differs: '+kind)
            plans[kind].append([{'clock':names.index(clock),'name':clock,'allowed_hz':rates} for clock,rates in zip(clocks,allowed)])
    return {'clock_ids':clockids,'roots':roots,'branches':branches,'domains':domains,'CAMSS_clock_plans':plans,'hardware':False}

def topology_header(g):
    # Stable ABI uses source DT clock names rather than invented magic IDs.
    def enum_name(name):return 'EldenCamClock'+''.join(p.capitalize() for p in name.split('_'))
    header='/* SPDX-License-Identifier: GPL-2.0-only */\n#ifndef ELDEN_CAMERA_CLOCK_IDS_H\n#define ELDEN_CAMERA_CLOCK_IDS_H\n'
    header+='enum EldenCamClockId {\n'+''.join('    '+enum_name(r['name'])+'='+str(i)+',\n' for i,r in enumerate(g['clock_ids']))+'    EldenCamClockCount='+str(len(g['clock_ids']))+'\n};\n#endif\n'
    return header.encode()

def topology(g):
    ctl={'camcc':0,'cambistmclkcc':1,'gcc':2}
    lines=['/* SPDX-License-Identifier: GPL-2.0-only */','/* Exact frozen Gen4 clock graph, no parent/rate readiness defaults. */']
    lines+=['static const struct root_spec root_specs[] = {']
    for r in g['roots']:
        lines.append(' {%du,0x%x,%du,%du,%du}, /*%s*/'%(ctl[r['controller']],r['reg'],r['mnd_width'],r['hid_width'],r['safe_selector'],r['name']))
    lines+=['};','static const struct branch_spec branch_specs[] = {']
    for b in g['branches']:
        lines.append(' {%du,0x%x,0x%x,%d,%du,%du}, /*%s*/'%(ctl[b['controller']],b['reg'],b['halt'],-1 if b['root'] is None else b['root'],b['hwcg_reg'],b['hwcg_bit'],b['name']))
    lines+=['};','static const struct clock_spec clock_specs[] = {']
    for r in g['clock_ids']:lines.append(' {%d}, /*%s*/'%(-1 if r['branch'] is None else r['branch'],r['name']))
    lines+=['};','static const struct frequency_spec frequencies[] = {']
    for i,r in enumerate(g['roots']):
        for f in r['frequencies']:lines.append(' {%du,%du,%du,%du,%du,%du},'%(i,f['hz'],f['selector'],f['hid'],f['m'],f['n']))
    lines+=['};','static const struct domain_spec domain_specs[] = {']
    for d in g['domains']:lines.append(' {0x%x,0x%x},'%(d['reg'],d['wait_value']))
    lines+=['};']
    return ('\n'.join(lines)+'\n').encode()

def board_plan(g):
    lines=['/* SPDX-License-Identifier: GPL-2.0-only */','/* Exact original CAMSS per-PHY/CSID/VFE clock/rate lists. */']
    for kind,rows in g['CAMSS_clock_plans'].items():
        for index,clocks in enumerate(rows):
            lines.append('static const struct board_clock_constraint '+kind+'_'+str(index)+'[] = {')
            for row in clocks:
                rates=row['allowed_hz'];lines.append(' {%du,%du,{%s}}, /*%s*/'%(row['clock'],len(rates),','.join(str(v)+'u' for v in rates),row['name']))
            lines.append('};')
        lines.append('static const struct board_clock_list '+kind+'_plans[] = {'+','.join('{'+kind+'_'+str(i)+',ARRAY_SIZE('+kind+'_'+str(i)+')}' for i in range(len(rows)))+'};')
    return ('\n'.join(lines)+'\n').encode()

def derive():
    spec=json.loads((HERE/'clock-source.json').read_text());files={}
    for row in spec['files']:
        body=(HERE/'upstream'/row['file']).read_bytes()
        if len(body)!=row['bytes'] or hashlib.sha256(body).hexdigest()!=row['sha256']:raise ValueError('Frozen camera clock source changed: '+row['file'])
        files[row['file']]=body.decode().replace('\r\n','\n')
    funcs=[];records=[]
    selections={'clk-rcg2.c':['static int clk_rcg2_is_enabled(','static int update_config(','static int __clk_rcg2_configure_parent(',
        'static int __clk_rcg2_configure_mnd(','static int __clk_rcg2_configure(','static int clk_rcg2_set_force_enable(',
        'static int clk_rcg2_clear_force_enable(','static int clk_rcg2_shared_enable(','static void clk_rcg2_shared_disable('],
        'common.c':['int qcom_find_src_index('],
        'clk-regmap.c':['int clk_enable_regmap(','void clk_disable_regmap('],
        'clk-branch.c':['static bool clk_branch_in_hwcg_mode(','static bool clk_branch2_check_halt(','static int clk_branch_wait(',
            'static int clk_branch_toggle(','static int clk_branch2_enable(','static void clk_branch2_disable('],
        'gdsc.c':['static int gdsc_check_status(','static int gdsc_update_collapse_bit(','static void gdsc_retain_ff_on('],
        'pinctrl-msm.c':['static int msm_gpio_direction_output(','static int msm_gpio_set(']}
    for file,signatures in selections.items():
        for signature in signatures:
            original=function(files[file],signature);body=original
            if signature.startswith(('int qcom_','int clk_','void clk_')):body='static '+body
            delay_context='rcg->clkr.regmap->core' if file=='clk-rcg2.c' else 'br->clkr.regmap->core' if file=='clk-branch.c' else None
            if delay_context:body=body.replace('udelay(', 'delay_core('+delay_context+',')
            funcs.append(body);records.append({'file':file,'signature':signature,'original_sha256':hashlib.sha256(original.encode()).hexdigest(),
                'derived_sha256':hashlib.sha256(body.encode()).hexdigest(),'visibility_or_context_bound_delay_only':body!=original})
    g=graph(files)
    return {'ClockIds.h':topology_header(g),'ClockTopology.inc':topology(g),
        'ClockBoard.inc':board_plan(g),
        'ClockOriginal.inc':('/* SPDX-License-Identifier: GPL-2.0-only */\n/* Original bodies use actual admitted/latched-error regmap callbacks. */\n'+'\n\n'.join(funcs)+'\n').encode()},g,records
