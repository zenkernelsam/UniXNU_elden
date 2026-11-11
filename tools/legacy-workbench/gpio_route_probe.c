/* SPDX-License-Identifier: MIT */
/* Android GPIO character-device inventory. No line request or value write. */
#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc != 2 || strcmp(argv[1], "--inspect")) return 2;
    unsigned found = 0;
    for (unsigned i = 0; i < 10; ++i) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/gpiochip%u", i);
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) { printf("open path=%s errno=%d\n", path, errno); continue; }
        struct gpiochip_info info = {0};
        if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info)) {
            printf("chip path=%s errno=%d\n", path, errno); close(fd); continue;
        }
        ++found;
        printf("chip path=%s name=%.32s label=%.32s lines=%u\n", path, info.name, info.label, info.lines);
        const unsigned offsets[] = {0, 3, 29};
        for (unsigned k = 0; k < sizeof(offsets)/sizeof(offsets[0]); ++k) {
            if (offsets[k] >= info.lines) continue;
            struct gpio_v2_line_info line = {0}; line.offset = offsets[k];
            if (ioctl(fd, GPIO_V2_GET_LINEINFO_IOCTL, &line)) {
                printf("line chip=%u offset=%u errno=%d\n", i, offsets[k], errno); continue;
            }
            printf("line chip=%u offset=%u name=%.32s consumer=%.32s flags=%llx attrs=%u\n",
                i, offsets[k], line.name, line.consumer, (unsigned long long)line.flags, line.num_attrs);
        }
        close(fd);
    }
    return found ? 0 : 1;
}
