"""Decode source-bound Apple AHAP and vibration recipes, preserving semantics.

Raw Apple files remain external. This does not call arbitrary JSON tolerant
parsers, load external AudioCustom files, infer Taptic waveform types or equate
HapticIntensity/Sharpness to AW gain. Execution requires a calibrated compiler.
"""
from decimal import Decimal,ROUND_HALF_UP
import hashlib,json,plistlib
MAX_SECONDS=Decimal(60)
def scalar(value,low=0,high=1):
    if isinstance(value,bool) or not isinstance(value,(int,float,Decimal)):raise ValueError('Expected finite numeric scalar')
    number=Decimal(str(value))
    if not number.is_finite() or not Decimal(low)<=number<=Decimal(high):raise ValueError('Scalar out of range')
    return number
def micros(value):return int((scalar(value,0,MAX_SECONDS)*1000000).quantize(Decimal(1),rounding=ROUND_HALF_UP))
def q15(value):return int((scalar(value)*32767).quantize(Decimal(1),rounding=ROUND_HALF_UP))
def json_text(data):
    """Accept the trailing commas in Apple's actual AHAP only outside strings.
    Duplicate keys and nonfinite JSON numbers remain errors.
    """
    text=data.decode('utf-8-sig');output=[];quoted=escaped=False;removed=0
    for i,ch in enumerate(text):
        if quoted:
            output.append(ch)
            if escaped:escaped=False
            elif ch=='\\':escaped=True
            elif ch=='"':quoted=False
        elif ch=='"':quoted=True;output.append(ch)
        elif ch==',':
            j=i+1
            while j<len(text) and text[j].isspace():j+=1
            if j<len(text) and text[j] in ']}':removed+=1
            else:output.append(ch)
        else:output.append(ch)
    def pairs(items):
        result={}
        for k,v in items:
            if k in result:raise ValueError('Duplicate key '+k)
            result[k]=v
        return result
    def constant(value):raise ValueError('Nonfinite JSON '+value)
    return json.loads(''.join(output),parse_float=Decimal,object_pairs_hook=pairs,parse_constant=constant),removed
def ahap(data):
    root,trailing=json_text(data)
    if not isinstance(root,dict) or scalar(root.get('Version',0),1,2)>Decimal('1.1'):raise ValueError('Unsupported AHAP version')
    pattern=root.get('Pattern')
    if not isinstance(pattern,list) or len(pattern)>4096:raise ValueError('Invalid AHAP pattern')
    events=[];automation=[];audio=[]
    for entry in pattern:
        if not isinstance(entry,dict) or len(entry)!=1:raise ValueError('Unknown AHAP entry')
        if 'Event' in entry:
            event=entry['Event']
            if not isinstance(event,dict):raise ValueError('AHAP Event must be a dictionary')
            kind=event.get('EventType');time=micros(event.get('Time',0))
            if kind in ('AudioCustom','AudioContinuous'):
                # Record the semantic sidecar. It is not motor excitation and
                # is never opened relative to or outside the source package.
                audio.append({'kind':kind,'time_us':time,'waveform_path':event.get('EventWaveformPath'),'executed':False});continue
            if kind not in ('HapticTransient','HapticContinuous'):raise ValueError('Unsupported haptic type '+str(kind))
            duration=micros(event.get('EventDuration',0))
            if kind=='HapticContinuous' and not duration:raise ValueError('Continuous event needs duration')
            values={'HapticIntensity':Decimal(1),'HapticSharpness':Decimal('0.5')};seen=set()
            params=event.get('EventParameters',[])
            if not isinstance(params,list):raise ValueError('Invalid EventParameters')
            for item in params:
                if not isinstance(item,dict) or set(item)!={'ParameterID','ParameterValue'}:raise ValueError('Invalid event parameter')
                name=item['ParameterID']
                if name not in values or name in seen:raise ValueError('Unsupported/duplicate event parameter '+str(name))
                values[name]=scalar(item['ParameterValue']);seen.add(name)
            events.append({'kind':kind,'time_us':time,'duration_us':duration,'intensity_q15':q15(values['HapticIntensity']),
                           'sharpness_q15':q15(values['HapticSharpness'])})
        elif 'Parameter' in entry:
            parameter=entry['Parameter'];name=parameter.get('ParameterID')
            if name not in ('HapticIntensityControl','HapticSharpnessControl'):raise ValueError('Unsupported dynamic parameter')
            automation.append({'kind':'parameter','parameter':name,'time_us':micros(parameter.get('Time',0)),
                               'value_q15':q15(parameter.get('ParameterValue'))})
        elif 'ParameterCurve' in entry:
            curve=entry['ParameterCurve'];name=curve.get('ParameterID')
            if name not in ('HapticIntensityControl','HapticSharpnessControl'):raise ValueError('Unsupported parameter curve')
            points=curve.get('ParameterCurveControlPoints')
            if not isinstance(points,list) or not points or len(points)>4096:raise ValueError('Invalid curve points')
            parsed=[{'time_us':micros(p.get('Time')),'value_q15':q15(p.get('ParameterValue'))} for p in points]
            if any(a['time_us']>=b['time_us'] for a,b in zip(parsed,parsed[1:])):raise ValueError('Unordered curve')
            automation.append({'kind':'curve','parameter':name,'time_us':micros(curve.get('Time',0)),'points':parsed})
        else:raise ValueError('Unknown AHAP entry key')
    return {'schema':1,'format':'Apple AHAP','source_sha256':hashlib.sha256(data).hexdigest(),'events':events,'automation':automation,
            'audio_sidecars':audio,'trailing_commas_removed':trailing,'compiled_for_motor':False}
