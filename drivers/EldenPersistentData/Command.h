/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include "Fence.h"
typedef struct {EldenDataFence fence;uint8_t cdb[10];uint32_t phase,bytes;} EldenDataCommand;
/* Phase1 WRITE(10), phase2 matching SYNCHRONIZE CACHE(10). Success must include
 * the sync; optional data readback happens while the same reservation remains.
 */
bool elden_data_command_write(EldenDataCommand *,uint64_t first,uint32_t blocks);
bool elden_data_command_sync(EldenDataCommand *,bool after_write);
bool elden_data_command_allowed(const EldenDataCommand *,uint8_t lun,const uint8_t *,unsigned length,unsigned direction,uint32_t bytes);
void elden_data_command_end(EldenDataCommand *,bool complete_and_verified);
