"""Qualify immutable catalog inputs against the actual native calibration.

No stock gain or synthesized template is adopted as a safe limit. Input evidence
is checked by hash; the tool validates review identity, not physical safety.
The default catalog has no effects/output and no measured bandwidth claim.
"""
from pathlib import Path
import hashlib,json,re
FIRMWARE='84598d2a5130f7fe6bccba5cfbcafa1341d2d5bfe2f7d1361142cb78783ce79d'
def read(path):
    def pairs(rows):
        result={}
        for key,value in rows:
            if key in result:raise ValueError('Duplicate native-input key: '+key)
            result[key]=value
        return result
    return json.loads(Path(path).read_text(),object_pairs_hook=pairs)
def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def integer(v,lo,hi):
    if type(v) is not int or not lo<=v<=hi:raise ValueError('Invalid native-input integer')
    return v
def digest(v):
    if not isinstance(v,str) or not re.fullmatch('[0-9a-f]{64}',v):raise ValueError('Invalid identity hash')
    return v
def pair(values,lo,hi):
    if not isinstance(values,list) or len(values)!=2:raise ValueError('Two motor limits required')
    return [integer(v,lo,hi) for v in values]
def shared_header(r):
    s=r['shared_budget'];window=integer(s['window_us'],125,10000000)
    if window%125:raise ValueError('Shared budget must align to three24k samples')
    on=pair(s['max_on_us'],1,window);dual=integer(s['max_dual_on_us'],1,window)
    maximum=16384*16129*(window*24000//1000000)
    square=pair(s['max_squares'],1,maximum)
    return ('static const EldenSharedBudgetLimits elden_haptics_shared_limits={'+str(window)+',{'+','.join(map(str,on))+'},'+str(dual)+',{' +
            ','.join(str(v)+'ULL' for v in square)+'},3};\n'),s
def catalog_header(path,calibration_path):
    prefix='#pragma once\n#include "Catalog.h"\n'
    if path is None:
        return prefix+('#define ELDEN_HAPTICS_HAS_CATALOG 0\n#define ELDEN_HAPTICS_EFFECT_COUNT 0\n'
          'static const EldenHapticsEffect *const elden_haptics_effects=nullptr;\n'
          'static const EldenRtpLimits elden_haptics_rtp_limits={};\n'),None
    if calibration_path is None:raise ValueError('Native calibration required before catalog')
    path=Path(path).resolve(strict=True);calibration_path=Path(calibration_path).resolve(strict=True)
    r=read(path);cal=read(calibration_path)
    if r.get('schema')!=1 or r.get('board')!='TB322FC' or r.get('firmware_sha256')!=FIRMWARE or \
       r.get('native_calibration_sha256')!=sha(calibration_path):raise ValueError('Catalog/native calibration identity differs')
    if type(r.get('hardware_qualified')) is not bool or not r['hardware_qualified']:raise ValueError('Unmeasured catalog refused')
    if not r.get('evidence') or not r.get('review_notes'):raise ValueError('Actual catalog review/evidence required')
    evidence=[]
    for item in r['evidence']:
        p=(path.parent/item['path']).resolve(strict=True)
        if sha(p)!=digest(item['sha256']):raise ValueError('Catalog evidence changed')
        evidence.append({'sha256':item['sha256'],'bytes':p.stat().st_size})
    rtp=cal['rtp'];shared=cal['shared_budget'];window=integer(rtp['window_frames'],1,240000)
    if window!=shared['window_us']*24000//1000000 or shared['window_us']%125:raise ValueError('RAM/RTP moving windows differ')
    duration=integer(rtp['max_duration_us'],1,60000000);gap=integer(rtp['max_service_gap_us'],1,10000)
    bandwidth=integer(rtp['measured_min_payload_bps'],30000,1000000)
    bus=integer(cal['bus_hz'],100000,1000000)
    if bus not in (100000,400000,1000000) or rtp['measured_bus_hz']!=bus:raise ValueError('Measured bus profile differs')
    if not rtp.get('bandwidth_evidence_sha256') or rtp['bandwidth_evidence_sha256'] not in [x['sha256'] for x in evidence]:
        raise ValueError('Actual complete-path bandwidth evidence missing')
    gains=pair(cal['limits']['max_gain'],1,128);peak=pair(rtp['max_sample'],1,127)
    squares=pair(rtp['max_window_squares'],1,(1<<63)-1);on=pair(rtp['max_window_on_frames'],1,window)
    max_sum=integer(rtp['max_sum_gain'],1,256)
    if max_sum>cal['limits']['max_sum_gain']:raise ValueError('Catalog combined gain exceeds native limits')
    for motor in range(2):
        if squares[motor]>shared['max_squares'][motor] or (on[motor]*1000000+23999)//24000>shared['max_on_us'][motor]:
            raise ValueError('RTP limits exceed shared physical budget')
    entries=r['effects']
    if not isinstance(entries,list) or not 1<=len(entries)<=512:raise ValueError('Empty/oversized native effect catalog')
    lines=[prefix,'#define ELDEN_HAPTICS_HAS_CATALOG 1','#define ELDEN_HAPTICS_EFFECT_COUNT '+str(len(entries))]
    rows=[];records=[];previous=0;total=0
    for entry in entries:
        id=integer(entry['id'],1,0xffffffff)
        if id<=previous:raise ValueError('Catalog IDs must strictly increase')
        previous=id;manifest=(path.parent/entry['manifest']).resolve(strict=True);digest(entry['manifest_sha256'])
        if sha(manifest)!=entry['manifest_sha256']:raise ValueError('Compiled effect manifest changed')
        m=read(manifest);source=digest(m['source_sha256']);bank=digest(m['bank_sha256'])
        for field,source_file in [('compiler_source_sha256','EffectCompiler.py'),('sample_map_source_sha256','SampleMap.c'),('parser_source_sha256','AppleEffects.py')]:
            if m.get(field)!=sha(Path(__file__).resolve().parent/source_file):raise ValueError('Compiled effect generator changed: '+field)
        if type(m.get('hardware_qualified')) is not bool or not m['hardware_qualified']:raise ValueError('Synthetic compiled effect refused')
        if m.get('rate_hz')!=24000 or len(m.get('channels',[]))!=2:raise ValueError('Effect sample/channel format differs')
        frames=integer(m['frames'],1,1440000)
        if (frames*1000000+23999)//24000>duration:raise ValueError('Effect exceeds native duration')
        mask=integer(entry.get('mask',3),1,3);gain=[0,0];arrays=['nullptr','nullptr'];cost=[]
        name=entry['name']
        if not isinstance(name,str) or not re.fullmatch('[A-Za-z0-9_. -]{1,127}',name):raise ValueError('Invalid effect name')
        # The reviewed catalog must bind the originating source and bank,
        # not accept a renamed test output merely from a boolean manifest.
        source_file=(path.parent/entry['source_path']).resolve(strict=True);bank_file=(path.parent/entry['bank_path']).resolve(strict=True)
        if sha(source_file)!=source or sha(bank_file)!=bank:raise ValueError('Original source/template bank changed')
        from EffectCompiler import load_bank
        measured_bank=load_bank(bank_file)
        if measured_bank['firmware_sha256']!=FIRMWARE:raise ValueError('Waveform bank firmware differs')
        mapper=(path.parent/entry['sample_mapper_library']).resolve(strict=True)
        if sha(mapper)!=m.get('sample_mapper_sha256'):raise ValueError('Actual signed-sample mapper library changed')
        from AppleEffects import vibration,ahap
        from EffectCompiler import compile_vibration,compile_ahap
        if source_file.suffix.lower()=='.ahap':replay,replay_pcm=compile_ahap(ahap(source_file.read_bytes()),measured_bank,mapper)
        else:replay,replay_pcm=compile_vibration(m['recipe'],vibration(source_file.read_bytes()),measured_bank,mapper)
        if replay['frames']!=frames or replay['bank_sha256']!=bank:raise ValueError('Compiled source replay differs')
        for motor in range(2):
            if not mask&(1<<motor):continue
            data_path=manifest.parent/('motor'+str(motor)+'.rtp');data=data_path.read_bytes();ch=m['channels'][motor]
            gain[motor]=integer(ch['gain'],1,gains[motor])
            if gain[motor]!=measured_bank['channels'][motor]['gain']:raise ValueError('Compiled gain differs from measured template bank')
            if len(data)!=frames or hashlib.sha256(data).hexdigest()!=digest(ch['pcm_sha256']):raise ValueError('Effect PCM changed')
            if data!=replay_pcm[motor]:raise ValueError('Effect PCM differs from actual source/compiler replay')
            rolling=maximum=running_on=max_on=0
            for i,byte in enumerate(data):
                value=byte-256 if byte>127 else byte
                if abs(value)>peak[motor]:raise ValueError('Catalog PCM exceeds peak')
                rolling+=value*value*gain[motor]*gain[motor];running_on+=bool(value)
                if i>=window:
                    old=data[i-window];old=old-256 if old>127 else old
                    rolling-=old*old*gain[motor]*gain[motor];running_on-=bool(old)
                maximum=max(maximum,rolling);max_on=max(max_on,running_on)
            motor_on=min(frames,window)
            if maximum>squares[motor] or motor_on>on[motor]:raise ValueError('Effect exceeds calibrated moving window')
            symbol='elden_haptics_effect_'+str(id)+'_'+str(motor);arrays[motor]=symbol
            values=','.join(str(byte-256 if byte>127 else byte) for byte in data)
            lines.append('static const int8_t '+symbol+'[]={'+values+'};');total+=len(data)
            cost.append({'motor':motor,'pcm_sha256':ch['pcm_sha256'],'max_window_squares':maximum,'max_window_on_frames':motor_on,
                         'nonzero_sample_frames':max_on})
        if total>32*1024*1024 or sum(gain)>max_sum or bandwidth<24000*mask.bit_count()*5//4:raise ValueError('Catalog storage/gain/bandwidth exceeded')
        rows.append('{'+','.join(map(str,[id,frames,mask]))+',{'+','.join(map(str,gain))+'},{'+','.join(arrays)+'},'+json.dumps(name)+','+json.dumps(source)+','+json.dumps(bank)+'}')
        records.append({'id':id,'name':name,'manifest_sha256':entry['manifest_sha256'],'source_sha256':source,'bank_sha256':bank,'frames':frames,'mask':mask,'costs':cost})
    lines.append('static const EldenHapticsEffect elden_haptics_effects[]={'+','.join(rows)+'};')
    lines.append('static const EldenRtpLimits elden_haptics_rtp_limits={'+','.join(map(str,[duration,gap,bandwidth,window]))+',{'+
                 ','.join(str(v)+'ULL' for v in squares)+'},{'+','.join(map(str,on))+'},'+str(max_sum)+',{'+','.join(map(str,gains))+'},{'+','.join(map(str,peak))+'},3};')
    return '\n'.join(lines)+'\n',{'input_sha256':sha(path),'native_calibration_sha256':sha(calibration_path),'evidence':evidence,
        'effects':records,'PCM_bytes':total,'measured_by_this_tool':False,'review_notes':r['review_notes']}
