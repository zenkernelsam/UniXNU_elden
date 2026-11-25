"""Derives the A830 synchronized-buffer maintenance and SMMU fault isolation.

Donors (Adreno740-ipados27 lineage, research/Adreno740-ipados27):
  5eb625f  perf(maintenance): leave the driver's synchronized buffers out of the
           per-submission cache passes (declareSynchronized, care_ work list,
           native_adreno_maint_kind/maint_walk/sync_skips/sync_audit/maint_all,
           auditSynchronized, headWords, NativeAdrenoPhaseWrite).
  88241fa  fix(recovery): a GPU access fault no longer fails other address
           spaces (NativeAdrenoVm::notOurFault on the SMMU global fault status;
           GpuContext::endFault clears sGFSR too).
  4799715  fix(memory): keep the pool's zero fill out of the driver's lock —
           the A830 tree has no page pool; only the offerSystemPages(allocs)
           parity line applies (NativeAdrenoPublish.cpp).

Applied to Adreno740 @ 9c34ea46aa991c4fed6f0347a814473ad626de77 (the A830
sources the tree ships). Every anchor must match exactly once; drift fails
loudly. A830 deltas honoured: six context buffers (SQE, ring, memptrs, pwrup,
scratch, AQE — verified writer sets; the only kernel-mapping store is
probeBuffer's dword, cleaned where made), buffer_count_ loop, no
NativeAdrenoPoolAllocate/grant_, no waited-fence plumbing.
"""
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent


def once(source, anchor, replacement, label):
    if source.count(anchor) != 1:
        raise ValueError(label + ' anchor count ' + str(source.count(anchor)))
    return source.replace(anchor, replacement)


def pin():
    return json.loads((HERE / 'upstream/maintenance-source.json').read_text())


def memory_h(text):
    # NativeAdrenoPhaseWrite takes the unused first phase slot, as the donor did.
    text = once(text,
        'enum NativeAdrenoMaintenancePhase {NativeAdrenoPhaseUnused,NativeAdrenoPhaseAllocate,NativeAdrenoPhaseMap,NativeAdrenoPhaseSubmit,\n',
        '/* NativeAdrenoPhaseWrite: a writer of the driver\'s own memory cleaning what it stored through a cacheable mapping. */\n'
        'enum NativeAdrenoMaintenancePhase {NativeAdrenoPhaseWrite,NativeAdrenoPhaseAllocate,NativeAdrenoPhaseMap,NativeAdrenoPhaseSubmit,\n',
        'phase write')
    text = once(text,
        '    bool prepared_=false,quarantined_=false,write_combined_=false,system_=false,borrowed_=false,owned_=false;\n',
        '    bool prepared_=false,quarantined_=false,write_combined_=false,system_=false,borrowed_=false,owned_=false;\n'
        '    bool synchronized_=false;   /* declareSynchronized(): its writers keep the device\'s view current themselves */\n',
        'synchronized field')
    text = once(text,
        '    bool writeCombined() const {return write_combined_;}\n',
        '    bool writeCombined() const {return write_combined_;}\n'
        '    /* The driver\'s own kernel-mapped buffer whose users keep the device\'s view current themselves, as a driver of a non-coherent\n'
        '     * device does with dma_sync_single_for_device and dma_sync_single_for_cpu: whoever stores into it through a cacheable mapping\n'
        '     * cleans what it stored before the device can read it (publishForDevice), whoever loads what the device wrote invalidates\n'
        '     * first (acquireDeviceWrites), and a user with a non-cacheable view needs neither. A dirty line can then exist only between a\n'
        '     * store and its writer\'s own clean, so the passes over every mapping (at a map, an unmap, a submission) have nothing to do\n'
        '     * for it and leave it out. Called after allocate() and before any mapping: the zero fill is cleaned and dropped here, so no\n'
        '     * line of the buffer is in a CPU cache when its users take it over. */\n'
        '    IOReturn declareSynchronized();\n'
        '    bool synchronized() const {return synchronized_;}\n'
        '    /* Whether a CPU cache can hold a line of this memory that the device has not seen: not for an import whose owner maps it\n'
        '     * non-cacheable, and not for write-combined memory unless a view of it was found caching (include_write_combined). The passes\n'
        '     * that decide without calling publishForDevice use the same answer. */\n'
        '    bool holdsCpuLines(bool include_write_combined) const\n'
        '    {return !(imported_ && owner_uncached_) && !(write_combined_ && !include_write_combined);}\n'
        '    /* Whether reassertWriteCombined has pages to look at. */\n'
        '    bool reassertable() const {return write_combined_ && span_count_ && !system_;}\n',
        'synchronized decls')
    text = once(text,
        '    IOReturn reassertWriteCombined(uint32_t *reset) const\n'
        '    {return write_combined_ && span_count_ && !system_?NativeAdrenoReassertWriteCombined(spans_,span_count_,reset):kIOReturnSuccess;}\n',
        '    IOReturn reassertWriteCombined(uint32_t *reset) const\n'
        '    {return reassertable()?NativeAdrenoReassertWriteCombined(spans_,span_count_,reset):kIOReturnSuccess;}\n',
        'reassertable')
    return text


