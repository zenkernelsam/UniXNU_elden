"""Derives the A830 NativeAdrenoSubmit pair with the A740 submit-timing port.

Donor: Adreno740-ipados27 @ 93adbc1d312ecbabbb3356affe56738ba6184b0b (A740 lineage),
applied to Adreno740 @ 9c34ea46aa991c4fed6f0347a814473ad626de77 (the A830 file
the tree ships). Every anchor must match exactly once in the pinned input; a
silent source drift fails the derivation loudly.

What the port carries: the gpu-audit boot argument gating the write-combine
audit, native_adreno_job_time plus the 8-longest and 16-slowest job tables with
per-job IB bytes, and the stall kick instrumentation (CP progress at the first
wait / at the stage / 500 us later, working-vs-stopped decision, snapshot taken
before the first stage acts, kick-to-retirement time).

A830 deltas honoured, no further semantics invented: noteStall stays 2-arg (the
A830 wait has no waited-fence plumbing), there is no BV progress sample (the
A830 ring has no bvProgressState), no stall_log/stall_ring/stall_fault/findIb2/
findFault additions beyond what the file already has, Job.level is a uint32_t
value on both sides, and release() hands noteJobTime a Job copy like everywhere
else in this file.
"""


def once(source, anchor, replacement, label):
    if source.count(anchor) != 1:
        raise ValueError(label + ' anchor count ' + str(source.count(anchor)))
    return source.replace(anchor, replacement)


def submit_h(text):
    # Public diagnostic counters, as the donor header declares them (the stall
    # kick comment drops the BV-thread clauses the A830 ring cannot provide).
    text = once(text,
        'extern "C" volatile uint32_t native_adreno_wc_distrusted;\n',
        'extern "C" volatile uint32_t native_adreno_wc_distrusted;\n'
        '/* gpu-audit (NativeAdrenoSubmit.cpp): 0 the submissions run no line audit, 1 they do, ~0 the boot argument is not read yet. */\n'
        'extern "C" volatile uint32_t native_adreno_audit_mode;\n'
        '/* The jobs\' GPU time, kept for every job at its retirement (NativeAdrenoSubmit::noteJobTime), with or without diag=1.\n'
        ' * native_adreno_job_time: [0] jobs timed, [1] of them by the CP\'s stamps (gpu-timeline=1 or diag=1), [2] over 8 ms (one frame at\n'
        ' * 120 Hz), [3] over 16 ms, [4] over 33 ms, [5] over 100 ms, [6] the longest (us), [7] rows written to native_adreno_slow_jobs.\n'
        ' * A row of native_adreno_long_jobs (the 8 longest since boot, unordered) and of native_adreno_slow_jobs (the newest 16 over 8 ms, at\n'
        ' * [n & 15]): [0] fence | IBs << 32 | ring << 40 | (priority + 1) << 44 | source << 52 (1 the CP\'s stamps: from after the prologue\'s\n'
        ' * idle wait to after the job\'s IBs; 2 the driver\'s clock: from the later of its submission and the previous retirement seen on its\n'
        ' * ring to its own, an upper bound) | replays << 56, [1] duration (us), [2] IB bytes, [3] pid | GPU role << 32 | GPU level at\n'
        ' * submission << 40, [4] queue, [5] submission time (us), [6] submission to retirement seen (us), [7] the job\'s number in [0]. */\n'
        'extern "C" volatile uint32_t native_adreno_job_time[8];\n'
        'extern "C" volatile uint64_t native_adreno_long_jobs[8][8],native_adreno_slow_jobs[16][8];\n'
        '/* The first stage of a stall (write pointer given again, translations dropped) and what the CP did before it: [0] the fence, [1] us\n'
        ' * it had been pending, [2..7] the CP\'s progress (NativeAdrenoRing::progressState: IB1 base lo/hi, IB2 base lo/hi, IB1 and IB2 dwords\n'
        ' * left) at the fence\'s first timed-out wait, [8..13] at the stage, [14..19] 500 us later, just before the stage acts, [20] bit 0: the\n'
        ' * CP moved between the first wait and the stage, bit 1: it moved in the 500 us, [21] us from the stage to the job\'s retirement seen\n'
        ' * (0: not yet), [22] the job\'s pid, [23] its IBs | ring << 16 | (priority + 1) << 24, [24] its IB bytes, [25] stages recorded. */\n'
        'extern "C" volatile uint32_t native_adreno_stall_kick[26];\n',
        'header diagnostics')
    # Job gains the IB byte total the timing rows carry.
    text = once(text,
        '        uint64_t replay_us=0;         /* when it was last given to the ring again (0: never) */\n',
        '        uint64_t replay_us=0;         /* when it was last given to the ring again (0: never) */\n'
        '        uint32_t ib_bytes=0;          /* the bytes of its IBs (UINT32_MAX at most) */\n',
        'job ib_bytes')
    # Private helpers and state (2-arg noteStall and no waited argument: the
    # A830 call sites do not know a waited fence).
    text = once(text,
        '    void noteStall(uint32_t observed,const NativeAdrenoRingClock *clock);   /* the CP\'s state for a fence that stopped advancing (native_adreno_stall) */\n',
        '    void noteStall(uint32_t observed,const NativeAdrenoRingClock *clock);   /* the CP\'s state for a fence that stopped advancing (native_adreno_stall) */\n'
        '    /* A retired job\'s GPU time into native_adreno_job_time and the two job tables (stamped: NativeAdrenoRing::timeline\'s status). */\n'
        '    void noteJobTime(const Job &job,uint32_t stamped,uint64_t start,uint64_t end);\n'
        '    /* noteStall\'s CP snapshot (native_adreno_stall, _stall_mem and a stall log entry), taken before the first stage acts. */\n'
        '    void snapshot(uint32_t next,uint64_t now,uint64_t pending,NativeAdrenoRing *stalled);\n'
        '    uint32_t stall_begin_cp_[6]{};   /* the CP\'s progress at the stalled fence\'s first timed-out wait */\n'
        '    uint64_t ring_retired_us_[2]{};uint32_t ring_retired_fence_[2]{};   /* the newest retirement seen on each ring */\n',
        'private helpers')
    # Round 3 (7e8d248): the hung-CP snapshot marker and the fault-address search
    # result live beside the stall log the same header documents.
    text = once(text,
        'extern "C" volatile uint32_t native_adreno_stall_kick[26];\n',
        'extern "C" volatile uint32_t native_adreno_stall_kick[26];\n'
        '/* The number of the hang the retained CP snapshot was taken at, 0 for one of an ordinary stall (7e8d248). */\n'
        'extern "C" volatile uint32_t native_adreno_stall_hang;\n'
        '/* Where a stalled job\'s commands named the IB2 the thread stopped in, and where they hold the faulting address\n'
        ' * (NativeAdrenoSubmit.cpp findIb2/findFault): candidates for the board log, not decoded operands. */\n'
        'extern "C" volatile uint32_t native_adreno_stall_find[14],native_adreno_stall_fault[32];\n'
        '/* Where an accepted submission\'s time under the driver\'s lock goes (us, summed): [0] submissions timed,\n'
        ' * [1] validation/retiring/holding, [2] re-assertion and audits, [3] the mappings pass, [4] the submission log,\n'
        ' * [5] the table switch/ring/job record, [6] all of it. */\n'
        'extern "C" volatile uint64_t native_adreno_submit_us[8];\n',
        'header round3')
    return text


