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
    return text


def derive(cpp_bytes, h_bytes):
    cpp = cpp_bytes.decode('utf-8')
    header = h_bytes.decode('utf-8')
    return submit_cpp(cpp).encode('utf-8'), submit_h(header).encode('utf-8')
