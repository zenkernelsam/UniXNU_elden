"""Preserve an accepted ADT while applying sheng's no-Home-button UI contract."""
import copy,hashlib,struct

def decode(data):
    def node(off,depth):
        if depth>32 or off+8>len(data):raise ValueError('invalid ADT node')
        count,children=struct.unpack_from('<II',data,off);off+=8
        if count>256 or children>256:raise ValueError('invalid ADT counts')
        props=[]
        for _ in range(count):
            if off+36>len(data):raise ValueError('truncated property')
            field=data[off:off+32];length=struct.unpack_from('<I',data,off+32)[0];off+=36
            end=off+length;aligned=(end+3)&~3
            if aligned>len(data):raise ValueError('truncated value')
            name=field.split(b'\0',1)[0].decode('ascii')
            if any(p[0]==name for p in props):raise ValueError('duplicate property')
            props.append((name,field,data[off:end],data[end:aligned]));off=aligned
        nodes=[]
        for _ in range(children):child,off=node(off,depth+1);nodes.append(child)
        return dict(props=props,children=nodes),off
    tree,end=node(0,0)
    if end!=len(data):raise ValueError('trailing ADT bytes')
    return tree

def encode(tree):
    out=struct.pack('<II',len(tree['props']),len(tree['children']))
    for name,field,value,pad in tree['props']:
        assert len(field)==32 and len(pad)==(-len(value)&3)
        out+=field+struct.pack('<I',len(value))+value+pad
    return out+b''.join(encode(c) for c in tree['children'])

def values(tree):return {p[0]:p[2] for p in tree['props']}

def set_value(tree,key,value):
    item=(key,key.encode().ljust(32,b'\0'),value,bytes(-len(value)&3))
    for i,p in enumerate(tree['props']):
        if p[0]==key:tree['props'][i]=item;return
    tree['props'].append(item)

def child(tree,name):
    matches=[c for c in tree['children'] if values(c).get('name')==name.encode()+b'\0']
    if len(matches)>1:raise ValueError('duplicate node')
    return matches[0] if matches else None

def flatten(tree,path='/'):
    result={path.rstrip('/')+'/'+p[0]:p[2] for p in tree['props']}
    for c in tree['children']:
        name=values(c)['name'].rstrip(b'\0').decode('ascii')
        result.update(flatten(c,path.rstrip('/')+'/'+name))
    return result

IPAD_PRO={
    '/model':b'iPad8,1\0','/target-type':b'J317\0',
    '/product/product-name':b'iPad Pro (11-inch)\0',
    '/product/product-description':b'iPad Pro (11-inch)\0',
    '/product/compatible-device-fallback':b'iPad8,1\0',
    '/product/artwork-device-subtype':struct.pack('<I',2388),
}

def apply_gestures(data,identity='inherit'):
    assert identity in ('inherit','ipad-pro-11')
    old=decode(data);assert encode(old)==data
    root=copy.deepcopy(old)
    assert values(root)['model'].startswith(b'iPad')
    product=child(root,'product');assert product is not None
    assert values(product)['artwork-device-idiom']==b'pad\0'
    if identity=='ipad-pro-11':
        for path,value in IPAD_PRO.items():
            set_value(product if path.startswith('/product/') else root,path.rsplit('/',1)[1],value)
    set_value(product,'thin-bezel',struct.pack('<I',1))
    buttons=child(root,'buttons')
    if buttons is None:
        buttons=dict(props=[],children=[]);set_value(buttons,'name',b'buttons\0');root['children'].append(buttons)
    for name,value in [('compatible',b'buttons\0'),('device_type',b'buttons\0'),
        ('button-names',b'hold\0voldown\0volup\0'),('home-button-type',struct.pack('<I',2))]:
        set_value(buttons,name,value)
    new=encode(root);audit_gestures(data,new,identity)
    before,after=flatten(old),flatten(root)
    changes=sorted(k for k in before.keys()|after.keys() if before.get(k)!=after.get(k))
    return new,dict(source_sha256=hashlib.sha256(data).hexdigest(),adt_sha256=hashlib.sha256(new).hexdigest(),
        changed_properties=changes,model=values(root)['model'].rstrip(b'\0').decode(),
        profile=identity,home_button_type=2,physical_face_authentication=False,cache_check='pending')

def audit_gestures(before,after,identity='inherit'):
    assert identity in ('inherit','ipad-pro-11')
    old,new=flatten(decode(before)),flatten(decode(after))
    allowed={'/product/thin-bezel','/buttons/name','/buttons/compatible','/buttons/device_type',
             '/buttons/button-names','/buttons/home-button-type'}
    if identity=='ipad-pro-11':allowed.update(IPAD_PRO)
    changes={k for k in old.keys()|new.keys() if old.get(k)!=new.get(k)}
    assert changes<=allowed and not (old.keys()-new.keys())
    assert new['/buttons/home-button-type']==struct.pack('<I',2)
    assert new['/buttons/button-names']==b'hold\0voldown\0volup\0'
    assert new['/product/thin-bezel']==struct.pack('<I',1)
    if identity=='inherit':
        assert new['/model']==old['/model'] and new['/target-type']==old['/target-type']
    else:
        assert all(new[key]==value for key,value in IPAD_PRO.items())
    assert new['/compatible']==old['/compatible']
    assert new['/product/artwork-device-idiom']==b'pad\0'