def submit_cpp(text):
    # The boot-argument read and the mode the wc audit consults.
    text = once(text,
        'extern "C" {volatile uint32_t native_adreno_wc_audit[6];}\n',
        'extern "C" {volatile uint32_t native_adreno_wc_audit[6];}\n'
        'extern "C" boolean_t PE_parse_boot_argn(const char *,void *,int);\n'
        'extern "C" {volatile uint32_t native_adreno_audit_mode=~0u;}\n'
        'static bool auditsWanted()\n'
        '{\n'
        '    if(native_adreno_audit_mode==~0u) {\n'
        '        uint32_t on=0;PE_parse_boot_argn("gpu-audit",&on,sizeof on);\n'
        '        native_adreno_audit_mode=on?1:0;\n'
        '    }\n'
        '    return native_adreno_audit_mode!=0;\n'
        '}\n',
        'audit mode')
    # Timing tables and the kick timestamp, next to the stall log they join.
    text = once(text,
        'extern "C" {volatile uint64_t native_adreno_stall_log[32][8];volatile uint32_t native_adreno_stall_log_count;}\n',
        'extern "C" {volatile uint64_t native_adreno_stall_log[32][8];volatile uint32_t native_adreno_stall_log_count;}\n'
        'extern "C" {volatile uint32_t native_adreno_job_time[8];volatile uint64_t native_adreno_long_jobs[8][8],native_adreno_slow_jobs[16][8];\n'
        '    volatile uint32_t native_adreno_stall_kick[26];}\n'
        'static uint64_t stall_kick_us;   /* when the last first stage acted (us, the submission clock) */\n',
        'timing tables')
    # noteJobTime, verbatim from the donor (Job.level is a uint32_t on both sides).
    text = once(text,
        'IOReturn NativeAdrenoSubmit::poison()\n',
        '/* Every retired job, whatever the diagnostics: the duration of a job from the CP\'s stamps when its ring wrote them, else from the\n'
        ' * driver\'s clock (see native_adreno_job_time). The 8 longest since boot and the newest 16 over a 120 Hz frame name the job: its\n'
        ' * process, queue, ring, priority, IBs and their size. */\n'
        'void NativeAdrenoSubmit::noteJobTime(const Job &job,uint32_t stamped,uint64_t start,uint64_t end)\n'
        '{\n'
        '    const uint64_t now=clock_now_?clock_now_(clock_cookie_):0;\n'
        '    const unsigned r=job.ring<2?job.ring:0;\n'
        '    const bool newest=!ring_retired_fence_[r] || (int32_t)(job.fence-ring_retired_fence_[r])>0;\n'
        '    uint64_t us=0;unsigned source=0;\n'
        '    /* status 31: attached, stamps on, page big enough, acquired, slot read; the stamps count 19.2 MHz ticks. A slot whose end is not\n'
        '     * after its start, or that spans more than a minute, is another fence\'s (64 slots) and is not used. */\n'
        '    if(stamped==31 && end>start && end-start<UINT64_C(19200000)*60) {us=(end-start)*10/192;source=1;}\n'
        '    else if(now && job.submit_us && now>=job.submit_us) {\n'
        '        uint64_t began=job.submit_us;\n'
        '        if(newest && ring_retired_us_[r]>began)began=ring_retired_us_[r];\n'
        '        us=now-began;source=2;\n'
        '    }\n'
        '    if(now && newest) {ring_retired_fence_[r]=job.fence;ring_retired_us_[r]=now;}\n'
        '    if(native_adreno_stall_kick[0]==job.fence && stall_kick_us && now>=stall_kick_us && !native_adreno_stall_kick[21])\n'
        '        native_adreno_stall_kick[21]=(uint32_t)(now-stall_kick_us>UINT32_MAX?UINT32_MAX:now-stall_kick_us);\n'
        '    if(!source)return;\n'
        '    const uint32_t n=native_adreno_job_time[0];\n'
        '    native_adreno_job_time[0]=n+1;\n'
        '    if(source==1)native_adreno_job_time[1]=native_adreno_job_time[1]+1;\n'
        '    if(us>8000)native_adreno_job_time[2]=native_adreno_job_time[2]+1;\n'
        '    if(us>16000)native_adreno_job_time[3]=native_adreno_job_time[3]+1;\n'
        '    if(us>33000)native_adreno_job_time[4]=native_adreno_job_time[4]+1;\n'
        '    if(us>100000)native_adreno_job_time[5]=native_adreno_job_time[5]+1;\n'
        '    if(us>native_adreno_job_time[6])native_adreno_job_time[6]=(uint32_t)(us>UINT32_MAX?UINT32_MAX:us);\n'
        '    const uint64_t row[8]={(uint64_t)job.fence|(uint64_t)(job.word_count/4)<<32|(uint64_t)r<<40|(uint64_t)(uint8_t(job.priority+1)&0xff)<<44|\n'
        '        (uint64_t)source<<52|(uint64_t)(job.replays<15?job.replays:15)<<56,us,job.ib_bytes,\n'
        '        (uint64_t)(uint32_t)job.owner|(uint64_t)(job.role&0xff)<<32|(uint64_t)(job.level&0xffff)<<40,job.queue,job.submit_us,\n'
        '        now>job.submit_us && job.submit_us?now-job.submit_us:0,n};\n'
        '    unsigned slot=0;\n'
        '    for(unsigned i=1;i<8;++i)if(native_adreno_long_jobs[i][1]<native_adreno_long_jobs[slot][1])slot=i;\n'
        '    if(us>native_adreno_long_jobs[slot][1])for(unsigned k=0;k<8;++k)native_adreno_long_jobs[slot][k]=row[k];\n'
        '    if(us>8000) {\n'
        '        const uint32_t m=native_adreno_job_time[7];\n'
        '        for(unsigned k=0;k<8;++k)native_adreno_slow_jobs[m&15][k]=row[k];\n'
        '        native_adreno_job_time[7]=m+1;\n'
        '    }\n'
        '}\n'
        'IOReturn NativeAdrenoSubmit::poison()\n',
        'noteJobTime')
    # Retirement calls it right after the retire trace, like the donor.
    text = once(text,
        '        gpuTraceEvent(GpuTraceRetire,job.owner,job.fence,job.queue,job.ring,job.submit_us,start,end,stamped,job.replays);\n',
        '        gpuTraceEvent(GpuTraceRetire,job.owner,job.fence,job.queue,job.ring,job.submit_us,start,end,stamped,job.replays);\n'
        '        noteJobTime(job,stamped,start,end);\n',
        'retire timing')
    # The IB byte total each submitted job carries.
    text = once(text,
        '    uint32_t *words=scratch_words_;size_t word_count=0;\n',
        '    uint32_t *words=scratch_words_;size_t word_count=0;\n'
        '    uint64_t ib_bytes=0;\n',
        'ib count')
    text = once(text,
        '        words[word_count++]=(uint32_t)(buffer.bytes/4);\n',
        '        words[word_count++]=(uint32_t)(buffer.bytes/4);\n'
        '        ib_bytes+=buffer.bytes;\n',
        'ib add')
    text = once(text,
        '    job->priority=int8_t(next_priority_);job->role=uint8_t(next_role_);\n',
        '    job->priority=int8_t(next_priority_);job->role=uint8_t(next_role_);\n'
        '    job->ib_bytes=(uint32_t)(ib_bytes>UINT32_MAX?UINT32_MAX:ib_bytes);\n',
        'ib store')
    # gpu-audit=0 (the default once the argument was read) ends the line audit.
    text = once(text,
        '        const bool audit_now=audit_tick_<=16;\n',
        '        const bool audit_now=audit_tick_<=16 && auditsWanted();\n',
        'audit gate')
    # noteStall: remember where the CP was when this fence's first wait timed out.
    text = once(text,
        '        stall_fence=next;stall_timeouts=0;stall_stage=0;\n    }\n',
        '        stall_fence=next;stall_timeouts=0;stall_stage=0;\n'
        '        /* Where the CP was at the fence\'s first timed-out wait, to tell at the first stage whether it moved since. */\n'
        '        if(stalled->progressState(stall_begin_cp_))for(unsigned i=0;i<6;++i)stall_begin_cp_[i]=~0u;\n'
        '    }\n',
        'stall begin')
    # The kick instrumentation before the stages act (no BV progress: the A830
    # ring exposes none), then the count and the pre-stage snapshot.
    text = once(text,
        '    stall_last_us=now;\n    const uint64_t pending=now-stall_began_us;\n',
        '    stall_last_us=now;\n'
        '    const uint64_t pending=now-stall_began_us;\n'
        '    const bool kick_due=now && stage_due(stall_stage,0,pending,50000);\n'
        '    /* Before the first stage acts: where the CP is now and 500 us later. Unchanged since the first wait and over the 500 us, the CP\n'
        '     * stopped; changed, it is still working through the job (a long job, not a lost write pointer). The snapshot below is taken\n'
        '     * before the stage too, so its registers are the stall\'s and not the stage\'s result. */\n'
        '    bool working=false;\n'
        '    if(kick_due) {\n'
        '        uint32_t at[6]={},later[6]={};\n'
        '        if(stalled->progressState(at))for(unsigned k=0;k<6;++k)at[k]=~0u;\n'
        '        IODelay(500);\n'
        '        if(stalled->progressState(later))for(unsigned k=0;k<6;++k)later[k]=~0u;\n'
        '        uint32_t moved=0;\n'
        '        for(unsigned k=0;k<6;++k) {\n'
        '            if(at[k]!=stall_begin_cp_[k])moved|=1;\n'
        '            if(later[k]!=at[k])moved|=2;\n'
        '            native_adreno_stall_kick[2+k]=stall_begin_cp_[k];native_adreno_stall_kick[8+k]=at[k];native_adreno_stall_kick[14+k]=later[k];\n'
        '        }\n'
        '        const Job *const owner=find(next);\n'
        '        native_adreno_stall_kick[20]=moved;native_adreno_stall_kick[21]=0;\n'
        '        native_adreno_stall_kick[22]=owner?(uint32_t)owner->owner:0;\n'
        '        native_adreno_stall_kick[23]=owner?(uint32_t)(owner->word_count/4)|(uint32_t)owner->ring<<16|(uint32_t)uint8_t(owner->priority+1)<<24:0;\n'
        '        native_adreno_stall_kick[24]=owner?owner->ib_bytes:0;\n'
        '        native_adreno_stall_kick[1]=pending>UINT32_MAX?UINT32_MAX:(uint32_t)pending;\n'
        '        native_adreno_stall_kick[0]=stall_fence;\n'
        '        native_adreno_stall_kick[25]=native_adreno_stall_kick[25]+1;\n'
        '        /* The CP moved: a long job, not a stop. Nothing is done to a working GPU: the stages below count their time from this\n'
        '         * sample, so they act only after that long without progress. */\n'
        '        if(moved) {working=true;stall_began_us=now;for(unsigned k=0;k<6;++k)stall_begin_cp_[k]=later[k];}\n'
        '    }\n'
        '    ++stall_timeouts;\n'
        '    if(now && snapshot_due(&stall_snapshots,pending))snapshot(next,now,pending,stalled);\n',
        'kick capture')
    # Stage one now requires the CP actually stopped; the kick time is kept.
    text = once(text,
        '    if(now && stage_due(stall_stage,0,pending,50000)) {\n',
        '    if(kick_due && !working) {\n',
        'stage one gate')
    text = once(text,
        '        (void)vm_->invalidateTlb();(void)stalled->kick();\n        log(stall_fence,1,pending,r0&0xffff,w0);\n',
        '        (void)vm_->invalidateTlb();(void)stalled->kick();\n'
        '        stall_kick_us=now;\n'
        '        log(stall_fence,1,pending,r0&0xffff,w0);\n',
        'kick stamp')
    # The tail becomes snapshot(); the stall log row it writes is inlined (the
    # noteStall lambda does not reach it). No waited/stall_ring/stall_fault:
    # none exist on this side.
    text = once(text,
        '    ++stall_timeouts;\n'
        '    if(!now || !snapshot_due(&stall_snapshots,pending))return;\n'
        '    uint32_t words[NativeAdrenoRingStallWords]={},fault[4]={};\n'
        '    if(stalled->stallState(words))return;\n'
        '    log(next,4,pending,words[8]&0xffff,words[9]&0xffff);\n',
        '}\n'
        '/* The CP snapshot of a stalled fence (native_adreno_stall, _stall_mem, and a stall log entry), taken before the first stage acts. */\n'
        'void NativeAdrenoSubmit::snapshot(uint32_t next,uint64_t now,uint64_t pending,NativeAdrenoRing *stalled)\n'
        '{\n'
        '    uint32_t words[NativeAdrenoRingStallWords]={},fault[4]={};\n'
        '    if(stalled->stallState(words))return;\n'
        '    {\n'
        '        const uint32_t n=native_adreno_stall_log_count;volatile uint64_t *slot=native_adreno_stall_log[n&31];\n'
        '        const Job *owner=find(next);\n'
        '        slot[0]=now;slot[1]=next;slot[2]=4u|(uint64_t)stall_timeouts<<8;slot[3]=pending;\n'
        '        slot[4]=(uint64_t)(words[8]&0xffff)|(uint64_t)(words[9]&0xffff)<<16|(uint64_t)stalled->readPointer()<<32|(uint64_t)(stalled->writePointer()&0xffff)<<48;\n'
        '        slot[5]=owner?owner->queue:0;slot[6]=owner?(owner->handles[0]&0xffffffffffffull)|(uint64_t)owner->count<<48:0;slot[7]=n;\n'
        '        native_adreno_stall_log_count=n+1;\n'
        '    }\n',
        'snapshot split')
    # Round 3 (5eb625f + 88241fa + 7e8d248): submit phase times, the synchronized
    # trust audit, headWords for the IB log, the IB2/fault searches and the
    # retained hung snapshot.
    text = once(text,
        'extern "C" {volatile uint32_t native_adreno_stall[2+NativeAdrenoRingStallWords];volatile uint32_t native_adreno_stall_mem[4+3*17+4];}\n',
        'extern "C" {volatile uint32_t native_adreno_stall[2+NativeAdrenoRingStallWords];volatile uint32_t native_adreno_stall_mem[4+3*17+4];}\n'
        'extern "C" {volatile uint32_t native_adreno_stall_find[14];volatile uint32_t native_adreno_stall_fault[32];\n'
        '    volatile uint32_t native_adreno_stall_hang;volatile uint64_t native_adreno_submit_us[8];}\n',
        'round3 externs')
    # The IB2 and fault searches, next to the peeks that share their reader.
    text = once(text,
        'static bool indirectBuffer(uint32_t header) {return (header>>28)==7u && ((header>>16)&0x7fu)==0x3fu && (header&0x3fffu)==3;}\n'
        if 'static bool indirectBuffer' in text else
        'static uint32_t stall_fence,stall_timeouts,stall_stage;',
        '/* Where the stalled job\'s command buffers name the IB2 the thread stopped in (88241fa): every IB1 of the job (the\n'
        ' * CP_INDIRECT_BUFFER packets of its ring words) is read for a CP_INDIRECT_BUFFER whose low address word is the IB2 base\n'
        ' * register\'s. [0] how many were found, [1] the IB1\'s place in the job, [2..3] its address, [4] the packet\'s dword offset in\n'
        ' * it, [5..10] the packet\'s four words and the two before it, [11] IB1s read, [12] dwords read, [13] IB1s that could not be\n'
        ' * read. Nothing found with every IB1 read means the address did not come from the job\'s commands. */\n'
        'static bool indirectBuffer(uint32_t header) {return (header>>28)==7u && ((header>>16)&0x7fu)==0x3fu && (header&0x3fffu)==3;}\n'
        'static void findIb2(const NativeAdrenoVm *vm,const uint32_t *ring_words,size_t ring_count,uint32_t ib2_low)\n'
        '{\n'
        '    uint32_t out[14]={};\n'
        '    static uint32_t chunk[1024];\n'
        '    uint32_t index=0;\n'
        '    for(size_t i=0;i+3<ring_count && index<256;++i) {\n'
        '        if(!indirectBuffer(ring_words[i]))continue;\n'
        '        const uint64_t ib1=(uint64_t)ring_words[i+2]<<32|ring_words[i+1];\n'
        '        const uint32_t dwords=ring_words[i+3]&0xfffffu;\n'
        '        i+=3;++out[11];\n'
        '        uint32_t carry[2]={};\n'
        '        for(uint32_t at=0;at<dwords;at+=1024) {\n'
        '            const uint32_t take=dwords-at<1024?dwords-at:1024;\n'
        '            if(vm->peekWordsUncached(ib1+4ull*at,chunk,take)) {++out[13];break;}\n'
        '            out[12]+=take;\n'
        '            for(uint32_t k=0;k+3<take;++k) {\n'
        '                if(!indirectBuffer(chunk[k]) || chunk[k+1]!=ib2_low)continue;\n'
        '                if(!out[0]) {\n'
        '                    out[1]=index;out[2]=(uint32_t)ib1;out[3]=(uint32_t)(ib1>>32);out[4]=at+k;\n'
        '                    out[5]=k>=2?chunk[k-2]:carry[0];out[6]=k>=1?chunk[k-1]:carry[1];\n'
        '                    for(unsigned w=0;w<4;++w)out[7+w]=chunk[k+w];\n'
        '                }\n'
        '                ++out[0];\n'
        '            }\n'
        '            carry[0]=chunk[take>=2?take-2:0];carry[1]=chunk[take-1];\n'
        '        }\n'
        '        ++index;\n'
        '    }\n'
        '    for(unsigned i=0;i<14;++i)native_adreno_stall_find[i]=out[i];\n'
        '}\n'
        '/* Where the stalled job\'s commands hold the faulting address (7e8d248): every IB1 of the job and every IB2 those name (up\n'
        ' * to 512) are read for dwords equal to the fault\'s low address word. [0] matches, [1] of those followed by a 0 word (a 64-bit\n'
        ' * address with no upper word), [2] level of the first match (1 IB1, 2 IB2), [3..4] that IB\'s address low/high, [5] the\n'
        ' * match\'s dword offset in it, [6..15] the eight words before it, the match and the word after. [16] the nearest dword below\n'
        ' * the fault within 1 MiB followed by a 0 word (a base the faulting fetch could have offset from), [17] its distance, [18]\n'
        ' * its level, [19..20] its IB, [21] its offset, [22..29] the six words before it, it and the word after. [30] IBs read, [31]\n'
        ' * dwords read. The fault low word is searched only when its upper word is 0. */\n'
        'static void findFault(const NativeAdrenoVm *vm,const uint32_t *ring_words,size_t ring_count,uint32_t far_low)\n'
        '{\n'
        '    uint32_t out[32]={};\n'
        '    static uint32_t chunk[1024];\n'
        '    static uint64_t ib2s[512];static uint32_t ib2_size[512];\n'
        '    uint32_t ib2_count=0,best=0xffffffffu;\n'
        '    auto scan=[&](uint64_t ib,uint32_t dwords,uint32_t level) {\n'
        '        ++out[30];\n'
        '        for(uint32_t at=0;at<dwords;at+=1024) {\n'
        '            const uint32_t take=dwords-at<1024?dwords-at:1024;\n'
        '            if(vm->peekWordsUncached(ib+4ull*at,chunk,take))return;\n'
        '            out[31]+=take;\n'
        '            for(uint32_t k=0;k<take;++k) {\n'
        '                const uint32_t w=chunk[k],next=k+1<take?chunk[k+1]:1;\n'
        '                if(level==1 && k+3<take && indirectBuffer(w) && ib2_count<512 && chunk[k+2]) {\n'
        '                    ib2s[ib2_count]=(uint64_t)chunk[k+2]<<32|chunk[k+1];ib2_size[ib2_count++]=chunk[k+3]&0xfffffu;\n'
        '                }\n'
        '                if(w==far_low) {\n'
        '                    if(!out[0]) {\n'
        '                        out[2]=level;out[3]=(uint32_t)ib;out[4]=(uint32_t)(ib>>32);out[5]=at+k;\n'
        '                        for(unsigned i=0;i<10;++i) {const int x=(int)k-8+(int)i;out[6+i]=x>=0 && x<(int)take?chunk[x]:0xdeadbeef;}\n'
        '                    }\n'
        '                    ++out[0];if(!next)++out[1];\n'
        '                }\n'
        '                if(w<far_low && far_low-w<=(1u<<20) && !next && far_low-w<best) {\n'
        '                    best=far_low-w;out[16]=w;out[17]=best;out[18]=level;out[19]=(uint32_t)ib;out[20]=(uint32_t)(ib>>32);out[21]=at+k;\n'
        '                    for(unsigned i=0;i<8;++i) {const int x=(int)k-6+(int)i;out[22+i]=x>=0 && x<(int)take?chunk[x]:0xdeadbeef;}\n'
        '                }\n'
        '            }\n'
        '        }\n'
        '    };\n'
        '    for(size_t i=0;i+3<ring_count;++i) {\n'
        '        if(!indirectBuffer(ring_words[i]))continue;\n'
        '        scan((uint64_t)ring_words[i+2]<<32|ring_words[i+1],ring_words[i+3]&0xfffffu,1);\n'
        '        i+=3;\n'
        '    }\n'
        '    for(uint32_t i=0;i<ib2_count;++i)scan(ib2s[i],ib2_size[i],2);\n'
        '    for(unsigned i=0;i<32;++i)native_adreno_stall_fault[i]=out[i];\n'
        '}\n'
        'static uint32_t stall_fence,stall_timeouts,stall_stage;',
        'find searches')
    # The phases' clock at the top of submit().
    text = once(text,
        '    if(!buffers || !count || count>SubmitMaxBuffers)return kIOReturnBadArgument;\n'
        '    native_adreno_submit_ibs[0]=native_adreno_submit_ibs[0]+1;',
        '    if(!buffers || !count || count>SubmitMaxBuffers)return kIOReturnBadArgument;\n'
        '    /* The phases\' clock: each lap takes the time since the one before; an accepted submission adds them to\n'
        '     * native_adreno_submit_us. */\n'
        '    uint64_t lap_us=clock && clock->now_us?clock->now_us(clock->cookie):0,laps[6]={};\n'
        '    const uint64_t began_us=lap_us;\n'
        '    auto lap=[&](unsigned slot) {\n'
        '        if(!began_us)return;\n'
        '        const uint64_t now=clock->now_us(clock->cookie);\n'
        '        laps[slot]=now-lap_us;lap_us=now;\n'
        '    };\n'
        '    native_adreno_submit_ibs[0]=native_adreno_submit_ibs[0]+1;',
        'lap machinery')
    text = once(text,
        '        ++stats_.references_held;\n    }\n    uint32_t value=0;\n',
        '        ++stats_.references_held;\n    }\n    lap(1);\n    uint32_t value=0;\n',
        'lap 1')
    # The synchronized trust audit after the wc audit, plus lap(2): the driver's
    # own buffers are checked on the first 8 submissions and every 1024th.
    text = once(text,
        '    /* The coherence sample reads the first IB\'s word through a fresh uncached mapping before and after the publication: every 64th\n',
        '    /* The driver\'s own buffers (ring, memptrs, SQE, AQE, power-up, scratch) are not cleaned at every submission: their writers\n'
        '     * keep the GPU\'s view current (NativeAdrenoMemory::declareSynchronized). The first 8 submissions and every 1024th after them\n'
        '     * compare the head of each one as a CPU cache and DRAM hold it; a dirty line ends that trust for good, and from this\n'
        '     * submission on they are cleaned every time. */\n'
        '    if((native_adreno_submit_ibs[0]<=8 || !(native_adreno_submit_ibs[0]&1023)) && !native_adreno_sync_audit[4]) {\n'
        '        uint32_t lines=0,dirty=0,stale=0;\n'
        '        const IOReturn audited=vm_->auditSynchronized(&lines,&dirty,&stale);\n'
        '        if(audited!=kIOReturnUnsupported) {   /* no board hook (host): nothing to compare */\n'
        '            native_adreno_sync_audit[0]=native_adreno_sync_audit[0]+1;\n'
        '            native_adreno_sync_audit[1]=native_adreno_sync_audit[1]+lines;\n'
        '            native_adreno_sync_audit[2]=native_adreno_sync_audit[2]+dirty;\n'
        '            native_adreno_sync_audit[3]=native_adreno_sync_audit[3]+stale;\n'
        '            if(audited)native_adreno_sync_audit[5]=(uint32_t)audited;\n'
        '            if(dirty || audited)native_adreno_sync_audit[4]=native_adreno_sync_audit[0];\n'
        '        }\n'
        '    }\n'
        '    lap(2);\n'
        '    /* The coherence sample reads the first IB\'s word through a fresh uncached mapping before and after the publication: every 64th\n',
        'sync audit')
    text = once(text,
        '    /* The ring this job goes to (the first ring alone unless the board turned the switch on). */\n'
        '    const unsigned index=preemption()?ringForPriority():0;\n',
        '    lap(3);\n'
        '    /* The ring this job goes to (the first ring alone unless the board turned the switch on). */\n'
        '    const unsigned index=preemption()?ringForPriority():0;\n',
        'lap 3')
    # The submission log reads the IB head only under diag, through headWords.
    text = once(text,
        '        const uint32_t slot=native_adreno_ib_log_count&3;uint32_t head[28];\n'
        '        (void)vm_->peekWords(buffers[0].iova,head,28);\n',
        '        const uint32_t slot=native_adreno_ib_log_count&3;uint32_t head[28]={};\n'
        '        if(native_adreno_diag)(void)vm_->headWords(buffers[0].handle,buffers[0].iova,head,28);\n',
        'headWords')
    text = once(text,
        '        native_adreno_ib_log_count=native_adreno_ib_log_count+1;\n    }\n'
        '    status=useVm(vm,clock);\n',
        '        native_adreno_ib_log_count=native_adreno_ib_log_count+1;\n    }\n'
        '    lap(4);\n'
        '    status=useVm(vm,clock);\n',
        'lap 4')
    text = once(text,
        '    *fence=value;\n    driveSwitch(clock);\n    return kIOReturnSuccess;\n}\n'
        'IOReturn NativeAdrenoSubmit::observeFence',
        '    *fence=value;\n    driveSwitch(clock);\n'
        '    lap(5);\n'
        '    if(began_us) {\n'
        '        native_adreno_submit_us[0]=native_adreno_submit_us[0]+1;\n'
        '        for(unsigned i=1;i<6;++i)native_adreno_submit_us[i]=native_adreno_submit_us[i]+laps[i];\n'
        '        native_adreno_submit_us[6]=native_adreno_submit_us[6]+(lap_us-began_us);\n'
        '    }\n'
        '    return kIOReturnSuccess;\n}\n'
        'IOReturn NativeAdrenoSubmit::observeFence',
        'lap 5')
    # The retained hung snapshot (7e8d248): a hang's snapshot stays until the next
    # hang; snapshot() skips while one is retained.
    text = once(text,
        '    uint32_t words[NativeAdrenoRingStallWords]={},fault[4]={};\n'
        '    if(stalled->stallState(words))return;\n'
        '    {\n'
        '        const uint32_t n=native_adreno_stall_log_count;',
        '    uint32_t words[NativeAdrenoRingStallWords]={},fault[4]={};\n'
        '    if(native_adreno_stall_hang)return;\n'
        '    if(stalled->stallState(words))return;\n'
        '    {\n'
        '        const uint32_t n=native_adreno_stall_log_count;',
        'hang retain')
    text = once(text,
        '    peekFetch(space,(uint64_t)words[21]<<32|words[20],&native_adreno_stall_mem[4+34]);\n'
        '    {\n'
        '        uint64_t brIb=',
        '    peekFetch(space,(uint64_t)words[21]<<32|words[20],&native_adreno_stall_mem[4+34]);\n'
        '    if(job && job->word_count)findIb2(space,job->words,job->word_count,words[13]);\n'
        '    for(unsigned i=0;i<32;++i)native_adreno_stall_fault[i]=0;\n'
        '    if(job && job->word_count && fault[0] && !fault[2] && fault[1])findFault(space,job->words,job->word_count,fault[1]);\n'
        '    {\n'
        '        uint64_t brIb=',
        'find calls')
    text = once(text,
        '    progress_retries_=0;++stats_.hangs;*hung=true;\n    return kIOReturnSuccess;\n}\n'
        'IOReturn NativeAdrenoSubmit::recover',
        '    progress_retries_=0;++stats_.hangs;*hung=true;\n'
        '    /* Where the CP stopped, before the restart clears it. */\n'
        '    native_adreno_stall_hang=0;\n'
        '    snapshot(fence+1,clock_now_?clock_now_(clock_cookie_):0,0,r);\n'
        '    native_adreno_stall_hang=(uint32_t)stats_.hangs;\n'
        '    return kIOReturnSuccess;\n}\n'
        'IOReturn NativeAdrenoSubmit::recover',
        'hang snapshot')
    return text


def derive(cpp_bytes, h_bytes):
    cpp = cpp_bytes.decode('utf-8')
    header = h_bytes.decode('utf-8')
    return submit_cpp(cpp).encode('utf-8'), submit_h(header).encode('utf-8')
