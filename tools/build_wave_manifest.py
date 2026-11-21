"""Deterministic offline builder: original Apple source recipes -> C manifest.

Parses Apple SystemSoundVibrationPatterns plist and supported AHAP files
through the existing AppleEffects parser, qualifies every carrier against the
measured source-bound template/calibration bank via EffectCompiler.load_bank,
and emits bounded static C: deduplicated 24 kHz sample arrays, EldenWaveCarrier
/ EldenWaveControlPoint / EldenWaveAutomation / EldenWaveEvent / EldenWaveRecipe
structures and a sorted, nonzero, unique, untagged EldenWaveSource manifest.

Only semantics WaveCompiler already represents are emitted: explicit
one-shot/loop/tail carriers, AHAP transient/continuous events, intensity/
sharpness automation, and VibePattern segments under an explicit qualified
legacy carrier/order. Unsupported waveform mappings, >2-point sharpness morphs,
mismatched morph carriers, unresolved duration, envelope units, fractional
repeats, audio sidecars, duplicate names/IDs and unknown policies all refuse.
No Tap/Sine carrier is ever synthesized and no intensity->voltage is inferred.

Production mode requires the bank to be hardware_qualified with valid evidence,
exact TB322FC/SM8750 board identity, 24 kHz and the pinned haptic firmware.
--test-only admits a synthetic bank and stamps production_ready:false.
"""
from array import array
from decimal import Decimal
from pathlib import Path
import argparse,hashlib,json,sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'drivers/EldenHaptics'))
from AppleEffects import vibration,ahap
from EffectCompiler import load_bank,frame,multiply
RATE=24000;MAX_US=60000000;MAX_EVENTS=1024;MAX_SOURCES=512
def fnv(text):
    h=2166136261
    for b in text.encode():h=((h^b)*16777619)&0xffffffff
    return h
def carrier_key(t):return (t['samples'].tobytes(),t['loop_frames'],t['tail_frames'])
def morph_pair(bank,mapping):
    """-> (carrier_template, morph_template|None) under WaveCompiler's single-pair contract."""
    def template(name):
        t=bank['templates'].get(name)
        if not t:raise ValueError('Unknown sharpness template '+str(name))
        return t
    if len(mapping)==1:
        if mapping[0]['sharpness_q15']!=0:raise ValueError('Single-carrier mapping must bind sharpness 0')
        return template(mapping[0]['template']),None
    if len(mapping)!=2:raise ValueError('Unsupported sharpness mapping (one morph pair only)')
    a,b=mapping
    if a['sharpness_q15']!=0 or b['sharpness_q15']!=32767:raise ValueError('Sharpness endpoints required')
    ta,tb=template(a['template']),template(b['template'])
    if (len(ta['samples']),ta['loop_frames'],ta['tail_frames'])!=(len(tb['samples']),tb['loop_frames'],tb['tail_frames']):
        raise ValueError('Mismatched sharpness carriers')
    return ta,tb
def event_frames(template,duration_us,what):
    if duration_us:
        frames=frame(duration_us)
        if not frames:raise ValueError('Unresolved duration for '+what)
    else:
        if template['loop_frames']:raise ValueError('Unresolved duration for '+what+' (loop carrier needs explicit)')
        frames=len(template['samples'])
    if not template['loop_frames'] and frames!=len(template['samples']):
        raise ValueError('One-shot carrier duration changed for '+what)
    if template['tail_frames'] and frames<template['tail_frames']:
        raise ValueError('Event shorter than qualified tail for '+what)
    return frames
def ahap_events(parsed,bank):
    if parsed['audio_sidecars']:raise ValueError('Unsupported audio sidecars cannot be represented')
    out=[]
    for e in parsed['events']:
        mapping=bank.get('ahap_templates',{}).get(e['kind'])
        if not mapping:raise ValueError('No calibrated AHAP mapping for '+e['kind'])
        carrier,morph=morph_pair(bank,mapping)
        frames=event_frames(carrier,e['duration_us'],e['kind'])
        out.append({'carrier':carrier,'morph':morph,'start':frame(e['time_us']),'frames':frames,
                    'intensity':e['intensity_q15'],'sharpness':e['sharpness_q15']})
    if len(out)>MAX_EVENTS:raise ValueError('Event bound')
    out.sort(key=lambda e:e['start']);return out