def memory_cpp(text):
    # declareSynchronized after makeWriteCombined, as the donor placed it.
    text = once(text,
        '    if(status)return status;\n    write_combined_=true;return kIOReturnSuccess;\n}\n',
        '    if(status)return status;\n    write_combined_=true;return kIOReturnSuccess;\n}\n'
        'IOReturn NativeAdrenoMemory::declareSynchronized()\n'
        '{\n'
        '    if(!descriptor_ || system_ || imported_ || borrowed_ || !prepared_ || quarantined_ || device_references_ || !mapping_ || !span_count_)\n'
        '        return kIOReturnNotReady;\n'
        '    const IOReturn status=NativeAdrenoMaintainOwnedPages(mapping_,bytes_,NativeAdrenoCleanInvalidate);\n'
        '    if(status)return status;\n'
        '    NativeAdrenoNotePhase(NativeAdrenoPhaseAllocate,bytes_);\n'
        '    synchronized_=true;return kIOReturnSuccess;\n'
        '}\n',
        'declareSynchronized')
    text = once(text,
        '    mapping_=nullptr;spans_=nullptr;bytes_=span_count_=span_capacity_=0;read_only_=false;owner_uncached_=false;\n',
        '    mapping_=nullptr;spans_=nullptr;bytes_=span_count_=span_capacity_=0;read_only_=false;owner_uncached_=false;synchronized_=false;\n',
        'release reset')
    text = once(text,
        '    /* Pages every CPU mapping holds non-cacheable carry no line to clean. */\n'
        '    if(imported_ && owner_uncached_)return kIOReturnSuccess;\n',
        '    /* Pages every CPU mapping holds non-cacheable carry no line to clean. */\n'
        '    if(!holdsCpuLines(include_write_combined))return kIOReturnSuccess;\n',
        'holdsCpuLines')
    text = once(text,
        '    if(write_combined_) {  /* no CPU mapping should cache these pages; if one does, reach its lines through the physical pages */\n'
        '        if(!include_write_combined)return kIOReturnSuccess;\n',
        '    if(write_combined_) {  /* no CPU mapping should cache these pages; if one does, reach its lines through the physical pages */\n',
        'wc gate')
    return text


def vm_h(text):
    text = once(text,
        'extern "C" int (*native_adreno_line_audit)(uint64_t physical,size_t bytes,uint32_t *dirty,uint32_t *stale);\n',
        'extern "C" int (*native_adreno_line_audit)(uint64_t physical,size_t bytes,uint32_t *dirty,uint32_t *stale);\n'
        '/* What a mapping is, for the maintenance counters: the rows of native_adreno_maint_kind. */\n'
        'enum NativeAdrenoMappingKind : unsigned {\n'
        '    NativeAdrenoKindDriver,          /* the driver\'s own synchronized buffer (NativeAdrenoMemory::declareSynchronized) */\n'
        '    NativeAdrenoKindSystemWc,        /* a user BO of system memory, write-combined */\n'
        '    NativeAdrenoKindSystemCached,    /* a user BO of system memory with the default attribute */\n'
        '    NativeAdrenoKindKernel,          /* kernel-mapped memory nobody declared synchronized (a contiguous user BO, a board\'s buffer) */\n'
        '    NativeAdrenoKindImportRead,      /* an import the GPU only reads */\n'
        '    NativeAdrenoKindImportWritten,   /* an import the GPU writes */\n'
        '    NativeAdrenoKindImportUncached,  /* an import whose owner maps it non-cacheable */\n'
        '    NativeAdrenoKindCount\n'
        '};\n'
        '/* CPU cache maintenance at submissions by kind of mapping: [kind][0] mappings maintained, [1] their KiB, [2] mappings left alone\n'
        ' * (coherent, non-cacheable for the CPU, or synchronized by their writers). */\n'
        'extern "C" volatile uint64_t native_adreno_maint_kind[8][3];\n'
        '/* The passes over a VM\'s mappings: [0] submissions, [1] mappings their passes visited, [2] mappings the VMs held at those\n'
        ' * submissions, [3] completions, [4] mappings their passes visited, [5] times every mapping was classified again (a mode changed),\n'
        ' * [6] mappings the write-combine re-assertion visited. */\n'
        'extern "C" volatile uint64_t native_adreno_maint_walk[8];\n'
        '/* Synchronized driver memory left out of the maintenance at [0] a map and [1] an unmap. */\n'
        'extern "C" volatile uint32_t native_adreno_sync_skips[2];\n'
        '/* The audit of the synchronized driver buffers (NativeAdrenoVm::auditSynchronized): [0] audits, [1] lines compared, [2] dirty\n'
        ' * lines, [3] stale clean lines, [4] the audit that found a dirty line or failed (0: never; from then on these buffers are\n'
        ' * maintained at every map, unmap and submission again), [5] the last failure. */\n'
        'extern "C" volatile uint32_t native_adreno_sync_audit[6];\n'
        '/* gpu-maint-all=1 (read at the first use; ~0 until then), for a comparison on the machine: every pass visits every mapping of\n'
        ' * the VM and the driver\'s own synchronized buffers are cleaned and invalidated at every map, unmap and submission like memory\n'
        ' * nobody declared. */\n'
        'extern "C" volatile uint32_t native_adreno_maint_all;\n',
        'kind externs')
    text = once(text,
        '        unsigned permissions=0,writeback=0,shareability=0,jobs=0,upper=0;\n    };\n',
        '        unsigned permissions=0,writeback=0,shareability=0,jobs=0,upper=0;\n'
        '        uint32_t care=UINT32_MAX;   /* its place in care_ (UINT32_MAX: not there) */\n'
        '        uint8_t kind=0,work=0;      /* NativeAdrenoMappingKind, and the Work bits it was counted with */\n'
        '    };\n',
        'mapping fields')
    text = once(text,
        '    Mapping *mappings_=nullptr;\n',
        '    /* What the passes owe a mapping in the current mode: cleaning before a submission, invalidating after a completion, a look\n'
        '     * at its pages\' write-combined attribute; and how the counters took it (coherent at a submission, and at a completion). */\n'
        '    enum : uint8_t {WorkSubmit=1,WorkComplete=2,WorkReassert=4,WorkAny=7,CountedCoherent=8,CountedCoherentWrite=16};\n'
        '    /* The mappings a pass has work for (mappings_ slots, in no order): a submission, a completion and the re-assertion visit\n'
        '     * these instead of every mapping. The list follows the mode (IO coherence, the write-combined trust, the synchronized\n'
        '     * trust, gpu-maint-all): sync() classifies every mapping again when the mode changed. */\n'
        '    uint32_t *care_=nullptr;\n'
        '    size_t care_count_=0;\n'
        '    size_t kind_count_[NativeAdrenoKindCount]{},kind_work_[NativeAdrenoKindCount]{};\n'
        '    size_t submit_skips_=0,complete_skips_=0;\n'
        '    uint32_t mode_=UINT32_MAX;\n'
        '    uint32_t mode() const;\n'
        '    void sync();\n'
        '    void enlist(Mapping *);\n'
        '    void delist(Mapping *);\n'
        '    Mapping *mappings_=nullptr;\n',
        'care members')
    text = once(text,
        '    ArmSmmu4KStatus observeCheap();\n',
        '    ArmSmmu4KStatus observeCheap();\n'
        '    /* The SMMU\'s global fault status set while the routes and the global configuration read as they should is the GPU\'s own\n'
        '     * access faulting (a job\'s bad address), which the hang check ends: it says nothing about this address space\'s tables.\n'
        '     * Counted with the status word; the operation goes on (88241fa: failing here took out whichever process mapped next). */\n'
        '    static ArmSmmu4KStatus notOurFault(ArmSmmu4KStatus,uint32_t word);\n',
        'notOurFault decl')
    text = once(text,
        '    IOReturn publishMapped();\n',
        '    /* Before every submission: cleans and invalidates the CPU caches over every mapped BO a CPU cache can hold a line of that\n'
        '     * the GPU would not see (whole objects; imported ones through their physical pages). Lines are physically tagged, so this\n'
        '     * also reaches lines a user mapping of the same pages left behind. Left alone: coherent mappings, memory no CPU mapping\n'
        '     * caches (write-combined while trusted, imports their owner maps non-cacheable), every import once IO coherence is proved,\n'
        '     * and the driver\'s own synchronized buffers, whose writers cleaned what they stored. Only the mappings with such work are\n'
        '     * visited. */\n'
        '    IOReturn publishMapped();\n',
        'publishMapped doc')
    text = once(text,
        '    IOReturn auditRange(uint64_t iova,size_t bytes,uint32_t *lines,uint32_t *dirty,uint32_t *stale) const;\n',
        '    IOReturn auditRange(uint64_t iova,size_t bytes,uint32_t *lines,uint32_t *dirty,uint32_t *stale) const;\n'
        '    /* The proof on the machine that the synchronized driver buffers hold no dirty line when a submission arrives: the first\n'
        '     * 32 KiB of each one mapped here, through native_adreno_line_audit, counted as auditRange counts. The lines the comparison\n'
        '     * brought into the cache are dropped again. kIOReturnUnsupported without a board hook. The caller (the first submissions)\n'
        '     * ends the trust on a dirty line: native_adreno_sync_audit[4]. */\n'
        '    IOReturn auditSynchronized(uint32_t *lines,uint32_t *dirty,uint32_t *stale) const;\n'
        '    /* Up to `count` dwords at iova inside the mapping `handle`, for the submission log: through the kernel\'s mapping when the\n'
        '     * memory has one, else through the board\'s cacheable physical reader (one load and one dropped line a word) and only\n'
        '     * without it through the uncached one, which makes and removes a kernel mapping for every word. */\n'
        '    IOReturn headWords(uint64_t handle,uint64_t iova,uint32_t *out,size_t count) const;\n',
        'audit/headWords decls')
    return text


