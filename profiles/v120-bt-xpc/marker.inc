    /* Read-only, fixed-format diagnostics; never changes XPC/exit outcomes. */
    if (code == 0xfe700018u) {
        static unsigned bt_markers;
        static const unsigned char expected[20] = {0xfe,0x7b,0xc1,0x82,0x38,0x6a,0x27,0x66,0xc7,0x57,0x4f,0x8b,0x48,0x41,0x2f,0xf9,0xdf,0x11,0x35,0x64};
        unsigned char actual[20];
        if (!p || magic != 0x4254 || status < 1 || status > 2 ||
            strcmp(proc_best_name(p), "bluetoothd") || proc_getcdhash(p, actual) ||
            memcmp(actual, expected, sizeof(expected)) ||
            __atomic_add_fetch(&bt_markers,1u,__ATOMIC_RELAXED) > 16) return;
        char text[128] = {0}; size_t copied = 0;
        int error = copyinstr((user_addr_t)version,text,sizeof(text),&copied);
        if (error) text[0] = 0;
        printf("ELDEN_BT_XPC pid=%d stage=%llu result=%llx text-error=%d text=%.127s\n",
               proc_pid(p),(unsigned long long)status,(unsigned long long)mode,error,text);
        return;
    }