def automation_lists(parsed,bank):
    names={'HapticIntensityControl':'intensity','HapticSharpnessControl':'sharpness'}
    if parsed['automation'] and bank.get('ahap_dynamic_policy')!='intensity-multiply/sharpness-add-clamp':
        raise ValueError('Unqualified dynamic AHAP policy')
    lists={'intensity':[],'sharpness':[]}
    for item in parsed['automation']:
        name=names.get(item['parameter'])
        if not name:raise ValueError('Unsupported automation parameter '+item['parameter'])
        if item['kind']=='parameter':
            lists[name].append({'kind':'step','frame':frame(item['time_us']),'value':item['value_q15']})
        elif item['kind']=='curve':
            pts=[{'frame':frame(p['time_us']),'value':p['value_q15']} for p in item['points']]
            if len(pts)>1024:raise ValueError('Curve bound')
            if any(a['frame']>=b['frame'] for a,b in zip(pts,pts[1:])):
                raise ValueError('Distinct curve point times collapse to one frame')
            lists[name].append({'kind':'curve','frame':frame(item['time_us']),'points':pts})
        else:raise ValueError('Unknown automation kind')
    for name in lists:lists[name].sort(key=lambda x:x['frame'])
    return lists
def atom_events(recipe,bank,name):
    out=[]
    for atom in recipe['atoms']:
        kind=atom['kind']
        if kind=='Silence':continue
        template=bank['templates'].get(kind)
        if not template:raise ValueError('No qualified template for '+kind)
        for key in ('frequency_hz','starting_gain_q15'):
            if key in atom and str(template.get(key))!=str(atom[key]):raise ValueError('Template '+key+' mismatches source')
        if 'envelope' in atom:raise ValueError('Envelope units cannot be represented for '+name)
        duration=atom['duration_us'] or (len(template['samples'])*1000000+RATE-1)//RATE
        period=atom.get('period_us',duration);cycles=Decimal(atom.get('cycles','1'))
        if cycles!=cycles.to_integral_value() or not 0<=cycles<=10000:raise ValueError('Fractional repeat policy')
        if cycles>1 and not period:raise ValueError('Zero repeat period')
        for rep in range(int(cycles)):
            start=atom['time_us']+rep*period
            if start+duration>MAX_US:raise ValueError('Recipe exceeds 60s')
            out.append({'carrier':template,'morph':None,'start':frame(start),
                        'frames':event_frames(template,duration,kind),
                        'intensity':multiply(atom['intensity_q15'],atom['master_q15']),'sharpness':0})
    if len(out)>MAX_EVENTS:raise ValueError('Event bound')
    out.sort(key=lambda e:e['start']);return out
def legacy_events(recipe,bank):
    carrier_name=bank.get('legacy_on_template')
    if carrier_name not in bank['templates']:raise ValueError('No qualified legacy carrier')
    carrier=bank['templates'][carrier_name];segments=[]
    if recipe['kind']=='on-off':segments=recipe['segments']
    elif recipe['kind']=='repeating-on-off':
        if bank.get('legacy_repeat_order')!='on-off':raise ValueError('Repeating source order unqualified')
        period=recipe['on_us']+recipe['off_us'];time=0
        while time<recipe['total_us']:
            segments.append({'on':True,'time_us':time,'duration_us':min(recipe['on_us'],recipe['total_us']-time)});time+=period
    else:raise ValueError('Unsupported original recipe form')
    out=[{'carrier':carrier,'morph':None,'start':frame(s['time_us']),'frames':event_frames(carrier,s['duration_us'],'legacy'),
          'intensity':recipe['intensity_q15'],'sharpness':0} for s in segments if s['on'] and s['duration_us']]
    if len(out)>MAX_EVENTS:raise ValueError('Event bound')
    out.sort(key=lambda e:e['start']);return out
