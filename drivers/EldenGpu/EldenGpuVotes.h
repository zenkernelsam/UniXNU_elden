/* SPDX-License-Identifier: MIT */
#pragma once
#include "Sm8750GmuCommandDb.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Inputs are an immutable live command-db snapshot and the actual speed fuse.
 * requested_hz must be an exact supported OPP: no guessed bin or clock fallback.
 * Builds messages only, performs no IO. All outputs are transactional.
 * IFPC/ACD stay disabled until their separate board prerequisites are met. */
AdrenoHfiStartupStatus elden_gpu_build_startup(const Sm8750GmuVoteInputs *,
    uint32_t speed_fuse,uint64_t requested_hz,AdrenoHfiStartupConfig *,
    unsigned *initial_level,unsigned *bandwidth_level);
#ifdef __cplusplus
}
#endif
