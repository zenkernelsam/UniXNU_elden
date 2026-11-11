/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {uint64_t current,backup,first,last,entries;uint32_t count,entries_crc;uint8_t disk[16];} EldenDataGptHeader;
enum {EldenDataGptOk=0,EldenDataGptHeaderError=-1,EldenDataGptPair=-2,EldenDataGptEntries=-3,EldenDataGptTarget=-4};
int elden_data_gpt_header(const uint8_t *,size_t,uint64_t last_lba,bool backup,EldenDataGptHeader *);
int elden_data_gpt_pair(const EldenDataGptHeader *,const uint8_t *,size_t,
                        const EldenDataGptHeader *,const uint8_t *,size_t);
