"""Use the existing v106 bounded user-wait sampler without synthetic input."""
import copy,tomllib
from battery_boot import arguments

def verify(before,after):
    old,new=tomllib.loads(before),tomllib.loads(after)
    expected=arguments(old['entry'][0]['args'])
    if expected.pop('elden-ui',None)!='1' or expected.get('elden-ui-replay')!='1':
        raise ValueError('Expected bounded UI observer configuration')
    expected['elden-user-wait']='1'
    if arguments(new['entry'][0]['args'])!=expected:raise ValueError('Unexpected wait-sample options')
    comparable=copy.deepcopy(new)
    if new['entry'][0]['title']!='iPadOS About wait capture - TEST':raise ValueError('Changed diagnostic title')
    comparable['entry'][0]['args']=old['entry'][0]['args']
    comparable['entry'][0]['title']=old['entry'][0]['title']
    if comparable!=old:raise ValueError('Changed fallback or menu policy')
    count=len(new['entry'][0]['args'].replace('@SESSION@','0'*16).encode())
    if count>1023:raise ValueError('Wait-sample args overflow')
    return {'boot_args_bytes':count,'user_wait_enabled':True,'ui_kdebug_observer_enabled':False,
            'automatic_input':False,'kernel_changed':False}

def template(before):
    cfg=tomllib.loads(before);args=cfg['entry'][0]['args'];values=arguments(args)
    for key in ('elden-battery','elden-battery-profile'):
        if values.get(key)!='1':raise ValueError('Unexpected battery values')
    tokens=[t.split('=',1)[0] if t.split('=',1)[0] in ('elden-battery','elden-battery-profile') else t
            for t in args.split() if t.split('=',1)[0]!='elden-ui']
    tokens.append('elden-user-wait')
    result=before.replace('args = "'+args+'"','args = "'+' '.join(tokens)+'"',1)
    result=result.replace('title = "'+cfg['entry'][0]['title']+'"','title = "iPadOS About wait capture - TEST"',1)
    verify(before,result);return result
