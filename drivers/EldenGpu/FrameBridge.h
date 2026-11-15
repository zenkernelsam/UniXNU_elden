/* SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
class IOService;class IOMemoryDescriptor;
extern "C" int elden_gpu_framebuffer_start(IOService *nub);
extern "C" void elden_gpu_framebuffer_stop(void);
extern "C" void elden_gpu_framebuffer_block_for_reboot(void);
extern "C" bool elden_gpu_frame_offer(void *,IOMemoryDescriptor *,uint64_t,uint32_t,uint32_t);
extern "C" void elden_gpu_display_ready(IOService *nub);
