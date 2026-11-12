"""Derive the two Elden delivery units from the exact tested v94 input sources.

The v2 callback returns actual delivery status. Both units must be rebuilt
together; the different symbol prevents linking against the old void contract.
No controller, axis, descriptor, event ABI, charging or boot option changes.
"""
import hashlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
INPUTS = {
    'NativeHidInput.cpp': (
        'profiles/v91/sources/tree/build/sheng-hid-input/src/NativeHidInput.cpp',
        '79f92e4f75994fcd99af1495f3d058c3fef50d6bc2c3241fc1a31dd0f3258bdb'),
    'EldenHidGlue.cpp': (
        'profiles/v91/sources/tree/scripts/driver-patches/EldenHidGlue.cpp',
        '2d91ac1c37befe12a50e4c7182acc8d2c0a7f79ec56f7341598591e5b611efb8'),
}


def original(name):
    path, expected = INPUTS[name]
    data = (ROOT / path).read_bytes()
    if hashlib.sha256(data).hexdigest() != expected:
        raise ValueError('Frozen input changed: ' + name)
    return data


def derive(name):
    text = original(name).decode()

    def replace(before, after, count=1):
        nonlocal text
        if text.count(before) != count:
            raise ValueError('Ambiguous delivery delta: ' + before[:70])
        text = text.replace(before, after)

    if name == 'NativeHidInput.cpp':
        replace('void native_touch_set_sink(void (*)(void *), void *)',
                'void elden_touch_set_sink_v2(int (*)(void *), void *)')
        text = text.replace('native_touch_set_sink', 'elden_touch_set_sink_v2')
        replace('''    static void touchNotify(void *context) {
        NativeHidInput *self=static_cast<NativeHidInput *>(context);
        if(self && self->inputLoop)self->inputLoop->runAction(&NativeHidInput::touchAction,self);
    }''', '''    static int touchNotify(void *context) {
        NativeHidInput *self=static_cast<NativeHidInput *>(context);
        if(!self || !self->inputLoop)return kIOReturnNotReady;
        IOReturn result=kIOReturnNotReady;
        // Worker context only. The publisher serializes the snapshot across
        // all attempts; sleep outside the work-loop gate, at most24ms total.
        for(unsigned attempt=0;attempt<4;++attempt){
            result=self->inputLoop->runAction(&NativeHidInput::touchAction,self);
            if(result!=kIOReturnNoMemory && result!=kIOReturnNotReady && result!=kIOReturnBusy)break;
            if(attempt!=3)IOSleep(8);
        }
        return result;
    }''')
        replace('''        NativeHidInput *self=OSDynamicCast(NativeHidInput,owner);if(!self)return kIOReturnSuccess;
        self->touchSend();
        return kIOReturnSuccess;''', '''        NativeHidInput *self=OSDynamicCast(NativeHidInput,owner);if(!self)return kIOReturnBadArgument;
        return self->touchSend();''')
        replace('    void touchSend() {', '    IOReturn touchSend() {')
        replace('        if(!self->hasEventDriverCached())return;',
                '        if(!self->hasEventDriverCached())return kIOReturnNotReady;')
        replace('        if((before&1) || before==self->lastSeq)return;',
                '        if(before&1)return kIOReturnBusy;\n        if(before==self->lastSeq)return kIOReturnSuccess;')
        replace('''        if(native_touch_seq!=before)return;      // torn read: retry on the next tick
        self->lastSeq=before;''',
                '        if(native_touch_seq!=before)return kIOReturnBusy;')
        commit = '''        for(unsigned j=0;j<TOUCH_FINGERS;++j) {
            self->prevIds[j]=j<downCount?nextIds[j]:-1;
            if(j<downCount){self->prevX[j]=nextX[j];self->prevY[j]=nextY[j];}
        }
'''
        replace(commit, '')
        replace('        if(!slot)return;',
                '        if(!slot){self->lastSeq=before;return kIOReturnSuccess;}')
        replace('''        native_touch_hid_reports=native_touch_hid_reports+1;
    }''', '''        native_touch_hid_reports=native_touch_hid_reports+1;
        if(result!=kIOReturnSuccess)return result;
        // Only accepted reports change the contact history. In particular,
        // a failed tip-up retains its old ID and coordinates for the retry.
''' + commit + '''        self->lastSeq=before;
        return kIOReturnSuccess;
    }''')
    elif name == 'EldenHidGlue.cpp':
        replace('static IOLock *sinkLock;', 'static IOLock *sinkLock,*publishLock;')
        replace('static void (*touchSink)(void *);', 'static int (*touchSink)(void *);')
        replace('void native_touch_set_sink(void (*fn)(void *),void *context)',
                'void elden_touch_set_sink_v2(int (*fn)(void *),void *context)')
        replace('    if(!status)sinkLock=IOLockAlloc();',
                '    if(!status){sinkLock=IOLockAlloc();publishLock=IOLockAlloc();}')
        replace('IOService *device=(status || !sinkLock)?',
                'IOService *device=(status || !sinkLock || !publishLock)?')
        replace('    if(!sinkLock)return -1002;', '    if(!sinkLock || !publishLock)return -1002;')
        replace('''    IOLockLock(sinkLock);
    void (*fn)(void *)=touchSink;''', '''    // The callback enters an IO work loop. Never hold sinkLock across it;
    // a separate sleepable publisher lock keeps this snapshot stable and
    // orders concurrent physical/software producers until delivery returns.
    IOLockLock(publishLock);
    IOLockLock(sinkLock);
    int (*fn)(void *)=touchSink;''')
        replace('        return -1010-(int)missing;',
                '        IOLockUnlock(publishLock);return -1010-(int)missing;')
        replace('    fn(context);object->release();return 0;',
                '    int result=fn(context);object->release();IOLockUnlock(publishLock);return result;')
    else:
        raise ValueError('Unknown delivery unit')
    return text.encode()
