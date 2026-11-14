    /* Diagnostic only: expose the original exit reason before changing policy.
     * Bound independently from IconServices and require the exact native daemon. */
    static unsigned bt_count;
    if (p && !strcmp(proc_best_name(p), "bluetoothd")) {
        static const unsigned char bt_expected[20] = {0xfe,0x7b,0xc1,0x82,0x38,0x6a,0x27,0x66,0xc7,0x57,0x4f,0x8b,0x48,0x41,0x2f,0xf9,0xdf,0x11,0x35,0x64};
        unsigned char bt_actual[20];
        if (!proc_getcdhash(p, bt_actual) && !memcmp(bt_actual, bt_expected, sizeof(bt_expected)))
            return __atomic_add_fetch(&bt_count, 1u, __ATOMIC_RELAXED) <= 16;
    }
