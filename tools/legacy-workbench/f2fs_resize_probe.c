/* Read-only admission probe, not a resize tool. Linux arm64 raw syscalls.
 * Requests 2^40 blocks only after proving the current filesystem is smaller.
 * A conforming F2FS resize handler must reject growth with EINVAL before GC.
 * Never accepts a user-specified target size or edits the partition table. */
typedef unsigned long u64;
static long syscall4(long n, long a, long b, long c, long d)
{
    register long x8 __asm__("x8") = n, x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b, x2 __asm__("x2") = c, x3 __asm__("x3") = d;
    __asm__ volatile("svc 0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}
static void say(const char *s)
{
    long size = 0;
    while (s[size]) size++;
    syscall4(64, 1, (long)s, size, 0);
}
int main(int argc, char **argv)
{
    if (argc != 2) { say("usage: f2fs-resize-probe /data\n"); return 2; }
    static char uts[6 * 65];
    static const char release[] = "6.6.118-android15-8-g350f378eb496-ab14943902-4k";
    if (syscall4(160, (long)uts, 0, 0, 0) < 0) {
        say("REFUSED: cannot identify kernel\n"); return 1;
    }
    for (unsigned i = 0; i < sizeof(release); ++i) {
        if (uts[130 + i] != release[i]) {
            say("REFUSED: kernel differs from the audited stock binary\n"); return 1;
        }
    }
    u64 info[32] = {0};
    if (syscall4(43, (long)argv[1], (long)info, 0, 0) < 0 ||
        info[0] != 0xf2f52010 || info[1] != 4096 || info[2] >= ((u64)1 << 40)) {
        say("REFUSED: not F2FS or unexpected filesystem size\n"); return 1;
    }
    long fd = syscall4(56, -100, (long)argv[1], 040000, 0);
    if (fd < 0) { say("REFUSED: cannot open mountpoint\n"); return 1; }
    u64 impossible_growth = (u64)1 << 40;
    long result = syscall4(29, fd, 0x4008f510UL, (long)&impossible_growth, 0);
    syscall4(57, fd, 0, 0, 0);
    if (result == -22) { say("EINVAL: matches audited growth rejection; no resize success claimed\n"); return 0; }
    if (result == -25) { say("ENOTTY: resize ioctl unavailable\n"); return 1; }
    if (result == -1 || result == -30) { say("INCONCLUSIVE: permission/read-only rejection\n"); return 1; }
    say("INCONCLUSIVE: unexpected ioctl result; no partition changes permitted\n"); return 1;
}
__attribute__((naked, noreturn)) void _start(void)
{
    __asm__ volatile("ldr x0, [sp]\nadd x1, sp, #8\nbl main\nmov x8, #93\nsvc 0\n");
}
