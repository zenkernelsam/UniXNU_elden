"""Derive fixed scalar FastRPC/listener code with explicit Gen4 provider gates.

No donor native MMIO, fixed address, context-bank guesses or empty-pack success.
Every change has one exact original anchor and a recorded reason.
"""
from pathlib import Path
import hashlib,json,re
HERE=Path(__file__).resolve().parent
def derive():
    record=json.loads((HERE/'rpc-source.json').read_text());data={}
    for row in record['files']:
        b=(HERE/'upstream'/row['file']).read_bytes()
        if len(b)!=row['bytes'] or hashlib.sha256(b).hexdigest()!=row['sha256']:raise ValueError('RPC source changed: '+row['path'])
        data[row['file']]=b.decode()
    out={}
    for src,dest in [('PaganiSensorsRpc.c','RpcCore.c'),('PaganiSensorsRpc.h','RpcPrivate.h'),
                     ('PaganiSensorsPack.c','RpcPack.c'),('PaganiSensorsPack.h','RpcPack.h'),('PaganiSensorsText.h','RpcText.h')]:
        t=data[src].replace('"SensorsRpc.h"','"RpcPrivate.h"').replace('"SensorsPack.h"','"RpcPack.h"').replace('"SensorsText.h"','"RpcText.h"')
        t=re.sub(r'\bssr_', 'elden_sensor_rpc_',t);t=re.sub(r'\bssp_', 'elden_sensor_pack_',t);t=re.sub(r'\bsnt_', 'elden_sensor_text_',t)
        t=t.replace('SENSORS_RPC_H','ELDEN_RPC_PRIVATE_H').replace('SENSORS_PACK_H','ELDEN_RPC_PACK_H').replace('SENSORS_TEXT_H','ELDEN_RPC_TEXT_H')
        out[dest]=t
    deltas=[]
    def patch(name,before,after,why):
        if out[name].count(before)!=1:raise ValueError('RPC anchor differs: '+why)
        out[name]=out[name].replace(before,after);deltas.append({'file':name,'reason':why})
    patch('RpcPrivate.h','#define SSR_CONTEXT_BANK 4u', '#define SSR_CONTEXT_BANK_SOURCE_ONLY 4u','no production default session')
    patch('RpcPrivate.h','#define SSR_ARENA_BYTES 0x800000u','#define SSR_ARENA_BYTES_SOURCE_ONLY 0x800000u','no production fixed arena')
    patch('RpcPrivate.h','#define SSR_LIBRARY_PATH "/vendor/dsp/adsp;/vendor/dsp;/vendor/lib/rfsa/adsp;/odm/lib/rfsa/adsp;/vendor/dsp/sensors"',
          '#define SSR_LIBRARY_PATH ""','actual OEM library path required')
    patch('RpcPrivate.h','#define SSR_LIBRARY_PATH_FILE "/.pagani/library_path"','#define SSR_LIBRARY_PATH_FILE "/.elden/library_path"','owned private library path metadata')
    patch('RpcPrivate.h','    uint32_t bank;                     /* the context bank (SSR_CONTEXT_BANK; Linux takes the first free one, so 3 is worth a try too) */',
          '    uint32_t bank; /* actual FastRPC session tag from reg3..8; NOT an apps-SMMU hardware context bank */','session tag distinguished from hardware CB')
    patch('RpcPrivate.h','    uint64_t arena_pa;                 /* physical, below 4 GiB */',
          '    uint64_t arena_pa; /* actual owned contiguous DMA IOVA below4GiB; not a fixed physical RAM address */','actual mapped IOVA semantics')
    patch('RpcPrivate.h','    void *(*alloc)(void *context, uint32_t bytes);',
          '''    void *(*alloc)(void *context, uint32_t bytes);
    /* Required real owner/epoch/route and stop gate; must be nonsleeping.
     *1only while same session permits these accesses,0closes the service. */
    int (*live)(void *context);
    /* The original shared transport reserves an opaque non-reused wire ctx;
     * low4bits are the source-qualified sensors PD2.0only on reservation. */
    int (*reserve_cookie)(void *context,uint64_t *wire_ctx);
    /* Actual response fences this one request; absent reply retains memory. */
    int (*complete_cookie)(void *context,uint64_t wire_ctx);''','actual lifetime/cookie provider contract')
    patch('RpcPrivate.h','    volatile int32_t response_value;',
          '''    volatile int32_t response_value;
    struct {uint64_t ctx;int32_t value;} reply[8];
    volatile uint32_t reply_head,reply_tail,reply_overflow;''','coherent bounded reply mailbox')
    start=out['RpcCore.c'].index('void elden_sensor_rpc_core_receive(')
    end=out['RpcCore.c'].index('\n/* ---- one invoke ---- */',start)
    original=out['RpcCore.c'][start:end]
    patch('RpcCore.c',original,'''void elden_sensor_rpc_core_receive(SsrCore *c,const uint8_t *data,uint32_t length)
{
    if(!c||!data||length<12||length>64)return;
    /* Sole original DSP callback is the producer. Release head commits the
     * ctx/value pair together; the worker never reads mixed responses. */
    uint32_t head=__atomic_load_n(&c->reply_head,__ATOMIC_RELAXED),tail=__atomic_load_n(&c->reply_tail,__ATOMIC_ACQUIRE);
    if(head-tail>=8){__atomic_store_n(&c->reply_overflow,1u,__ATOMIC_RELEASE);return;}
    c->reply[head%8].ctx=get64(data);c->reply[head%8].value=(int32_t)get32(data+8);
    __atomic_store_n(&c->reply_head,head+1,__ATOMIC_RELEASE);
}
''','replace split-atomic ctx/status with coherent reply ledger')
    patch('RpcCore.c','    c->next_id = c->next_id >= 255 ? 1 : c->next_id + 1;\n    const uint64_t ctx = (uint64_t)c->next_id << 8 | SSR_PD_SENSORS;',
          '''    if(!c->ops->live(CTX))return InvokeLink;
    uint64_t ctx=0;
    if(c->ops->reserve_cookie(CTX,&ctx)||!ctx||(ctx&15)!=SSR_PD_SENSORS||!(ctx>>4))return InvokeRefused;''',
          'real transport cookie reservation instead of reused donor counter')
    patch('RpcCore.c','    uint32_t seen = __atomic_load_n(&c->responses, __ATOMIC_ACQUIRE);','    uint32_t seen = __atomic_load_n(&c->reply_tail, __ATOMIC_RELAXED);','worker reply ledger cursor')
    patch('RpcCore.c','''        const uint32_t responses = __atomic_load_n(&c->responses, __ATOMIC_ACQUIRE);
        if (responses != seen) {
            seen = responses;
            if (__atomic_load_n(&c->response_ctx, __ATOMIC_ACQUIRE) == ctx) {
                *value = __atomic_load_n(&c->response_value, __ATOMIC_ACQUIRE);
                return InvokeOk;
            }
            c->stray++;
            continue;
        }''','''        if(!c->ops->live(CTX)||__atomic_load_n(&c->reply_overflow,__ATOMIC_ACQUIRE))return InvokeLink;
        const uint32_t head=__atomic_load_n(&c->reply_head,__ATOMIC_ACQUIRE);
        if(head!=seen){
            uint64_t actual=c->reply[seen%8].ctx;int32_t result=c->reply[seen%8].value;
            seen++;__atomic_store_n(&c->reply_tail,seen,__ATOMIC_RELEASE);
            if(actual==ctx){
                if(c->ops->complete_cookie(CTX,ctx))return InvokeLink;
                *value=result;return InvokeOk;
            }
            c->stray++;continue;
        }''','coherent matching reply before completed/fenced cookie')
    patch('RpcCore.c','        const uint64_t waited = now(c) - since;',
          '        const uint64_t current=now(c);if(current<since)return InvokeLink;\n        const uint64_t waited=current-since;','clock rollback closes active invoke')
    patch('RpcCore.c','            if (waited >= timeout_ms) return InvokeTimeout;',
          '            if(waited>=timeout_ms){c->phase=PhaseStopped;c->status.stage=-SsrStChannel;c->status.error=-110;return InvokeLink;}','unanswered DMA request cannot reuse arena on timeout')
    patch('RpcCore.c','    if (c->ops->send(CTX, message, sizeof message)) return InvokeRefused;',
          '    if (c->ops->send(CTX, message, sizeof message)) return InvokeLink; /* reserved cookie/memory retained until reset */','uncertain queued send cannot reuse memory')
    patch('RpcPrivate.h','    void (*publish)(void *context);',
          '    int (*publish)(void *context); /* actual lease/cache visibility; errors propagate */','memory publication error propagation')
    patch('RpcCore.c','    if (c->ops->publish) c->ops->publish(CTX);',
          '    if(c->ops->publish(CTX))return InvokeLink;','no DMA send before actual memory visibility')
    patch('RpcCore.c','    c->pack.count = 0;',
          '    c->pack.count = 0;\n    c->phase=PhaseStopped;c->status.stage=-SsrStPack;c->status.error=-74;','missing/bad pack never becomes ready')
    patch('RpcCore.c','    const int override = elden_sensor_pack_find(&c->pack, SSR_LIBRARY_PATH_FILE);',
          '''    const int registry=elden_sensor_pack_find(&c->pack,"/mnt/vendor/persist/sensors");
    const int configuration=elden_sensor_pack_find(&c->pack,"/vendor/etc/sensors");
    if(registry<0||configuration<0)return empty_pack(c,"actual OEM registry/config missing");
    SspEntry root;elden_sensor_pack_entry(&c->pack,(uint32_t)registry,&root);
    if(!(root.flags&SSP_DIRECTORY))return empty_pack(c,"registry is not a directory");
    elden_sensor_pack_entry(&c->pack,(uint32_t)configuration,&root);
    if(!(root.flags&SSP_DIRECTORY))return empty_pack(c,"configuration is not a directory");
    const int override=elden_sensor_pack_find(&c->pack,SSR_LIBRARY_PATH_FILE);
    if(override<0)return empty_pack(c,"actual OEM library path missing");''','actual registry/config/library path mandatory')
    patch('RpcCore.c','    if (ordinary(c)) {\n        begin(c, &t, "pack");',
          '''    if(!c->library_path[0])return empty_pack(c,"empty actual library path");
    for(uint32_t at=0;c->library_path[at];){
        uint32_t start=at;while(c->library_path[at]&&c->library_path[at]!=';')at++;
        char path[SSP_PATH];uint32_t len=at-start;
        if(!len||elden_sensor_pack_path(c->library_path+start,len,path,sizeof path)!=(int)len)
            return empty_pack(c,"noncanonical actual library path");
        for(uint32_t i=0;i<len;i++)if(path[i]!=c->library_path[start+i])return empty_pack(c,"library path alias");
        if(c->library_path[at]){at++;if(!c->library_path[at])return empty_pack(c,"empty library directory");}
    }
    if (ordinary(c)) {
        begin(c, &t, "pack");''','no empty/donor/noncanonical path fallback')
    patch('RpcCore.c','        elden_sensor_pack_entry(&c->pack, (uint32_t)override, &e);\n        if (e.size && e.size < sizeof c->library_path && !c->ops->pack_read(CTX, e.offset, c->library_path, (uint32_t)e.size)) {',
          '''        elden_sensor_pack_entry(&c->pack,(uint32_t)override,&e);
        if(e.flags&SSP_DIRECTORY)return empty_pack(c,"library path is a directory");
        if (e.size && e.size < sizeof c->library_path && !c->ops->pack_read(CTX,e.offset,c->library_path,(uint32_t)e.size)) {
            for(uint32_t i=0;i<e.size;i++)if(!c->library_path[i]||((uint8_t)c->library_path[i]<32&&!(i+1==e.size&&c->library_path[i]=='\\n')))
                return empty_pack(c,"library path has hidden bytes");''','library path directory/NUL/control suffix refused')
    patch('RpcCore.c','    const uint64_t length = (asked + kPage - 1) & ~(uint64_t)(kPage - 1);',
          '    if(asked>UINT64_MAX-(kPage-1))return SSR_ENOMEMORY;\n    const uint64_t length = (asked + kPage - 1) & ~(uint64_t)(kPage - 1);','heap page round overflow')
    patch('RpcCore.c','    if (!(rflags & kAddPages) || !length || length > heap - c->heap_used) {',
          '    if (!(rflags & kAddPages) || !length || c->heap_used>heap || length > heap - c->heap_used) {','heap range underflow')
    patch('RpcCore.c','        if (length - at < 4 || at > length) return SSR_EBADPARM;',
          '        if (at > length || length - at < 4) return SSR_EBADPARM;','input bounds before subtraction')
    patch('RpcCore.c','    c->out_length = written;\n    if (k->method == kExtended) {',
          '    if(at!=length)return SSR_EBADPARM;\n    c->out_length = written;\n    if (k->method == kExtended) {','no undeclared call-blob tail')
    patch('RpcCore.c','    case 2: case 14: case 17: case 18: case 23:   /* fflush, clearerr, setenv, unsetenv, fsync */\n        return SSR_OK;',
          '    case 2: case 14: case 23: return SSR_OK; /* readonly fflush/clearerr/fsync */\n    case 17: case 18: return SSR_EUNSUPPORTED; /* no fake environment writes */','read-only environment mutation refusal')
    patch('RpcCore.c','        const int error = c->ops->prepare ? c->ops->prepare(CTX) : 0;',
          '        const int error = c->ops->prepare(CTX);','mandatory actual mapping provider')
    patch('RpcCore.c','    c->phase = PhaseChannel;\n    c->result = -1;',
          '''    c->phase=PhaseChannel;c->result=-1;
    if(!ops||!config||!ops->now_ms||!ops->send||!ops->wait||!ops->publish||!ops->prepare||
       !ops->pack_size||!ops->pack_read||!ops->alloc||!ops->live||!ops->reserve_cookie||!ops->complete_cookie||
       !config->arena||!config->bounce||config->arena_size<=SSR_CALL_BYTES||config->arena_size>0x4000000u||
       (config->arena_size&0xfff)||
       !config->arena_pa||(config->arena_pa&0xfff)||config->arena_pa>UINT64_C(0x100000000)-config->arena_size||
       config->bank<3||config->bank>8||!config->process){c->phase=PhaseStopped;c->status.stage=-SsrStSmmu;c->status.error=-22;return;}''',
          'required callbacks/owned DMA IOVA/session geometry before access')
    patch('RpcCore.c','    c->ops = ops;\n    c->cfg = *config;',
          '    c->phase=PhaseStopped;c->status.stage=-SsrStSmmu;c->status.error=-22;\n    if(!ops||!config)return;\n    c->ops = ops;\n    c->cfg = *config;','null config refused before dereference')
    patch('RpcCore.c','    c->started_ms = now(c);',
          '    c->status.stage=0;c->status.error=0;\n    c->started_ms = now(c);','valid initialization clears precondition refusal')
    patch('RpcCore.c','    uint8_t *b = (uint8_t *)c;\n    for (uint32_t i = 0; i < sizeof *c; ++i) b[i] = 0;',
          '    if(!c)return;\n    uint8_t *b = (uint8_t *)c;\n    for (uint32_t i = 0; i < sizeof *c; ++i) b[i] = 0;','null core init refusal')
    patch('RpcCore.c','    return b->n > 0 && b->n < SSP_PATH * 4 && b->p[b->n - 1] == 0;',
          '    if(!b->p||!b->n||b->n>=SSP_PATH*4||b->p[b->n-1])return 0;\n    for(uint32_t i=0;i+1<b->n;i++)if(!b->p[i])return 0;\n    return 1;','no embedded NUL in string arguments')
    patch('RpcCore.c','    if (k->method == kUnmapMethod) return SSR_OK;     /* the pages stay the DSP\'s until reboot */',
          '    if (k->method == kUnmapMethod) return SSR_EUNSUPPORTED; /* no unproved live unmap/heap reuse */','no fake successful heap unmap')
    patch('RpcCore.c','    put64(k->out[0].p, 0);\n    put64(k->out[0].p + 8, get64(a + kPrimOut) & 0xffffffffu);',
          '''    uint64_t remote=get64(a+kPrimOut);
    if(remote>UINT32_MAX)return SSR_EBADSIZE;
    put64(k->out[0].p,0);put64(k->out[0].p+8,remote);''','DSP32bit address overflow refuses instead of truncates')
    patch('RpcCore.c','    if (base < 0 || base + offset < 0) return SSR_EFAILED;',
          '    if(base<0||(offset>0&&base>INT64_MAX-offset)||base+offset<0)return SSR_EFAILED;','file seek signed overflow')
    patch('RpcCore.c','    for (uint32_t done = 0; done < n;) {',
          '    for (uint32_t done = 0; done < n;) {\n        if(!c->ops->live(CTX))return SSR_EINTERRUPTED;','read chunk observes actual stop')
    # SSCPACK1 remains compatible, while all geometry and path spelling are
    # validated before the pack becomes visible. Raw file hashes are separate.
    patch('RpcPack.c','    if (out->total < (uint64_t)SSP_HEADER + elden_sensor_pack_table_bytes(out)) return -1;',
          '    if (out->total < (uint64_t)SSP_HEADER + elden_sensor_pack_table_bytes(out)||out->total>0x4000000u) return -1;','bounded64MiB readonly pack')
    patch('RpcPack.c','    const char *previous = 0;',
          '    const char *previous=0;uint32_t expected_name=0;uint64_t expected_data=data;','contiguous pack geometry ledger')
    patch('RpcPack.c','        if (name >= h->names || flags > SSP_DIRECTORY || names[name] != \'/\') return -1;',
          '''        if(name!=expected_name||name>=h->names||flags>SSP_DIRECTORY||names[name]!='/')return -1;
        uint32_t length=0;while(name+length<h->names&&names[name+length])length++;
        if(name+length>=h->names||!length||length>=SSP_PATH)return -1;
        char canonical[SSP_PATH];
        if(elden_sensor_pack_path((const char *)names+name,length,canonical,sizeof canonical)!=(int)length||
           compare(canonical,(const char *)names+name))return -1;
        expected_name=name+length+1;
        if(!(flags&SSP_DIRECTORY)){if(offset!=expected_data)return -1;expected_data+=size;}''','no aliased path/name/data extents')
    patch('RpcPack.c','    p->table = table;',
          '    if(expected_name!=h->names||expected_data!=h->total)return -1;\n    p->table = table;','no unused names/data suffix')
    patch('RpcPack.c','    const uint32_t bytes = elden_sensor_pack_table_bytes(h);',
          '    if(!p||!h||!h->count||h->count>SSP_MAX_ENTRIES||!h->names||h->names>SSP_MAX_NAMES||h->total>0x4000000u)return -1;\n    const uint32_t bytes = elden_sensor_pack_table_bytes(h);','header counts checked before table reads')
    patch('RpcPack.c','    if (!in || !length || in[0] != \'/\') return -1;',
          '''    if(!in||!length||!out||!capacity||length>SSP_PATH||in[0]!='/')return -1;
    if(in[length-1]==0)length--;
    for(uint32_t i=0;i<length;i++)if((uint8_t)in[i]<32||(uint8_t)in[i]>=127||in[i]=='\\\\')return -1;''','no path embedded NUL/control/backslash/truncation')
    patch('RpcCore.c','    SntText t;\n    const uint32_t changes = __atomic_load_n(&c->link_changes, __ATOMIC_ACQUIRE);',
          '    SntText t;\n    if(c->phase==PhaseStopped)return 0;\n    if(!c->ops->live(CTX))return stop(c,SsrStChannel,-125,"session stopped; memory retained");\n    const uint32_t changes = __atomic_load_n(&c->link_changes, __ATOMIC_ACQUIRE);','explicit stop/live admission before step')
    patch('RpcCore.c','            forget_session(c);\n            c->phase = PhaseChannel;\n            c->status.stage = SsrStChannel;',
          '            return stop(c,SsrStChannel,-116,"session changed; mapped heap retained to reset");','no heap/cookie reuse after link change')
    begin=out['RpcCore.c'].index('static void forget_session(');end=out['RpcCore.c'].index('\nstatic int attach(',begin)
    patch('RpcCore.c',out['RpcCore.c'][begin:end],'','remove now-forbidden heap/session reset helper')
    patch('RpcCore.c','        c->pack_done = 1;\n        c->status.stage = SsrStPack;',
          '        if(c->phase==PhaseStopped)return 0;\n        c->pack_done = 1;\n        c->status.stage = SsrStPack;','failed pack cannot override terminal state')
    # Each invoke caller already has the same source guard; count all six
    # sites explicitly. A link/stop cannot fall through into session reuse.
    anchor='    if (rc == InvokeLink) return 1;'
    if out['RpcCore.c'].count(anchor)!=6:raise ValueError('Original invoke/link call-site count changed')
    out['RpcCore.c']=out['RpcCore.c'].replace(anchor,'    if(rc==InvokeLink)return stop(c,SsrStChannel,c->status.error?c->status.error:-116,"invoke session closed; memory retained");')
    deltas.append({'file':'RpcCore.c','reason':'six actual invoke-link sites terminate and retain memory'})
    # Portable public wrapper appends lifecycle/read-only facts, not a native
    # GLINK channel or SMMU mapper. Full donor body remains source bound.
    # Test original independent pack/table primitives with prefixed fixed code.
    test=data['PaganiTest-rpc.c'];start=test.index('/* ---- the pack built here ---- */');end=test.index('static uint8_t pack_store',start)
    pack_helpers=test[start:end]
    begin=test.index('static void paths(void)');end=test.index('/* A pack from scripts/',begin)
    tests=test[begin:end]
    for variable in ('pack_helpers','tests'):
        t=pack_helpers if variable=='pack_helpers' else tests
        t=re.sub(r'\bssp_','elden_sensor_pack_',t)
        if variable=='pack_helpers':pack_helpers=t
        else:tests=t
    out['RpcOriginalPackTests.inc']=pack_helpers+'\n'+tests
    out['RpcTestCheck.h']=data['PaganiTest-check.h']
    return out,deltas
