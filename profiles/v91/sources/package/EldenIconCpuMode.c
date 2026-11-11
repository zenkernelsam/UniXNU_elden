/* SPDX-License-Identifier: MIT
 * Opt-in userspace experiment for the exact 21H461 IconServices framework.
 * Select the framework's existing non-CoreImage compositor mode. This is not
 * a GPU driver or a general CoreImage software renderer. Integration is pending.
 */
#include <dlfcn.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <objc/runtime.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include "EldenIconCpuPaths.h"
#if defined(ELDEN_GRAPHIC_VARIANT_CPU) && ELDEN_GRAPHIC_VARIANT_CPU
extern void elden_install_graphic_variant_cpu(void);
#endif
#if SYS_kdebug_trace != 180
#error Review the target kdebug_trace syscall ABI
#endif
// Invoke the real syscall even when the userspace tracing wrapper is disabled.
extern int elden_marker_syscall(int,...) __asm("_syscall");

static const char framework[] =
    "/System/Library/PrivateFrameworks/IconServices.framework/IconServices";
static const uint8_t expected_uuid[16] = {
    0x5b,0xb7,0x47,0x3e,0xda,0x66,0x36,0x8e,
    0x9d,0x87,0x5b,0x2a,0xfd,0x09,0xe2,0x80
};
static const uint8_t expected_getter[8] = {
    0x00,0x08,0x40,0xf9, 0xc0,0x03,0x5f,0xd6
};
static unsigned long cpu_mode(id self, SEL command) {
    (void)self; (void)command;
    return 0;
}
static void note(const char *result) {
    static const char *const statuses[]={"loaded","installed","refused-process","refused-enable",
        "refused-load","refused-uuid","refused-abi","refused-implementation","refused-race","superseded"};
    for(unsigned i=0;i<sizeof(statuses)/sizeof(statuses[0]);++i)
        if(!strcmp(result,statuses[i]))
            (void)elden_marker_syscall(SYS_kdebug_trace,(unsigned long)ELDEN_ICON_TRACE_CODE,
                (unsigned long)(i+1),0UL,1UL,(unsigned long)ELDEN_ICON_TRACE_MAGIC);
    char line[192];
    int n = snprintf(line, sizeof(line),
        "ELDEN_ICON_CPU %s pid=%d mode=0; original non-CI branches\n",
        result, getpid());
    if (n > 0 && (size_t)n < sizeof(line)) {
        int fd=open("/dev/console",O_WRONLY|O_NOCTTY|O_NONBLOCK);
        if (fd>=0) { (void)write(fd,line,(size_t)n); close(fd); }
        else (void)write(STDERR_FILENO,line,(size_t)n);
    }
}
static int match_framework(intptr_t *slide) {
    for (uint32_t i=0; i<_dyld_image_count(); ++i) {
        const char *path=_dyld_get_image_name(i);
        if (!path || strcmp(path,framework)) continue;
        const struct mach_header_64 *h=(const void *)_dyld_get_image_header(i);
        if (!h || h->magic!=MH_MAGIC_64 || h->sizeofcmds>65536 || h->ncmds>1024) return 0;
        const uint8_t *at=(const uint8_t *)(h+1), *end=at+h->sizeofcmds;
        unsigned matched=0;
        for (uint32_t j=0; j<h->ncmds; ++j) {
            if ((size_t)(end-at)<sizeof(struct load_command)) return 0;
            const struct load_command *lc=(const void *)at;
            if (lc->cmdsize<sizeof(*lc) || lc->cmdsize>(size_t)(end-at)) return 0;
            if (lc->cmd==LC_UUID) {
                if (lc->cmdsize!=sizeof(struct uuid_command)) return 0;
                const struct uuid_command *u=(const void *)lc;
                if (memcmp(u->uuid,expected_uuid,16)) return 0;
                ++matched;
            }
            at+=lc->cmdsize;
        }
        if (matched!=1 || at!=end) return 0;
        *slide=_dyld_get_image_vmaddr_slide(i);
        return 1;
    }
    return 0;
}
__attribute__((constructor)) static void install(void) {
    const char *process=getprogname();
    if (!process || strcmp(process,"iconservicesagent")) { note("refused-process"); return; }
    note("loaded");
    const char *enabled=getenv("ELDEN_ICON_CPU_MODE");
    if (!enabled || strcmp(enabled,"0")) { note("refused-enable");return; }
    /* Keep the image loaded for the lifetime of the replacement IMP. */
    if (!dlopen(framework,RTLD_NOW|RTLD_LOCAL)) { note("refused-load"); return; }
    intptr_t slide=0;
    if (!match_framework(&slide)) { note("refused-uuid"); return; }
    Class cls=objc_getClass("ISCompositor");
    Method method=cls?class_getInstanceMethod(cls,sel_registerName("renderingMode")):NULL;
    if (!method || method_getNumberOfArguments(method)!=2 ||
        strcmp(method_getTypeEncoding(method),"Q16@0:8")) { note("refused-abi"); return; }
    IMP before=method_getImplementation(method);
    if ((uintptr_t)before!=(uintptr_t)(UINT64_C(0x1ad28a294)+slide) ||
        memcmp((const void *)before,expected_getter,sizeof(expected_getter))) {
        note("refused-implementation"); return;
    }
    IMP previous=method_setImplementation(method,(IMP)cpu_mode);
    if (previous!=before) {
        /* Only undo our replacement if it has not already been superseded. */
        if (method_getImplementation(method)==(IMP)cpu_mode)
            method_setImplementation(method,previous);
        note("refused-race");return;
    }
    if (method_getImplementation(method)!=(IMP)cpu_mode) { note("superseded");return; }
    note("installed");
#if defined(ELDEN_GRAPHIC_VARIANT_CPU) && ELDEN_GRAPHIC_VARIANT_CPU
    elden_install_graphic_variant_cpu();
#endif
}