def vm_cpp(text):
    text = once(text,
        'static bool importedUntouched(const NativeAdrenoMemory *memory) {return memory->imported() && native_adreno_io_coherent;}\n',
        'static bool importedUntouched(const NativeAdrenoMemory *memory) {return memory->imported() && native_adreno_io_coherent;}\n'
        'extern "C" {volatile uint64_t native_adreno_maint_kind[8][3];volatile uint64_t native_adreno_maint_walk[8];\n'
        '    volatile uint32_t native_adreno_sync_skips[2];volatile uint32_t native_adreno_sync_audit[6];volatile uint32_t native_adreno_maint_all=~0u;\n'
        '    volatile uint32_t native_adreno_global_fault[4];}\n'
        'extern "C" boolean_t PE_parse_boot_argn(const char *,void *,int);\n'
        'static bool maintainAll()\n'
        '{\n'
        '    if(native_adreno_maint_all==~0u) {\n'
        '        uint32_t all=0;PE_parse_boot_argn("gpu-maint-all",&all,sizeof all);\n'
        '        native_adreno_maint_all=all?1:0;\n'
        '    }\n'
        '    return native_adreno_maint_all!=0;\n'
        '}\n'
        '/* A synchronized driver buffer is left out of the passes while nothing says otherwise: its writers cleaned what they stored\n'
        ' * (NativeAdrenoMemory::declareSynchronized), so no CPU cache holds a line the GPU has not seen. The audit of the first\n'
        ' * submissions checks that on the machine and ends the trust for good on a dirty line. */\n'
        'static bool synchronizedTrusted() {return !maintainAll() && !native_adreno_sync_audit[4];}\n'
        'static void noteKind(unsigned kind,size_t bytes)\n'
        '{\n'
        '    native_adreno_maint_kind[kind][0]=native_adreno_maint_kind[kind][0]+1;\n'
        '    native_adreno_maint_kind[kind][1]=native_adreno_maint_kind[kind][1]+((bytes+1023)>>10);\n'
        '}\n'
        'static unsigned kindOf(const NativeAdrenoMemory *memory,unsigned permissions)\n'
        '{\n'
        '    if(memory->imported())\n'
        '        return memory->ownerUncached()?NativeAdrenoKindImportUncached:\n'
        '            (permissions&ArmSmmu4KWrite)?NativeAdrenoKindImportWritten:NativeAdrenoKindImportRead;\n'
        '    if(memory->synchronized())return NativeAdrenoKindDriver;\n'
        '    if(memory->system())return memory->writeCombined()?NativeAdrenoKindSystemWc:NativeAdrenoKindSystemCached;\n'
        '    return NativeAdrenoKindKernel;\n'
        '}\n',
        'maint counters')
    # 88241fa: the SMMU's global fault status does not fail this address space.
    text = once(text,
        'ArmSmmu4KStatus NativeAdrenoVm::observe() const\n'
        '{\n'
        '    ArmSmmu4KStreamResult routes;\n'
        '    ArmSmmu4KStatus status=ArmSmmu4KObserveStreams(&io_,streams_,context_,&routes);if(status)return status;',
        'ArmSmmu4KStatus NativeAdrenoVm::notOurFault(ArmSmmu4KStatus status,uint32_t word)\n'
        '{\n'
        '    if(status!=ArmSmmu4KFault)return status;\n'
        '    native_adreno_global_fault[0]=native_adreno_global_fault[0]+1;native_adreno_global_fault[1]=word;\n'
        '    return ArmSmmu4KSuccess;\n'
        '}\n'
        'ArmSmmu4KStatus NativeAdrenoVm::observe() const\n'
        '{\n'
        '    ArmSmmu4KStreamResult routes;\n'
        '    ArmSmmu4KStatus status=notOurFault(ArmSmmu4KObserveStreams(&io_,streams_,context_,&routes),routes.global_fault);if(status)return status;',
        'notOurFault')
    text = once(text,
        '    return ArmSmmu4KObserveStreams(&io_,streams_,context_,&routes);\n}\n',
        '    return notOurFault(ArmSmmu4KObserveStreams(&io_,streams_,context_,&routes),routes.global_fault);\n}\n',
        'observe tail')
    text = once(text,
        '        ArmSmmu4KStreamResult routes;\n'
        '        return ArmSmmu4KObserveStreams(&io_,streams_,context_,&routes);\n',
        '        ArmSmmu4KStreamResult routes;\n'
        '        return notOurFault(ArmSmmu4KObserveStreams(&io_,streams_,context_,&routes),routes.global_fault);\n',
        'observeCheap')
    # attach: care_ table plus the classification state.
    text = once(text,
        '    order_=IONewData(uint32_t,64);\n'
        '    capacity_=first;mapping_capacity_=64;\n'
        '    if(!changes_ || !alias_scratch_ || !mappings_ || !order_ || !index_.allocate()) {freeTables();return kIOReturnNoMemory;}\n'
        '    for(size_t i=0;i<64;++i)mappings_[i]=Mapping{};\n'
        '    free_hint_=0;high_=0;memset(class_spans_,0,sizeof(class_spans_));order_count_[0]=order_count_[1]=0;\n',
        '    order_=IONewData(uint32_t,64);care_=IONewData(uint32_t,64);\n'
        '    capacity_=first;mapping_capacity_=64;\n'
        '    if(!changes_ || !alias_scratch_ || !mappings_ || !order_ || !care_ || !index_.allocate()) {freeTables();return kIOReturnNoMemory;}\n'
        '    for(size_t i=0;i<64;++i)mappings_[i]=Mapping{};\n'
        '    free_hint_=0;high_=0;memset(class_spans_,0,sizeof(class_spans_));order_count_[0]=order_count_[1]=0;\n'
        '    care_count_=0;memset(kind_count_,0,sizeof(kind_count_));memset(kind_work_,0,sizeof(kind_work_));\n'
        '    submit_skips_=complete_skips_=0;mode_=mode();\n',
        'attach care')
    text = once(text,
        '    if(order_)IODeleteData(order_,uint32_t,mapping_capacity_);\n'
        '    index_.release();\n'
        '    changes_=nullptr;alias_scratch_=nullptr;mappings_=nullptr;order_=nullptr;capacity_=mapping_capacity_=0;\n',
        '    if(order_)IODeleteData(order_,uint32_t,mapping_capacity_);\n'
        '    if(care_)IODeleteData(care_,uint32_t,mapping_capacity_);\n'
        '    index_.release();\n'
        '    changes_=nullptr;alias_scratch_=nullptr;mappings_=nullptr;order_=nullptr;care_=nullptr;capacity_=mapping_capacity_=0;care_count_=0;\n',
        'freeTables care')
    text = once(text,
        '    uint32_t *order=IONewData(uint32_t,size);\n'
        '    if(!order) {IODeleteData(made,Mapping,size);return false;}\n'
        '    for(size_t i=0;i<size;++i)made[i]=Mapping{};\n'
        '    memcpy(made,mappings_,sizeof(Mapping)*mapping_capacity_);\n'
        '    memcpy(order,order_,sizeof(uint32_t)*(order_count_[0]+order_count_[1]));\n'
        '    IODeleteData(mappings_,Mapping,mapping_capacity_);IODeleteData(order_,uint32_t,mapping_capacity_);\n'
        '    mappings_=made;order_=order;mapping_capacity_=size;\n',
        '    uint32_t *order=IONewData(uint32_t,size);\n'
        '    if(!order) {IODeleteData(made,Mapping,size);return false;}\n'
        '    uint32_t *care=IONewData(uint32_t,size);\n'
        '    if(!care) {IODeleteData(made,Mapping,size);IODeleteData(order,uint32_t,size);return false;}\n'
        '    for(size_t i=0;i<size;++i)made[i]=i<mapping_capacity_?mappings_[i]:Mapping{};\n'
        '    memcpy(order,order_,sizeof(uint32_t)*(order_count_[0]+order_count_[1]));\n'
        '    memcpy(care,care_,sizeof(uint32_t)*care_count_);\n'
        '    IODeleteData(mappings_,Mapping,mapping_capacity_);IODeleteData(order_,uint32_t,mapping_capacity_);\n'
        '    IODeleteData(care_,uint32_t,mapping_capacity_);\n'
        '    mappings_=made;order_=order;care_=care;mapping_capacity_=size;\n',
        'growMappings care')
    # map(): classify in the current mode, skip the publish for a trusted
    # synchronized buffer, enlist the live record.
    text = once(text,
        '    if(!attached_ || poisoned_)return kIOReturnNotReady;\n'
        '    PhaseClock clock;\n'
        '    const unsigned cls=aliasClass(permissions,writeback,shareability)&15;\n',
        '    if(!attached_ || poisoned_)return kIOReturnNotReady;\n'
        '    PhaseClock clock;\n'
        '    sync();   /* the new mapping is classified in the mode the others are in */\n'
        '    const unsigned cls=aliasClass(permissions,writeback,shareability)&15;\n',
        'map sync')
    text = once(text,
        '    if(coherentMapping(writeback))native_adreno_coherent_skips[0]=native_adreno_coherent_skips[0]+1;\n'
        '    else {\n'
        '        checked=memory->publishForDevice(0,memory->bytes(),permissions&ArmSmmu4KWrite,distrust_write_combined_);\n'
        '        if(checked)return fail(ArmSmmu4KIoError);\n'
        '    }\n',
        '    if(coherentMapping(writeback))native_adreno_coherent_skips[0]=native_adreno_coherent_skips[0]+1;\n'
        '    /* A synchronized buffer was cleaned by whoever stored into it: one more table that maps it finds no line to write back. */\n'
        '    else if(memory->synchronized() && synchronizedTrusted())native_adreno_sync_skips[0]=native_adreno_sync_skips[0]+1;\n'
        '    else {\n'
        '        checked=memory->publishForDevice(0,memory->bytes(),permissions&ArmSmmu4KWrite,distrust_write_combined_);\n'
        '        if(checked)return fail(ArmSmmu4KIoError);\n'
        '    }\n',
        'map skip')
    text = once(text,
        '    insert(record);record->mapped_spans=spans;result_.completed_spans=spans;\n',
        '    insert(record);record->mapped_spans=spans;result_.completed_spans=spans;\n'
        '    enlist(record);\n',
        'map enlist')
    text = once(text,
        '    if(coherentMapping(record->writeback))native_adreno_coherent_skips[1]=native_adreno_coherent_skips[1]+1;\n'
        '    else if(record->permissions&ArmSmmu4KWrite) {\n',
        '    if(coherentMapping(record->writeback))native_adreno_coherent_skips[1]=native_adreno_coherent_skips[1]+1;\n'
        '    /* Whoever reads what the GPU wrote into a synchronized buffer invalidates first, or reads through a non-cacheable view. */\n'
        '    else if(record->memory->synchronized() && synchronizedTrusted()) {\n'
        '        if(record->permissions&ArmSmmu4KWrite)native_adreno_sync_skips[1]=native_adreno_sync_skips[1]+1;\n'
        '    }\n'
        '    else if(record->permissions&ArmSmmu4KWrite) {\n',
        'unmap skip')
    text = once(text,
        '    if(!erase(record))return fail(ArmSmmu4KStateMismatch);\n'
        '    record->mapped_spans=0;result_.completed_spans=spans;\n',
        '    if(!erase(record))return fail(ArmSmmu4KStateMismatch);\n'
        '    delist(record);\n'
        '    record->mapped_spans=0;result_.completed_spans=spans;\n',
        'unmap delist')
    # The work-list machinery and the three rewritten passes, after invalidateTlb.
    text = once(text,
        'IOReturn NativeAdrenoVm::publishMapped()\n'
        '{\n'
        '    if(!attached_ || poisoned_)return kIOReturnNotReady;\n'
        '    for(size_t i=0;i<high_;++i) {\n'
        '        const Mapping &m=mappings_[i];\n'
        '        if(!m.memory)continue;\n'
        '        if(coherentMapping(m.writeback) || importedUntouched(m.memory)) {native_adreno_coherent_skips[2]=native_adreno_coherent_skips[2]+1;continue;}\n'
        '        if(m.memory->publishForDevice(0,m.memory->bytes(),true,distrust_write_combined_,NativeAdrenoPhaseSubmit))return fail(ArmSmmu4KIoError);\n'
        '    }\n'
        '    return kIOReturnSuccess;\n'
        '}\n',
        '/* The mode the mappings were classified in: IO coherence, the two trusts and gpu-maint-all decide which mappings a pass has\n'
        ' * work for. */\n'
        'uint32_t NativeAdrenoVm::mode() const\n'
        '{\n'
        '    return (native_adreno_io_coherent?1u:0u)|(distrust_write_combined_?2u:0u)|(native_adreno_sync_audit[4]?4u:0u)|(maintainAll()?8u:0u);\n'
        '}\n'
        'void NativeAdrenoVm::enlist(Mapping *m)\n'
        '{\n'
        '    const NativeAdrenoMemory *memory=m->memory;\n'
        '    const bool coherent=coherentMapping(m->writeback) || importedUntouched(memory);\n'
        '    const bool written=memory->imported() && (m->permissions&ArmSmmu4KWrite);\n'
        '    unsigned work=0;\n'
        '    if(!coherent && memory->holdsCpuLines(distrust_write_combined_) && !(memory->synchronized() && synchronizedTrusted()))work|=WorkSubmit;\n'
        '    if(written && !coherent && memory->holdsCpuLines(false))work|=WorkComplete;\n'
        '    if(memory->reassertable())work|=WorkReassert;\n'
        '    m->kind=(uint8_t)kindOf(memory,m->permissions);\n'
        '    ++kind_count_[m->kind];\n'
        '    if(work&WorkSubmit)++kind_work_[m->kind];\n'
        '    if(coherent) {++submit_skips_;work|=CountedCoherent;}\n'
        '    if(coherent && written) {++complete_skips_;work|=CountedCoherentWrite;}\n'
        '    m->work=(uint8_t)work;m->care=UINT32_MAX;\n'
        '    if(work&WorkAny) {m->care=(uint32_t)care_count_;care_[care_count_++]=(uint32_t)(m-mappings_);}\n'
        '}\n'
        'void NativeAdrenoVm::delist(Mapping *m)\n'
        '{\n'
        '    --kind_count_[m->kind];\n'
        '    if(m->work&WorkSubmit)--kind_work_[m->kind];\n'
        '    if(m->work&CountedCoherent)--submit_skips_;\n'
        '    if(m->work&CountedCoherentWrite)--complete_skips_;\n'
        '    if(m->care!=UINT32_MAX) {\n'
        '        const uint32_t moved=care_[--care_count_];\n'
        '        care_[m->care]=moved;mappings_[moved].care=m->care;\n'
        '    }\n'
        '    m->care=UINT32_MAX;m->work=0;\n'
        '}\n'
        'void NativeAdrenoVm::sync()\n'
        '{\n'
        '    const uint32_t now=mode();\n'
        '    if(now==mode_)return;\n'
        '    mode_=now;\n'
        '    care_count_=0;memset(kind_count_,0,sizeof(kind_count_));memset(kind_work_,0,sizeof(kind_work_));\n'
        '    submit_skips_=complete_skips_=0;\n'
        '    for(size_t i=0;mappings_ && i<high_;++i)if(mappings_[i].memory && mappings_[i].mapped_spans)enlist(&mappings_[i]);\n'
        '    native_adreno_maint_walk[5]=native_adreno_maint_walk[5]+1;\n'
        '}\n'
        'IOReturn NativeAdrenoVm::publishMapped()\n'
        '{\n'
        '    if(!attached_ || poisoned_)return kIOReturnNotReady;\n'
        '    sync();\n'
        '    native_adreno_maint_walk[0]=native_adreno_maint_walk[0]+1;\n'
        '    native_adreno_maint_walk[2]=native_adreno_maint_walk[2]+objects_;\n'
        '    if(maintainAll()) {\n'
        '        native_adreno_maint_walk[1]=native_adreno_maint_walk[1]+high_;\n'
        '        for(size_t i=0;i<high_;++i) {\n'
        '            const Mapping &m=mappings_[i];\n'
        '            if(!m.memory)continue;\n'
        '            if(coherentMapping(m.writeback) || importedUntouched(m.memory)) {\n'
        '                native_adreno_coherent_skips[2]=native_adreno_coherent_skips[2]+1;\n'
        '                native_adreno_maint_kind[m.kind][2]=native_adreno_maint_kind[m.kind][2]+1;continue;\n'
        '            }\n'
        '            if(!m.memory->holdsCpuLines(distrust_write_combined_)) {native_adreno_maint_kind[m.kind][2]=native_adreno_maint_kind[m.kind][2]+1;continue;}\n'
        '            if(m.memory->publishForDevice(0,m.memory->bytes(),true,distrust_write_combined_,NativeAdrenoPhaseSubmit))return fail(ArmSmmu4KIoError);\n'
        '            noteKind(m.kind,m.memory->bytes());\n'
        '        }\n'
        '        return kIOReturnSuccess;\n'
        '    }\n'
        '    native_adreno_maint_walk[1]=native_adreno_maint_walk[1]+care_count_;\n'
        '    for(size_t i=0;i<care_count_;++i) {\n'
        '        const Mapping &m=mappings_[care_[i]];\n'
        '        if(!(m.work&WorkSubmit))continue;\n'
        '        if(m.memory->publishForDevice(0,m.memory->bytes(),true,distrust_write_combined_,NativeAdrenoPhaseSubmit))return fail(ArmSmmu4KIoError);\n'
        '        noteKind(m.kind,m.memory->bytes());\n'
        '    }\n'
        '    native_adreno_coherent_skips[2]=native_adreno_coherent_skips[2]+(uint32_t)submit_skips_;\n'
        '    for(unsigned k=0;k<NativeAdrenoKindCount;++k)\n'
        '        native_adreno_maint_kind[k][2]=native_adreno_maint_kind[k][2]+(kind_count_[k]-kind_work_[k]);\n'
        '    return kIOReturnSuccess;\n'
        '}\n',
        'publishMapped')
    text = once(text,
        'IOReturn NativeAdrenoVm::acquireImported()\n'
        '{\n'
        '    if(!attached_ || poisoned_)return kIOReturnNotReady;\n'
        '    for(size_t i=0;i<high_;++i) {\n'
        '        const Mapping &m=mappings_[i];\n'
        '        if(!m.memory || !m.memory->imported() || !(m.permissions&ArmSmmu4KWrite))continue;\n'
        '        if(coherentMapping(m.writeback) || importedUntouched(m.memory)) {native_adreno_coherent_skips[3]=native_adreno_coherent_skips[3]+1;continue;}\n'
        '        if(m.memory->acquireDeviceWrites(0,m.memory->bytes(),NativeAdrenoPhaseComplete))return fail(ArmSmmu4KIoError);\n'
        '    }\n'
        '    return kIOReturnSuccess;\n'
        '}\n',
        'IOReturn NativeAdrenoVm::acquireImported()\n'
        '{\n'
        '    if(!attached_ || poisoned_)return kIOReturnNotReady;\n'
        '    sync();\n'
        '    native_adreno_maint_walk[3]=native_adreno_maint_walk[3]+1;\n'
        '    if(maintainAll()) {\n'
        '        native_adreno_maint_walk[4]=native_adreno_maint_walk[4]+high_;\n'
        '        for(size_t i=0;i<high_;++i) {\n'
        '            const Mapping &m=mappings_[i];\n'
        '            if(!m.memory || !m.memory->imported() || !(m.permissions&ArmSmmu4KWrite))continue;\n'
        '            if(coherentMapping(m.writeback) || importedUntouched(m.memory)) {native_adreno_coherent_skips[3]=native_adreno_coherent_skips[3]+1;continue;}\n'
        '            if(m.memory->acquireDeviceWrites(0,m.memory->bytes(),NativeAdrenoPhaseComplete))return fail(ArmSmmu4KIoError);\n'
        '        }\n'
        '        return kIOReturnSuccess;\n'
        '    }\n'
        '    native_adreno_maint_walk[4]=native_adreno_maint_walk[4]+care_count_;\n'
        '    for(size_t i=0;i<care_count_;++i) {\n'
        '        const Mapping &m=mappings_[care_[i]];\n'
        '        if(!(m.work&WorkComplete))continue;\n'
        '        if(m.memory->acquireDeviceWrites(0,m.memory->bytes(),NativeAdrenoPhaseComplete))return fail(ArmSmmu4KIoError);\n'
        '    }\n'
        '    native_adreno_coherent_skips[3]=native_adreno_coherent_skips[3]+(uint32_t)complete_skips_;\n'
        '    return kIOReturnSuccess;\n'
        '}\n',
        'acquireImported')
    text = once(text,
        'IOReturn NativeAdrenoVm::reassertWriteCombined(uint32_t *reset) const\n'
        '{\n'
        '    if(!reset)return kIOReturnBadArgument;\n'
        '    if(!attached_ || poisoned_)return kIOReturnNotReady;\n'
        '    for(size_t i=0;i<high_;++i) {\n'
        '        const Mapping &m=mappings_[i];\n'
        '        if(!m.memory)continue;\n'
        '        const IOReturn status=m.memory->reassertWriteCombined(reset);\n'
        '        if(status)return status;\n'
        '    }\n'
        '    return kIOReturnSuccess;\n'
        '}\n',
        'IOReturn NativeAdrenoVm::reassertWriteCombined(uint32_t *reset) const\n'
        '{\n'
        '    if(!reset)return kIOReturnBadArgument;\n'
        '    if(!attached_ || poisoned_)return kIOReturnNotReady;\n'
        '    if(maintainAll()) {\n'
        '        native_adreno_maint_walk[6]=native_adreno_maint_walk[6]+high_;\n'
        '        for(size_t i=0;i<high_;++i) {\n'
        '            const Mapping &m=mappings_[i];\n'
        '            if(!m.memory)continue;\n'
        '            const IOReturn status=m.memory->reassertWriteCombined(reset);\n'
        '            if(status)return status;\n'
        '        }\n'
        '        return kIOReturnSuccess;\n'
        '    }\n'
        '    /* Whether a mapping\'s pages can lose the attribute does not depend on the mode: every such mapping is in the list. */\n'
        '    native_adreno_maint_walk[6]=native_adreno_maint_walk[6]+care_count_;\n'
        '    for(size_t i=0;i<care_count_;++i) {\n'
        '        const Mapping &m=mappings_[care_[i]];\n'
        '        if(!(m.work&WorkReassert))continue;\n'
        '        const IOReturn status=m.memory->reassertWriteCombined(reset);\n'
        '        if(status)return status;\n'
        '    }\n'
        '    return kIOReturnSuccess;\n'
        '}\n'
        '/* The page of memory that holds offset: its physical address and how many bytes of the span follow it. */\n'
        'static bool physicalAt(const NativeAdrenoMemory &memory,size_t offset,uint64_t *physical,size_t *run)\n'
        '{\n'
        '    for(size_t j=0;j<memory.spanCount();++j) {\n'
        '        const auto &span=memory.spans()[j];\n'
        '        if(offset<span.offset || offset-span.offset>=span.bytes)continue;\n'
        '        *physical=span.physical+(offset-span.offset);*run=(size_t)(span.bytes-(offset-span.offset));\n'
        '        return true;\n'
        '    }\n'
        '    return false;\n'
        '}\n'
        'IOReturn NativeAdrenoVm::auditSynchronized(uint32_t *lines,uint32_t *dirty,uint32_t *stale) const\n'
        '{\n'
        '    if(!lines || !dirty || !stale)return kIOReturnBadArgument;\n'
        '    if(!attached_ || poisoned_)return kIOReturnNotReady;\n'
        '    if(!native_adreno_line_audit)return kIOReturnUnsupported;\n'
        '    for(size_t i=0;i<high_;++i) {\n'
        '        const Mapping &m=mappings_[i];\n'
        '        if(!m.memory || !m.mapped_spans || !m.memory->synchronized())continue;\n'
        '        const size_t end=m.memory->bytes()<32768?m.memory->bytes():32768;\n'
        '        for(size_t at=0;at<end;at+=4096) {\n'
        '            uint64_t physical=0;size_t run=0;\n'
        '            if(!physicalAt(*m.memory,at,&physical,&run))return kIOReturnBadArgument;\n'
        '            if(native_adreno_line_audit(physical,4096,dirty,stale))return kIOReturnIOError;\n'
        '            *lines+=64;\n'
        '            /* The comparison loaded the page\'s lines through the cacheable aperture: none stays, so a later reader of that\n'
        '             * aperture does not take a copy from now for what DRAM holds then. */\n'
        '            const IOReturn dropped=NativeAdrenoMaintainPhysicalPages(physical,4096,NativeAdrenoCleanInvalidate);\n'
        '            NativeAdrenoNotePhase(NativeAdrenoPhaseDiagnostic,4096);\n'
        '            if(dropped)return dropped;\n'
        '        }\n'
        '    }\n'
        '    return kIOReturnSuccess;\n'
        '}\n'
        'IOReturn NativeAdrenoVm::headWords(uint64_t handle,uint64_t iova,uint32_t *out,size_t count) const\n'
        '{\n'
        '    if(!out || !attached_ || poisoned_)return kIOReturnBadArgument;\n'
        '    for(size_t k=0;k<count;++k)out[k]=0;\n'
        '    const Mapping *m=find(handle);\n'
        '    if(!m || iova<m->iova || iova-m->iova>=m->memory->bytes() || (iova&3))return kIOReturnBadArgument;\n'
        '    const size_t offset=(size_t)(iova-m->iova),bytes=m->memory->bytes();\n'
        '    if(m->memory->mapping()) {\n'
        '        for(size_t k=0;k<count && offset+4*k+4<=bytes;++k)out[k]=static_cast<const volatile uint32_t *>(m->memory->mapping())[offset/4+k];\n'
        '        return kIOReturnSuccess;\n'
        '    }\n'
        '    int (*const read)(uint64_t,uint32_t *)=native_adreno_cached_read32?native_adreno_cached_read32:native_adreno_dram_read32;\n'
        '    if(!read)return kIOReturnUnsupported;\n'
        '    uint64_t physical=0;size_t run=0;\n'
        '    for(size_t k=0;k<count && offset+4*k+4<=bytes;++k,physical+=4,run-=4) {\n'
        '        if(run<4 && !physicalAt(*m->memory,offset+4*k,&physical,&run))return kIOReturnBadArgument;\n'
        '        if(read(physical,&out[k]))return kIOReturnUnsupported;\n'
        '    }\n'
        '    return kIOReturnSuccess;\n'
        '}\n',
        'reassert + audit + headWords')
    return text