def build(recipes,bank):
    """recipes: [(name,events,intensity|None,sharpness|None,source_sha)] -> C text + report rows."""
    carriers={};car_order=[]
    for name,events,i_,s_,sha in recipes:
        for e in events:
            for t in (e['carrier'],e['morph']):
                if not t:continue
                key=carrier_key(t)
                if key not in carriers:carriers[key]=len(car_order);car_order.append((key,t))
    body=[];decls=[]
    for i,(key,t) in enumerate(car_order):
        samples=t['samples']
        body.append('static const int16_t elden_wave_pcm_%d[%d] = {%s};'%(i,len(samples),','.join(map(str,samples))))
        decls.append('static const EldenWaveCarrier elden_wave_carrier_%d = {elden_wave_pcm_%d,%d,%d,%d};'
                     %(i,i,len(samples),t['loop_frames'],t['tail_frames']))
    car_id={key:i for i,(key,t) in enumerate(car_order)}
    source_rows=[];entries=[]
    for name,events,intensity,sharpness,source_sha in recipes:
        rid=fnv(name+'\0'+source_sha+'\0'+bank['bank_sha256'])&0x7fffffff
        if not rid:rid=1
        if any(e[0]==rid for e in entries):raise ValueError('Duplicate source id')
        tag='s%d'%len(entries)
        for e in events:
            e['_sym']='elden_wave_events_%s'%tag
        lines=[]
        if events:
            lines.append('static const EldenWaveEvent elden_wave_events_%s[%d] = {'%(tag,len(events)))
            for e in events:
                c='&elden_wave_carrier_%d'%car_id[carrier_key(e['carrier'])]
                mo='&elden_wave_carrier_%d'%car_id[carrier_key(e['morph'])] if e['morph'] else '0'
                lines.append('    {%s,%s,%d,%d,%d,%d},'%(c,mo,e['start'],e['frames'],e['intensity'],e['sharpness']))
            lines.append('};')
        for label,items in (('intensity',intensity),('sharpness',sharpness)):
            if not items:continue
            for j,it in enumerate(items):
                if it['kind']!='curve':continue
                pts=it['points'];sym='elden_wave_pts_%s_%s_%d'%(tag,label,j)
                lines.append('static const EldenWaveControlPoint %s[%d] = {%s};'%(sym,len(pts),
                    ','.join('{%d,%d}'%(p['frame'],p['value']) for p in pts)))
                it['_sym']=sym
            lines.append('static const EldenWaveAutomation elden_wave_auto_%s_%s[%d] = {'%(tag,label,len(items)))
            for j,it in enumerate(items):
                if it['kind']=='step':lines.append('    {%d,EldenWaveStep,%d,0,0},'%(it['frame'],it['value']))
                else:lines.append('    {%d,EldenWaveCurve,0,%s,%d},'%(it['frame'],it['_sym'],len(it['points'])))
            lines.append('};')
        if intensity:
            lines.append('static const EldenWaveAutomationList elden_wave_list_%s_intensity = {elden_wave_auto_%s_intensity,%d};'
                         %(tag,tag,len(intensity)))
        if sharpness:
            lines.append('static const EldenWaveAutomationList elden_wave_list_%s_sharpness = {elden_wave_auto_%s_sharpness,%d};'
                         %(tag,tag,len(sharpness)))
        total=max((e['start']+e['frames'] for e in events),default=0)
        lines.append('static const EldenWaveRecipe elden_wave_recipe_%s = {elden_wave_events_%s,%d,%s%s,%d};'
                     %(tag,tag,len(events),
                       ('&elden_wave_list_%s_intensity'%tag+',') if intensity else '0,',
                       ('&elden_wave_list_%s_sharpness'%tag) if sharpness else '0',total))
        decls.extend(lines)
        entries.append((rid,tag,name,total,len(events)))
        source_rows.append({'name':name,'id':rid,'total_frames':total,'events':len(events)})
    entries.sort()
    source_rows.sort(key=lambda r:r['id'])
    decls.append('const EldenWaveSource elden_wave_manifest[%d] = {%s};'
                 %(len(entries),','.join('{%d,&elden_wave_recipe_%s}'%(rid,tag) for rid,tag,*_ in entries)))
    decls.append('const unsigned elden_wave_manifest_count = %d;'%len(entries))
    return '\n'.join(body+decls)+'\n',entries,source_rows
