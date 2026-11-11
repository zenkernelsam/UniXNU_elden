// SPDX-License-Identifier: MIT
// Adapted from UniXNU-sheng BootProcessProbe/BootUiTraceCore: bounded, read-only
// saved contexts and user frame chains. This isolated worker never holds a task
// or scheduler lock across a potentially faulting user-memory read.
#include <kern/task.h>
#include <kern/thread.h>
#include <kern/simple_lock.h>
#include <kern/clock.h>
#include <kern/misc_protos.h>
#include <machine/machine_routines.h>
#include <arm/thread.h>
#include <vm/vm_map.h>
#include <pexpert/pexpert.h>
#include <string.h>
struct proc;typedef struct proc *proc_t;
char *proc_best_name(proc_t);int proc_pid(proc_t);task_t proc_task(proc_t);
void proc_iterate(unsigned,int (*)(proc_t,void *),void *,int (*)(proc_t,void *),void *);
extern int elden_usb_preview_line(const char *,unsigned);
extern int elden_icon_cpu_observe_process(proc_t) __attribute__((weak_import));
struct sample {uint64_t pc,lr,fp,wait,cont,trap;unsigned state,index;};
static struct sample samples[64];
static unsigned started,pass;
static unsigned runtimeObserver;
extern unsigned int processor_count;
extern uint32_t processor_avail_count;
static const char *targets[]={"SpringBoard","backboardd","PreBoard","keybagd","mobileactivationd","lsd","runningboardd","cfprefsd","containermanagerd","chronod","liveactivitiesd","pkd","coreduetd","profiled","contextstored"};
static void emit(const char *line) {
    unsigned size=(unsigned)strlen(line);
    for(unsigned i=0;i<100;++i) {
        if(elden_usb_preview_line(line,size))return;
        assert_wait_timeout((event_t)&started,THREAD_UNINT,2,NSEC_PER_MSEC);thread_block(THREAD_CONTINUE_NULL);
    }
}
static int selected(proc_t p,void *unused) {
    (void)unused;const char *name=proc_best_name(p);
    if(runtimeObserver){
        if(elden_icon_cpu_observe_process && elden_icon_cpu_observe_process(p))return 1;
        const char *ui[]={"SpringBoard","backboardd","Preferences","runningboardd","cfprefsd"};
        for(unsigned i=0;i<sizeof(ui)/sizeof(*ui);i++)if(!strcmp(name,ui[i]))return 1;
        return 0;
    }
    for(unsigned i=0;i<sizeof(targets)/sizeof(*targets);++i)if(!strcmp(name,targets[i]))return 1;
    return 0;
}
/* Exact 21H461 libsystem_kernel mach_absolute_time reads offset at
 * 0xfffffc088 and selector at +8. Read its public commpage through the target
 * map, outside locks; never change its selector, offset or CPU registers. */