def gpuctx_cpp(text):
    text = once(text,
        '    for(unsigned i=0;!status && i<buffer_count_;++i) {\n'
        '        const size_t bytes=a830_ && i==0?GpuA830SqeBytes:a830_ && i==5?GpuAqeBytes:kBytes[i];\n'
        '        status=buffers_[i].allocate(bytes,report->output_bits,true,bytes);\n'
        '    }\n',
        '    for(unsigned i=0;!status && i<buffer_count_;++i) {\n'
        '        const size_t bytes=a830_ && i==0?GpuA830SqeBytes:a830_ && i==5?GpuAqeBytes:kBytes[i];\n'
        '        status=buffers_[i].allocate(bytes,report->output_bits,true,bytes);\n'
        '        /* The CPU\'s users of these buffers keep the GPU\'s view current themselves (a non-cacheable view, as the header asks, or\n'
        '         * a clean after each store through the kernel\'s mapping, as probeBuffer does), so no table that maps them and no\n'
        '         * submission cleans them. Every process table maps all of them; on A740 cleaning their 8548 KiB at each submission was\n'
        '         * 351 of a submission\'s 579 us (macOS run61). The A830 writer set was verified: the ring\'s own publish/acquire hooks,\n'
        '         * the WC-mapped firmware images, and probeBuffer\'s single dword are the only stores; no CP packet targets the scratch\n'
        '         * page (every MEM_WRITE/REG_TO_MEM aims at fence_iova_ or ring_iova_). */\n'
        '        if(!status)status=buffers_[i].declareSynchronized();\n'
        '    }\n',
        'declare synchronized')
    text = once(text,
        '    scratch[0xF00/4]=nop;\n'
        '    *handle=p->handles_[4];*iova=GpuScratchIova+0xF00;*bytes=4;\n',
        '    scratch[0xF00/4]=nop;\n'
        '    /* The store went through the kernel\'s own mapping of the page, which may cache: written back before the CP reads it. */\n'
        '    const IOReturn cleaned=buffers_[4].publishForDevice(0,GpuPageBytes,false,true,NativeAdrenoPhaseWrite);\n'
        '    if(cleaned)return cleaned;\n'
        '    *handle=p->handles_[4];*iova=GpuScratchIova+0xF00;*bytes=4;\n',
        'probe clean')
    # 88241fa: clear the SMMU's global fault status with the context's.
    text = once(text,
        'IOReturn NativeAdrenoGpuContext::endFault(uint32_t out[4])\n'
        '{\n'
        '    if(!out || !ready_)return kIOReturnBadArgument;\n'
        '    out[0]=out[1]=out[2]=out[3]=0;\n'
        '    const ArmSmmu4KIo *io=smmu_.ioHandle();\n'
        '    const uint64_t offset=smmu_.stateHandle()->context_offset;\n',
        'extern "C" {extern volatile uint32_t native_adreno_global_fault[4];}\n'
        'IOReturn NativeAdrenoGpuContext::endFault(uint32_t out[4])\n'
        '{\n'
        '    if(!out || !ready_)return kIOReturnBadArgument;\n'
        '    out[0]=out[1]=out[2]=out[3]=0;\n'
        '    const ArmSmmu4KIo *io=smmu_.ioHandle();\n'
        '    {   /* The global fault status the GPU\'s faulting access left (sGFSR, write one to clear): cleared with the context\'s, or\n'
        '         * every later look at the SMMU reads it. Its last value is kept. */\n'
        '        uint32_t global=0;\n'
        '        if(!io->read32(io->cookie,0x48,&global) && global) {\n'
        '            native_adreno_global_fault[2]=native_adreno_global_fault[2]+1;native_adreno_global_fault[3]=global;\n'
        '            (void)io->write32(io->cookie,0x48,global);\n'
        '        }\n'
        '    }\n'
        '    const uint64_t offset=smmu_.stateHandle()->context_offset;\n',
        'endFault global')
    return text


