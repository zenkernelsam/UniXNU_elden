"""Bind upstream software key operations to Elden's explicit RAM-fixture mode."""
from pathlib import Path
import hashlib
ROOT=Path(__file__).resolve().parents[2]
UPSTREAM='6d564b710c5e2143c2ce99ee456b4d93f588b87b7de5599b79199e82fb497213'
def service():
    data=(ROOT/'drivers/EldenKeyStore/NativeKeyStoreEvents.upstream.cpp').read_bytes()
    if hashlib.sha256(data).hexdigest()!=UPSTREAM:raise ValueError('Upstream source identity differs')
    text=data.decode()
    def replace(a,b):
        nonlocal text
        if text.count(a)!=1:raise ValueError('Ambiguous key-store delta')
        text=text.replace(a,b)
    replace('extern "C" bool native_aks_state_enabled(void);','extern "C" bool native_aks_state_enabled(void);\nextern "C" bool native_aks_key_operations_enabled(void);')
    first=text.index('bool deviceKey(uint8_t key[32]) {');last=text.index('void wrapTag(',first)
    text=text[:first]+'''bool deviceKey(uint8_t key[32]) {
    if (!native_aks_key_operations_enabled()) return false;
    uint8_t id[32];
    size_t n = native_aks_device_id(id, sizeof id);
    if (!n || n > sizeof id) return false;
    static const char domain[] = "UniXNU AppleKeyStore v1";
    Sha256 hash;
    hash.update(domain, sizeof domain - 1);
    hash.update(id, n);
    hash.final(key);
    return true;
}
'''+text[last:]
    replace('args->structureOutputDescriptor->writeBytes(0, data, n);','if (args->structureOutputDescriptor->writeBytes(0, data, n) != n) return kAksError;')
    replace('if (args->scalarInputCount != 2 || !args->scalarOutput || args->scalarOutputCount != 1 || !args->structureInput ||','if (args->scalarInputCount != 2 || !args->scalarInput || !args->scalarOutput || args->scalarOutputCount != 1 || !args->structureInput ||')
    replace('case kSelWrapKey: rc = wrapKey(args); break;','case kSelWrapKey: if (native_aks_key_operations_enabled()) rc = wrapKey(args); break;')
    replace('case kSelUnwrapKey: rc = unwrapKey(args); break;','case kSelUnwrapKey: if (native_aks_key_operations_enabled()) rc = unwrapKey(args); break;')
    replace('case kSelBagUuid: rc = bagUuid(args); break;','case kSelBagUuid: if (native_aks_key_operations_enabled()) rc = bagUuid(args); break;')
    replace('case kSelGeneration:\n','case kSelGeneration:\n            if (!native_aks_key_operations_enabled()) break;\n')
    replace('!setProperty("KeyOperationsAvailable", true)','!setProperty("KeyOperationsAvailable", native_aks_key_operations_enabled())')
    return text.encode()

def glue():
    path=ROOT/'profiles/v91/sources/tree/scripts/driver-patches/EldenKeyEventsGlue.cpp';text=path.read_text()
    def replace(a,b):
        nonlocal text
        if text.count(a)!=1:raise ValueError('Ambiguous Elden key-store glue delta')
        text=text.replace(a,b)
    replace('return stateRequested==1 && __atomic_load_n(&admitted,__ATOMIC_ACQUIRE)==1 &&','return (stateRequested==1 || stateRequested==2) && __atomic_load_n(&admitted,__ATOMIC_ACQUIRE)==1 &&')
    marker='extern "C" uint32_t native_aks_lock_state(void){return 0;}'
    replace(marker,'''static uint8_t device_identity[8];
static unsigned device_identity_ready;
extern "C" bool native_aks_key_operations_enabled(void){
    return stateRequested==2 && native_aks_state_enabled() && __atomic_load_n(&device_identity_ready,__ATOMIC_ACQUIRE)==1;
}
extern "C" size_t native_aks_device_id(uint8_t *out,size_t capacity){
    if(!out || capacity<sizeof(device_identity) || !native_aks_key_operations_enabled())return 0;
    memcpy(out,device_identity,sizeof(device_identity));return sizeof(device_identity);
}
'''+marker)
    replace('static unsigned attempted,refused,stateRequested,admitted,opens,calls;', 'static unsigned attempted,refused,stateRequested,admitted,opens,calls,keyCalls;')
    replace('    if(n<=48){char line[120];', '''    if(selector==10 || selector==11 || selector==23 || selector==40){
        unsigned k=__atomic_add_fetch(&keyCalls,1,__ATOMIC_RELAXED);
        if(k<=32){char keyline[120];snprintf(keyline,sizeof(keyline),"\\nELDEN_KEY_OPERATION call=%u selector=%u result=%x\\n",k,selector,(unsigned)result);say(keyline);}
    }
    if(n<=48){char line[120];''')
    replace('    OSSafeReleaseNULL(chosen);','''    if(stateRequested==2 && chosen){
        OSData *identity=OSDynamicCast(OSData,chosen->getProperty("unique-chip-id"));
        uint64_t value=0;
        if(identity && identity->getLength()==sizeof(value))memcpy(&value,identity->getBytesNoCopy(),sizeof(value));
        if(value){memcpy(device_identity,&value,sizeof(value));__atomic_store_n(&device_identity_ready,1u,__ATOMIC_RELEASE);}
    }
    OSSafeReleaseNULL(chosen);''')
    replace('    if(ephemeral!=1 || native_owner_data_status!=0','    if((stateRequested==2 && !device_identity_ready) || ephemeral!=1 || native_owner_data_status!=0')
    replace('key-operations=off\\n",stateRequested);','key-operations=%s\\n",stateRequested,native_aks_key_operations_enabled()?"software":"off");')
    replace('if(stateRequested>1)','if(stateRequested>2)')
    return text.encode()