static void userClock(vm_map_t map,const char *name,int pid) {
    if(strcmp(name,"SpringBoard")&&strcmp(name,"backboardd"))return;
    uint8_t first[16],last[16];uint64_t before,after,counter,offset=0;
    kern_return_t a=vm_map_read_user(map,0x0000000fffffc088ULL,first,sizeof(first));
    clock_get_uptime(&before);
    __asm__ volatile("isb\n\tmrs %0,cntvct_el0":"=r"(counter)::"memory");
    clock_get_uptime(&after);
    kern_return_t b=vm_map_read_user(map,0x0000000fffffc088ULL,last,sizeof(last));
    unsigned stable=a==KERN_SUCCESS&&b==KERN_SUCCESS&&!memcmp(first,last,sizeof(first));
    if(a==KERN_SUCCESS)for(unsigned i=0;i<8;++i)offset|=(uint64_t)first[i]<<(i*8);
    char line[280];
    snprintf(line,sizeof(line),"\nELDEN_USER_CLOCK pass=%u name=%s pid=%d error=%d/%d stable=%u selector=%u offset=%llx vct=%llx before=%llx after=%llx\n",
        pass,name,pid,a,b,stable,a==KERN_SUCCESS?(unsigned)first[8]:255,offset,counter,before,after);
    emit(line);
}
static int capture(proc_t p,void *unused) {
    (void)unused;task_t task=proc_task(p);char name[32];strlcpy(name,proc_best_name(p),sizeof(name));
    if(!task || !task_lock_try(task))return 0;
    vm_map_t map=task->map;if(!map){task_unlock(task);return 0;}vm_map_reference(map);
    unsigned slide=task->task_shared_region_slide,count=0,visited=0;thread_t thread;
    queue_iterate(&task->threads,thread,thread_t,task_threads) {
        if(visited++>=(runtimeObserver?16u:64u))break;
        bool irq=ml_set_interrupts_enabled(false);
        if(!simple_lock_try(&thread->sched_lock,&thread_lck_grp)){ml_set_interrupts_enabled(irq);continue;}
        arm_saved_state_t *state=thread->machine.upcb;
        {
            struct sample *s=&samples[count++];
            if(state && is_saved_state64(state)) {
                s->pc=get_saved_state_pc(state);s->lr=get_saved_state_lr(state);s->fp=get_saved_state_fp(state);
                s->trap=get_saved_state_reg(state,16);
            } else {s->pc=0;s->lr=0;s->fp=0;s->trap=0;}
            s->state=(unsigned)thread->state;
            s->wait=thread->wait_event;s->cont=(uint64_t)thread->continuation;s->index=visited;
        }
        thread_unlock(thread);ml_set_interrupts_enabled(irq);
    }
    task_unlock(task);
    userClock(map,name,proc_pid(p));
    for(unsigned i=0;i<count;++i) {
        struct sample *s=&samples[i];char line[900];
        int n=snprintf(line,sizeof(line),"\nELDEN_WAIT pass=%u name=%s pid=%d thread=%u slide=%x state=%x wait=%llx cont=%llx trap=%llx pc=%llx lr=%llx frames=",
            pass,name,proc_pid(p),s->index,slide,s->state,s->wait,s->cont,s->trap,s->pc,s->lr);
        uint64_t fp=s->fp;unsigned error=0;
        for(unsigned f=0;f<24 && fp;++f) {
            if(fp<4096 || fp>=(1ULL<<39)-16 || (fp&15)){error=1;break;}
            uint64_t pair[2]={0};
            if(vm_map_read_user(map,fp,pair,sizeof(pair))!=KERN_SUCCESS){error=2;break;}
            if(pair[1] && (pair[1]<4096 || pair[1]>=(1ULL<<39))){error=3;break;}
            n+=snprintf(line+n,sizeof(line)-(unsigned)n,"%s%llx",f?",":"",pair[1]);
            if(!pair[0])break;
            if(pair[0]<=fp || pair[0]-fp>1048576){error=4;break;}fp=pair[0];
        }
        snprintf(line+n,sizeof(line)-(unsigned)n," error=%u\n",error);emit(line);
    }
    vm_map_deallocate(map);return 0;
}
static void worker(void *unused,wait_result_t result) {
    (void)unused;(void)result;thread_set_thread_name(current_thread(),"elden_user_wait");
    for(pass=1;pass<=(runtimeObserver?6u:4u);++pass) {
        unsigned delay=pass==1?120:(runtimeObserver?120:30);
        assert_wait_timeout((event_t)&started,THREAD_UNINT,delay,NSEC_PER_SEC);thread_block(THREAD_CONTINUE_NULL);
        char cpu[140];snprintf(cpu,sizeof(cpu),"\nELDEN_CPU pass=%u registered=%u scheduler-available=%u; no frequency writes\n",
            pass,__atomic_load_n(&processor_count,__ATOMIC_RELAXED),__atomic_load_n(&processor_avail_count,__ATOMIC_RELAXED));emit(cpu);
        emit("\nELDEN_WAIT_BEGIN\n");
        // PROC_ALLPROCLIST | PROC_NOWAITTRANS; callbacks execute outside proc_list_lock.
        proc_iterate(1U|4U,capture,NULL,selected,NULL);
        emit("\nELDEN_WAIT_END\n");
    }
    thread_terminate(current_thread());
}
void elden_user_wait_start(void) {
    unsigned enabled=0;PE_parse_boot_argn("elden-user-wait",&enabled,sizeof(enabled));
    PE_parse_boot_argn("elden-ui-replay",&runtimeObserver,sizeof(runtimeObserver));runtimeObserver=runtimeObserver==1;
    if(enabled!=1 || __atomic_exchange_n(&started,1,__ATOMIC_ACQ_REL))return;
    thread_t thread;
    if(kernel_thread_start_priority(worker,NULL,MINPRI_KERNEL,&thread)==KERN_SUCCESS)thread_deallocate(thread);
}
