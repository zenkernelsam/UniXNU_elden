// SPDX-License-Identifier: MIT
// Included after exec_add_user_string; optional exact-service environment only.
static int
elden_icon_cpu_add_environment(struct image_params *imgp, int pointer_size)
{
    if (!__atomic_load_n(&elden_icon_cpu_ready, __ATOMIC_ACQUIRE) || pointer_size != 8 ||
        strcmp(imgp->ip_strings, EXECUTABLE_KEY ELDEN_ICON_ORIGINAL_PATH)) return 0;
    const char *cursor = imgp->ip_endargv;
    for (int i=0; i<imgp->ip_envc; ++i) {
        if (cursor >= imgp->ip_strendp) return EINVAL;
        const char *end = memchr(cursor, 0, (size_t)(imgp->ip_strendp-cursor));
        if (!end) return EINVAL;
        if (!strncmp(cursor,"DYLD_INSERT_LIBRARIES=",sizeof("DYLD_INSERT_LIBRARIES=")-1) ||
            !strncmp(cursor,"ELDEN_ICON_CPU_MODE=",sizeof("ELDEN_ICON_CPU_MODE=")-1)) {
            printf("ELDEN_ICON_CPU environment refused: existing setting\n");
            return 0;
        }
        cursor=end+1;
    }
    static const char *const values[] = {
        "ELDEN_ICON_CPU_MODE=0"
    };
    for (unsigned i=0; i<sizeof(values)/sizeof(values[0]); ++i) {
        int error=exec_add_user_string(imgp,CAST_USER_ADDR_T(values[i]),UIO_SYSSPACE,TRUE);
        if (error) return error;
        if (imgp->ip_argspace < pointer_size) return E2BIG;
        imgp->ip_argspace -= pointer_size;
        ++imgp->ip_envc;
    }
    printf("ELDEN_ICON_CPU environment appended to original service; argc=%d envc=%d\n",
        imgp->ip_argc,imgp->ip_envc);
    return 0;
}