HEADER='''/* GENERATED by tools/build_wave_manifest.py — deterministic, do not edit.
 * source_sha256: %s
 * bank_sha256:   %s
 * production_ready: %s
 */
#include "EldenWaveManifest.h"
'''
HHEADER='''/* GENERATED by tools/build_wave_manifest.py — deterministic, do not edit. */
#ifndef ELDEN_WAVE_MANIFEST_H
#define ELDEN_WAVE_MANIFEST_H
#include "WaveFeed.h"
extern const EldenWaveSource elden_wave_manifest[];
extern const unsigned elden_wave_manifest_count;
#endif
'''
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,action='append',required=True)
    p.add_argument('--vibration-recipe',action='append',default=None)
    p.add_argument('--bank',type=Path,required=True);p.add_argument('--test-only',action='store_true')
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    bank=load_bank(a.bank,allow_test=a.test_only)
    recipes=[];seen=set();source_meta=[]
    for src in a.source:
        raw=src.read_bytes()
        if src.suffix.lower()=='.ahap':
            parsed=ahap(raw);events=ahap_events(parsed,bank)
            lists=automation_lists(parsed,bank)
            name=src.stem;sha=parsed['source_sha256']
            rows=[(name,events,lists['intensity'],lists['sharpness'],sha)]
            source_meta.append({'source':src.name,'format':'AHAP','sha256':sha})
        else:
            parsed=vibration(raw);sha=parsed['source_sha256']
            names=a.vibration_recipe or sorted(parsed['recipes'])
            rows=[]
            for name in names:
                recipe=parsed['recipes'].get(name)
                if not recipe:raise ValueError('Unknown vibration recipe '+name)
                if recipe['kind']=='empty':continue
                events=atom_events(recipe,bank,name) if recipe['kind']=='atoms' else legacy_events(recipe,bank)
                rows.append((name,events,None,None,sha))
            source_meta.append({'source':src.name,'format':'vibration','sha256':sha,
                                'rejected':parsed['rejected']})
        for name,events,i_,s_,sha in rows:
            if name in seen:raise ValueError('Duplicate recipe name '+name)
            seen.add(name)
            if events:recipes.append((name,events,i_,s_,sha))
    if not recipes:raise ValueError('Empty manifest: no supported non-empty recipes')
    if len(recipes)>MAX_SOURCES:raise ValueError('Source bound')
    body,entries,rows=build(recipes,bank)
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    (out/'EldenWaveManifest.h').write_text(HHEADER)
    src_shas=','.join(sorted({s['sha256'] for s in source_meta}))
    (out/'EldenWaveManifest.c').write_text(HEADER%(src_shas,bank['bank_sha256'],
        'true' if not a.test_only else 'false')+body)
    report={'passed':True,'entries':rows,'sources':source_meta,'bank_sha256':bank['bank_sha256'],
            'builder_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
            'ids_sorted_unique_nonzero_untagged':True,'carriers_deduplicated':True,
            'hardware_qualified':bank['hardware_qualified'],'production_ready':not a.test_only,
            'native_bound':False,'hardware':False}
    (out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Generated %d bounded source entries (%s); Native.cpp stays unbound'
          %(len(entries),'production' if not a.test_only else 'TEST-ONLY'))
if __name__=='__main__':main()
