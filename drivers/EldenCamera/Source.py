"""Reuse exact Gen4 sensor tables and power/control/stream bodies with a real
owned-Cci/power/receiver adapter. No old-generation board or fake camera frames.
"""
from pathlib import Path
import hashlib,json,re
HERE=Path(__file__).resolve().parent
def function(text,signature):
    at=text.index(signature);end=text.index('{',at)+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[at:end]
def sources():
    spec=json.loads((HERE/'source.json').read_text());texts={};tables=[];powers=[];controls=[];streams=[];metadata={}
    for row in spec['sources']:
        raw=(HERE/'upstream'/row['file']).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=row['sha256']:raise ValueError('Fixed sensor source changed')
        text=raw.decode();name=Path(row['file']).stem;texts[name]=text
        arrays=re.findall(r'static const struct cci_reg_sequence\s+\w+\[\]\s*=\s*\{.*?\n\};',text,re.S)
        if len(arrays)!=(3 if name=='s5kjns' else 2):raise ValueError('Sensor table count changed')
        tables.extend(arrays)
        for suffix in ('power_on','power_off'):powers.append(function(text,'static int '+name+'_'+suffix+'('))
        controls.append(function(text,'static int '+name+'_set_ctrl('));streams.append(function(text,'static int '+name+'_s_stream('))
        definitions=[line for line in text.splitlines() if line.startswith('#define '+name.upper()+'_') and '_CODE ' not in line]
        metadata[name]={'tables':len(arrays),'table_entries':sum(a.count('CCI_REG') for a in arrays),
            'body_sha256':{s:hashlib.sha256(function(text,'static int '+name+'_'+s+'(').encode()).hexdigest() for s in ['power_on','power_off','set_ctrl','s_stream']}}
        metadata[name]['definitions']=definitions
        # Probe identities and control limits come from the actual source,
        # never from the reset register value or the device-tree label alone.
        expected='if (id != 0x38ee)' if name=='s5kjns' else 'if (id != 0x08a8)'
        if expected not in text:raise ValueError('Source sensor identity changed')
        gain=re.search(r'V4L2_CID_ANALOGUE_GAIN,\s*(\d+),\s*(\d+),\s*1,\s*(\d+)\)',text)
        if not gain:raise ValueError('Source gain range changed')
        metadata[name]['gain']=list(map(int,gain.groups()))
    definitions='\n'.join(d for r in metadata.values() for d in r['definitions'])
    support='''
static int pm_runtime_resume_and_get(struct device *dev){
    struct CameraInternal *c=to_s5kjns(dev_get_drvdata(dev))->regmap->core;
    if(!c->power_valid){int rc=c->target?gc08a8_power_on(dev):s5kjns_power_on(dev);if(!rc)rc=c->error;
        if(rc)return rc;c->power_valid=1;}
    c->references++;return 0;
}
static int camera_power_off(struct CameraInternal *c){
    if(c->receiver_leased||c->go_attempted){int rc=camera_admit(c,EldenCameraCleanup);
        if(!rc)rc=c->io.receiver_drain(c->io.context,c->target);if(rc)return camera_error(c,rc);c->receiver_leased=0;}
    int rc=c->target?gc08a8_power_off(&c->device):s5kjns_power_off(&c->device);
    if(!rc)rc=c->error;if(!rc){c->power_valid=0;c->go_attempted=0;}return rc;
}
static int pm_runtime_put_sync(struct device *dev){
    struct CameraInternal *c=to_s5kjns(dev_get_drvdata(dev))->regmap->core;
    if(!c->references)return camera_error(c,-71);c->references--;
    if(c->references)return 0;uint8_t prior=c->cleanup;c->cleanup=1;
    int rc=camera_power_off(c);c->cleanup=prior;return rc;
}
static int __v4l2_ctrl_handler_setup(struct v4l2_ctrl_handler *handler){
    struct CameraInternal *c=handler->core;struct v4l2_ctrl ctrl={handler,V4L2_CID_EXPOSURE,c->exposure};
    int rc=c->target?gc08a8_set_ctrl(&ctrl):s5kjns_set_ctrl(&ctrl);if(rc)return rc;
    ctrl.id=V4L2_CID_ANALOGUE_GAIN;ctrl.val=c->gain;rc=c->target?gc08a8_set_ctrl(&ctrl):s5kjns_set_ctrl(&ctrl);
    if(!rc&&!c->target){ctrl.id=V4L2_CID_TEST_PATTERN;ctrl.val=0;rc=s5kjns_set_ctrl(&ctrl);}return rc;
}
'''
    code='/* SPDX-License-Identifier: GPL-2.0-only */\n#include "CoreShim.h"\n'+definitions+'\n'+'\n'.join(tables+powers+controls)+'\n'+support+'\n'+'\n'.join(streams)+'\n'+(HERE/'CorePublic.inc').read_text()
    return code.encode(),metadata
