"""Compile original Apple recipes through an explicit source-bound template bank.

Templates and drive calibration are externally measured/qualified inputs. There
is no inferred Tap/Sine carrier, waveform-type fallback or intensity->voltage.
Unknown types/envelope policies reject. Original Apple assets stay external.
"""
from array import array
from decimal import Decimal, ROUND_HALF_UP
from pathlib import Path
import ctypes, hashlib, json, sys
from AppleEffects import q15
RATE=24000
MAX_FRAMES=60*RATE
def integer(value,low,high):
    if type(value) is not int or not low<=value<=high:raise ValueError('Invalid integer limit')
    return value
def frame(us):return (integer(us,0,60000000)*RATE+500000)//1000000
def multiply(a,b):return (a*b+16383)//32767
class GainPoint(ctypes.Structure):_fields_=[('magnitude_q15',ctypes.c_uint16),('drive_q15',ctypes.c_uint16)]
class GainMap(ctypes.Structure):
    _fields_=[('points',ctypes.POINTER(GainPoint)),('count',ctypes.c_uint16),('peak',ctypes.c_uint16),
              ('rate',ctypes.c_uint32),('max_frames',ctypes.c_uint32),('squares',ctypes.c_uint64)]
def load_bank(path,allow_test=False):
    def pairs(rows):
        result={}
        for key,value in rows:
            if key in result:raise ValueError('Duplicate template-bank key')
            result[key]=value
        return result
    path=Path(path).resolve(strict=True);data=json.loads(path.read_text(),object_pairs_hook=pairs);base=path.parent
    if data.get('schema')!=1 or data.get('rate_hz')!=RATE or data.get('board')!='TB322FC' or data.get('boost_mv')!=7000 or \
       data.get('firmware_sha256')!='84598d2a5130f7fe6bccba5cfbcafa1341d2d5bfe2f7d1361142cb78783ce79d':
        raise ValueError('Wrong template/calibration board/rate/boost')
    if type(data.get('hardware_qualified')) is not bool:raise ValueError('Invalid qualification field')
    if not data.get('hardware_qualified') and not allow_test:raise ValueError('Measured template bank required for delivery')
    if data.get('hardware_qualified'):
        if not data.get('evidence') or not data.get('review_notes'):raise ValueError('Missing measured evidence/review')
        for row in data['evidence']:
            p=(base/row['path']).resolve(strict=True)
            if hashlib.sha256(p.read_bytes()).hexdigest()!=row['sha256']:raise ValueError('Calibration evidence changed')
    templates={}
    for name,row in data['templates'].items():
        p=(base/row['path']).resolve(strict=True);raw=p.read_bytes()
        if row.get('format')!='s16le' or len(raw)%2 or hashlib.sha256(raw).hexdigest()!=row['sha256']:raise ValueError('Template identity/format differs')
        samples=array('h');samples.frombytes(raw)
        if sys.byteorder!='little':samples.byteswap()
        if not samples or len(samples)>RATE or any(v==-32768 for v in samples):raise ValueError('Template bounds differ')
        loop=integer(row['loop_frames'],0,len(samples));tail=integer(row['tail_frames'],0,len(samples))
        if loop and loop+tail!=len(samples):raise ValueError('Explicit loop/tail layout required')
        if row['envelope_mode'] not in ('seconds','normalized'):raise ValueError('Unknown envelope units')
        templates[name]={**row,'samples':samples}
    if len(data['channels'])!=2:raise ValueError('Per-motor calibration required')
    for channel in data['channels']:
        integer(channel['gain'],1,128);integer(channel['drive_peak_q15'],1,32767)
        points=channel['points']
        if not 2<=len(points)<=256 or points[0]!=[0,0] or points[-1][0]!=32767:raise ValueError('LUT bounds')
        previous=(-1,-1)
        for x,y in points:
            integer(x,0,32767);integer(y,0,channel['drive_peak_q15'])
            if x<=previous[0] or y<previous[1]:raise ValueError('Nonmonotonic measured LUT')
            previous=(x,y)
        integer(channel['window_frames'],1,240000);integer(channel['max_window_squares'],1,(1<<63)-1)
        integer(channel['max_window_on_frames'],1,channel['window_frames'])
    return {**data,'templates':templates,'bank_sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
def envelope(points,at,duration,mode):
    if not points:return 32767
    parsed=[]
    for row in points:
        t=Decimal(row['time_raw'])
        us=int((t*(duration if mode=='normalized' else 1000000)).quantize(Decimal(1),rounding=ROUND_HALF_UP))
        parsed.append((us,row['level_q15']))
    if any(a[0]>=b[0] for a,b in zip(parsed,parsed[1:])):raise ValueError('Envelope time order')
    if at<=parsed[0][0]:return parsed[0][1]
    for (ta,va),(tb,vb) in zip(parsed,parsed[1:]):
        if at<=tb:return va+(vb-va)*(at-ta)//(tb-ta)
    return parsed[-1][1]
def carrier(template,index,total):
    pcm=template['samples'];loop=template['loop_frames'];tail=template['tail_frames']
    if loop:
        if tail and index>=total-tail:return pcm[loop+index-(total-tail)]
        return pcm[index%loop]
    if total!=len(pcm):raise ValueError('One-shot template duration changed without a qualified loop')
    return pcm[index]
def render_atoms(recipe,bank):
    atoms=[];end=0
    for atom in recipe['atoms']:
        kind=atom['kind']
        if kind=='Silence':end=max(end,atom['time_us']+atom['duration_us']);continue
        if kind not in bank['templates']:raise ValueError('No qualified template for '+kind)
        template=bank['templates'][kind]
        # Frequency/StartingGain are not discarded or guessed. A template bank
        # must explicitly bind the supported source values to this waveform.
        for key in ('frequency_hz','starting_gain_q15'):
            if key in atom and str(template.get(key))!=str(atom[key]):raise ValueError('Template '+key+' does not match source')
        duration=atom['duration_us'] or (len(template['samples'])*1000000+RATE-1)//RATE
        count=frame(duration);period=atom.get('period_us',duration);cycles=Decimal(atom.get('cycles','1'))
        if cycles!=cycles.to_integral_value() or not 0<=cycles<=10000:raise ValueError('Unqualified fractional cycle count')
        if cycles>1 and not period:raise ValueError('Zero repeat period')
        for repeat in range(int(cycles)):
            start=atom['time_us']+repeat*period
            if start+duration>60000000:raise ValueError('Pattern exceeds60s')
            atoms.append((atom,template,frame(start),count,duration));end=max(end,start+duration)
    result=[0]*frame(end)
    for atom,template,start,count,duration in atoms:
        level=multiply(atom['intensity_q15'],atom['master_q15'])
        for i in range(count):
            env=envelope(atom.get('envelope'),i*1000000//RATE,duration,template['envelope_mode'])
            value=carrier(template,i,count);gain=multiply(level,env)
            scaled=(abs(value)*gain+16383)//32767;result[start+i]+= -scaled if value<0 else scaled
    if any(abs(v)>32767 for v in result):raise ValueError('Source mixture clips; no automatic amplification/limiter')
    return array('h',result)
def render_vibration(name,parsed,bank):
    if parsed.get('format')!='Apple SystemSoundVibrationPatterns' or name not in parsed['recipes']:raise ValueError('Unknown original recipe')
    recipe=parsed['recipes'][name]
    if recipe['kind']=='empty':return array('h')
    if recipe['kind']=='atoms':return render_atoms(recipe,bank)
    carrier_name=bank.get('legacy_on_template')
    if carrier_name not in bank['templates']:raise ValueError('No qualified legacy carrier')
    segments=[]
    if recipe['kind']=='on-off':segments=recipe['segments']
    elif recipe['kind']=='repeating-on-off':
        period=recipe['on_us']+recipe['off_us'];time=0
        while time<recipe['total_us']:
            # Actual source ordering is a profile fact, not inferred from names.
            if bank.get('legacy_repeat_order')!='on-off':raise ValueError('Repeating source order unqualified')
            duration=min(recipe['on_us'],recipe['total_us']-time);segments.append({'on':True,'time_us':time,'duration_us':duration})
            time+=period
    else:raise ValueError('Unsupported original recipe form')
    atoms=[{'kind':carrier_name,'time_us':s['time_us'],'duration_us':s['duration_us'],'intensity_q15':recipe['intensity_q15'],
            'master_q15':32767} for s in segments if s['on'] and s['duration_us']]
    pcm=render_atoms({'atoms':atoms},bank)
    end=max((s['time_us']+s['duration_us'] for s in segments),default=0)
    if recipe['kind']=='repeating-on-off':end=recipe['total_us']
    while len(pcm)<frame(end):pcm.append(0)
    return pcm
def automation(parsed,name,time_us,default):
    value=default
    for item in sorted(parsed['automation'],key=lambda x:x['time_us']):
        if item['parameter']!=name or time_us<item['time_us']:continue
        if item['kind']=='parameter':value=item['value_q15'];continue
        at=time_us-item['time_us'];points=item['points']
        if at<=points[0]['time_us']:value=points[0]['value_q15'];continue
        value=points[-1]['value_q15']
        for a,b in zip(points,points[1:]):
            if at<=b['time_us']:
                value=a['value_q15']+(b['value_q15']-a['value_q15'])*(at-a['time_us'])//(b['time_us']-a['time_us']);break
    return value
def sharp_carrier(bank,kind,sharp,index,total):
    mapping=bank.get('ahap_templates',{}).get(kind)
    if not mapping or len(mapping)<2:raise ValueError('No calibrated AHAP sharpness bank')
    if mapping[0]['sharpness_q15']!=0 or mapping[-1]['sharpness_q15']!=32767:raise ValueError('Sharpness endpoints missing')
    if any(a['sharpness_q15']>=b['sharpness_q15'] for a,b in zip(mapping,mapping[1:])):raise ValueError('Sharpness order')
    for a,b in zip(mapping,mapping[1:]):
        if sharp<=b['sharpness_q15']:
            ta=bank['templates'][a['template']];tb=bank['templates'][b['template']]
            if (len(ta['samples']),ta['loop_frames'],ta['tail_frames'])!=(len(tb['samples']),tb['loop_frames'],tb['tail_frames']):
                raise ValueError('Unqualified unequal sharpness morph')
            va=carrier(ta,index,total);vb=carrier(tb,index,total)
            return va+(vb-va)*(sharp-a['sharpness_q15'])//(b['sharpness_q15']-a['sharpness_q15'])
    raise ValueError('Sharpness range')
def render_ahap(parsed,bank):
    if parsed.get('format')!='Apple AHAP':raise ValueError('Wrong AHAP source')
    if parsed['automation'] and bank.get('ahap_dynamic_policy')!='intensity-multiply/sharpness-add-clamp':
        raise ValueError('Dynamic AHAP semantics must be explicitly qualified')
    events=[];end=0
    for event in parsed['events']:
        kind=event['kind'];mapping=bank.get('ahap_templates',{}).get(kind)
        if not mapping:raise ValueError('AHAP template missing for '+kind)
        template=bank['templates'][mapping[0]['template']]
        duration=event['duration_us'] or (len(template['samples'])*1000000+RATE-1)//RATE
        if event['time_us']+duration>60000000:raise ValueError('AHAP exceeds60s')
        events.append((event,frame(event['time_us']),frame(duration)));end=max(end,event['time_us']+duration)
    output=[0]*frame(end)
    for event,start,count in events:
        for i in range(count):
            time=(start+i)*1000000//RATE
            intensity=automation(parsed,'HapticIntensityControl',time,32767)
            sharpness=automation(parsed,'HapticSharpnessControl',time,0)
            sharp=max(0,min(32767,event['sharpness_q15']+sharpness))
            value=sharp_carrier(bank,event['kind'],sharp,i,count);level=multiply(event['intensity_q15'],intensity)
            scaled=(abs(value)*level+16383)//32767;output[start+i]+= -scaled if value<0 else scaled
    if any(abs(v)>32767 for v in output):raise ValueError('AHAP source mixture clips')
    return array('h',output)
def convert(pcm,bank,library):
    if not pcm or len(pcm)>MAX_FRAMES:raise ValueError('Empty/oversized stream')
    function=ctypes.CDLL(str(Path(library).resolve(strict=True))).elden_haptics_samples
    function.argtypes=[ctypes.POINTER(ctypes.c_int16),ctypes.c_size_t,ctypes.c_uint16,ctypes.POINTER(GainMap),ctypes.POINTER(ctypes.c_int8),ctypes.POINTER(ctypes.c_uint64)]
    function.restype=ctypes.c_int;outputs=[];summary=[]
    for channel in bank['channels']:
        points=(GainPoint*len(channel['points']))(*[GainPoint(*row) for row in channel['points']])
        mapping=GainMap(points,len(points),channel['drive_peak_q15'],RATE,48000,(1<<63)-1)
        output=bytearray();total_q30=0
        for at in range(0,len(pcm),48000):
            block=pcm[at:at+48000];source=(ctypes.c_int16*len(block))(*block);dest=(ctypes.c_int8*len(block))();cost=ctypes.c_uint64()
            if function(source,len(block),32767,ctypes.byref(mapping),dest,ctypes.byref(cost)):raise ValueError('Actual C sample conversion rejected')
            output.extend(bytes(dest));total_q30+=cost.value
        rolling=peak=0;on=peak_on=0;window=channel['window_frames'];gain=channel['gain'];values=array('b');values.frombytes(bytes(output))
        for i,v in enumerate(values):
            rolling+=v*v*gain*gain;on+=bool(v)
            if i>=window:old=values[i-window];rolling-=old*old*gain*gain;on-=bool(old)
            peak=max(peak,rolling);peak_on=max(peak_on,on)
        if peak>channel['max_window_squares'] or peak_on>channel['max_window_on_frames']:raise ValueError('Measured rolling drive budget exceeded')
        outputs.append(bytes(output));summary.append({'gain':gain,'max_window_squares':peak,'max_window_on_frames':peak_on,'normalized_q30_cost':total_q30,
            'pcm_sha256':hashlib.sha256(output).hexdigest()})
    return outputs,summary
def compile_vibration(name,parsed,bank,library):
    pcm=render_vibration(name,parsed,bank)
    if not pcm:return {'source_sha256':parsed['source_sha256'],'recipe':name,'frames':0,'rate_hz':RATE,'hardware_qualified':False},[b'',b'']
    output,channels=convert(pcm,bank,library)
    return {'source_sha256':parsed['source_sha256'],'recipe':name,'frames':len(pcm),'duration_us':(len(pcm)*1000000+RATE-1)//RATE,
        'rate_hz':RATE,'bank_sha256':bank['bank_sha256'],'channels':channels,'hardware_qualified':bank['hardware_qualified'],
        'physical_speakers_included':False,'native_delivery_verified':False},output
def compile_ahap(parsed,bank,library):
    pcm=render_ahap(parsed,bank)
    if not pcm:return {'source_sha256':parsed['source_sha256'],'format':'Apple AHAP','frames':0,'rate_hz':RATE,
        'bank_sha256':bank['bank_sha256'],'channels':[],'hardware_qualified':bank['hardware_qualified'],
        'audio_sidecars_executed':False,'native_delivery_verified':False},[b'',b'']
    output,channels=convert(pcm,bank,library)
    return {'source_sha256':parsed['source_sha256'],'format':'Apple AHAP','frames':len(pcm),'rate_hz':RATE,
        'bank_sha256':bank['bank_sha256'],'channels':channels,'hardware_qualified':bank['hardware_qualified'],
        'audio_sidecars_executed':False,'native_delivery_verified':False},output
