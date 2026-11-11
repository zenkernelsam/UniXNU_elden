/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* 21H461 tracepoint semantics from UniXNU-Pagani_SM8750 a84204ec,
 * phone-keyboard-trace.patch. Only UIApplication's generic event point is used.
 * This captures evidence; it does not synthesize or modify user input. */
#ifndef ELDEN_UI_DISPATCH_CORE_H
#define ELDEN_UI_DISPATCH_CORE_H
#include <stdint.h>
#include <stddef.h>
#define ELDEN_UI_EVENT 0x2b8701acu
#define ELDEN_UI_CSC 0x2b87u
#define ELDEN_UI_ROWS 32u
struct elden_ui_event {
    uint64_t time, tid, unique, arg1, arg2;
    int32_t pid;
    uint32_t target;
};
struct elden_ui_capture {
    struct elden_ui_event rows[ELDEN_UI_ROWS];
    uint64_t first, last;
    uint32_t count, total, overflow, targets[2];
};
static inline int elden_ui_is_touch(uint32_t code,uint64_t arg1) {
    return (code & ~3u)==ELDEN_UI_EVENT && (arg1 & 255u)==0;
}
static inline void elden_ui_record(struct elden_ui_capture *c,
    const struct elden_ui_event *e) {
    if(e->target>=2 || e->pid<=0)return;
    if(!c->total)c->first=e->time;
    c->last=e->time;
    if(c->total!=UINT32_MAX)++c->total;
    if(c->targets[e->target]!=UINT32_MAX)++c->targets[e->target];
    if(c->count<ELDEN_UI_ROWS) {
        volatile struct elden_ui_event *d=&c->rows[c->count++];
        d->time=e->time;d->tid=e->tid;d->unique=e->unique;
        d->arg1=e->arg1;d->arg2=e->arg2;d->pid=e->pid;d->target=e->target;
    }
    else if(c->overflow!=UINT32_MAX)++c->overflow;
}
#endif