def vibration(data):
    root=plistlib.loads(data)
    if not isinstance(root,dict) or len(root)>4096:raise ValueError('Invalid vibration table')
    result={};unsupported={}
    for name,recipe in root.items():
        try:
            if not isinstance(name,str) or not isinstance(recipe,dict):raise ValueError('Invalid recipe')
            if not recipe:result[name]={'kind':'empty'};continue
            if 'Atoms' in recipe:
                if set(recipe)-{'Atoms','Amplitude'}:raise ValueError('Unknown atom recipe fields')
                atoms=recipe['Atoms'];master=q15(recipe.get('Amplitude',1))
                if not isinstance(atoms,list) or len(atoms)>4096:raise ValueError('Invalid atoms')
                parsed=[]
                for atom in atoms:
                    if not isinstance(atom,dict) or set(atom)-{'Type','StartTime','Amplitude','Ampliude','Duration','Envelope','Period','Cycles','Frequency','StartingGain'}:
                        raise ValueError('Unknown atom fields')
                    kind=atom.get('Type')
                    if not isinstance(kind,str) or not kind or len(kind)>64:raise ValueError('Invalid atom type')
                    if 'Amplitude' in atom and 'Ampliude' in atom:raise ValueError('Ambiguous amplitude')
                    amplitude=atom.get('Amplitude',atom.get('Ampliude',1))
                    row={'kind':kind,'time_us':micros(atom.get('StartTime',0)),'intensity_q15':q15(amplitude),
                         'duration_us':micros(atom.get('Duration',0)),'master_q15':master}
                    if 'Ampliude' in atom:row['source_amplitude_typo_preserved']='Ampliude'
                    for key in ['Period','Cycles','Frequency','StartingGain']:
                        if key not in atom:continue
                        if key=='Period':row['period_us']=micros(atom[key])
                        elif key=='Cycles':row['cycles']=str(scalar(atom[key],0,10000))
                        elif key=='Frequency':row['frequency_hz']=str(scalar(atom[key],1,24000))
                        else:row['starting_gain_q15']=q15(atom[key])
                    if 'Envelope' in atom:
                        envelope=atom['Envelope']
                        if not isinstance(envelope,list) or len(envelope)>4096:raise ValueError('Invalid envelope')
                        row['envelope']=[{'time_raw':str(scalar(p['Time'],0,60)),'level_q15':q15(p['Level'])} for p in envelope]
                        if any(Decimal(a['time_raw'])>=Decimal(b['time_raw']) for a,b in zip(row['envelope'],row['envelope'][1:])):
                            raise ValueError('Unordered envelope')
                        row['envelope_units']='source values; calibrated compiler must explicitly resolve seconds/normalized mode'
                    parsed.append(row)
                result[name]={'kind':'atoms','atoms':parsed,'waveform_templates_required':True}
            elif 'VibePattern' in recipe:
                if set(recipe)!={'VibePattern','Intensity'}:raise ValueError('Unknown pattern fields')
                pattern=recipe['VibePattern']
                if not isinstance(pattern,list) or len(pattern)%2 or len(pattern)>8192:raise ValueError('Invalid VibePattern')
                time=0;segments=[]
                for index in range(0,len(pattern),2):
                    state,duration=pattern[index:index+2]
                    if not isinstance(state,bool):raise ValueError('Pattern state must be bool')
                    duration=micros(scalar(duration,0,60000)/1000)
                    segments.append({'on':state,'time_us':time,'duration_us':duration});time+=duration
                    if time>60000000:raise ValueError('Pattern too long')
                result[name]={'kind':'on-off','intensity_q15':q15(recipe['Intensity']),'segments':segments}
            elif set(recipe)=={'Intensity','OffDuration','OnDuration','TotalDuration'}:
                result[name]={'kind':'repeating-on-off','intensity_q15':q15(recipe['Intensity']),
                    'off_us':micros(recipe['OffDuration']),'on_us':micros(recipe['OnDuration']),'total_us':micros(recipe['TotalDuration'])}
                if not result[name]['on_us']+result[name]['off_us']:raise ValueError('Zero on-off period')
            else:raise ValueError('Unknown recipe form')
        except (ValueError,KeyError,TypeError,plistlib.InvalidFileException) as error:unsupported[name]=str(error)
    return {'schema':1,'format':'Apple SystemSoundVibrationPatterns','source_sha256':hashlib.sha256(data).hexdigest(),
            'recipes':result,'rejected':unsupported,'compiled_for_motor':False}