def publish_cpp(text):
    # 4799715's lock-side parity: the A830 allocateSystem has no pool, but the
    # offer is made for every BoAlloc so the prepared/under-lock contract is
    # identical on both lineages.
    text = once(text,
        '        if(connection->live_)connection->core_->offerSystemPages(pages.descriptor?&pages:nullptr);\n',
        '        /* Offered for every BoAlloc, held or not: allocateSystem then knows it runs under the lock. */\n'
        '        if(connection->live_)connection->core_->offerSystemPages(alloc?&pages:nullptr);\n',
        'offer pages')
    return text


def derive(file, raw):
    text = raw.decode('utf-8').replace('\r\n', '\n')
    if file == 'NativeAdrenoMemory.h':
        text = memory_h(text)
    elif file == 'NativeAdrenoMemory.cpp':
        text = memory_cpp(text)
    elif file == 'NativeAdrenoVm.h':
        text = vm_h(text)
    elif file == 'NativeAdrenoVm.cpp':
        text = vm_cpp(text)
    elif file == 'NativeAdrenoGpuContext.cpp':
        text = gpuctx_cpp(text)
    elif file == 'NativeAdrenoPublish.cpp':
        text = publish_cpp(text)
    else:
        raise ValueError('no maintenance derivation for ' + file)
    return text.encode('utf-8')
