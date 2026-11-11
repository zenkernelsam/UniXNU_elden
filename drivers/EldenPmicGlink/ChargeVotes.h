/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {ELDEN_VOTE_OWNER=32781,ELDEN_VOTE_TABLES=12,ELDEN_VOTERS=32};
typedef struct {uint32_t effective,mask,winner,override_id,values[ELDEN_VOTERS];} EldenVote;
typedef struct {
    bool up,pending,failed,ready;
    uint32_t epoch,cycle,timeouts,errors,index,tables,voters;
    char names[ELDEN_VOTE_TABLES][13],clients[ELDEN_VOTERS][13];
    EldenVote votes[ELDEN_VOTE_TABLES];
} EldenChargeVotes;
/* The stock debug interface uses 16B requests for both its list and items.
 * Only GET0x15/0x17 exist here. Votable override0x16 is never generated. */
void elden_charge_votes_init(EldenChargeVotes *s);
void elden_charge_votes_link(EldenChargeVotes *s,bool up);
void elden_charge_votes_refresh(EldenChargeVotes *s);
int elden_charge_votes_next(EldenChargeVotes *s,uint8_t out[16]);
void elden_charge_votes_unsent(EldenChargeVotes *s);
void elden_charge_votes_timeout(EldenChargeVotes *s);
int elden_charge_votes_receive(EldenChargeVotes *s,const uint8_t *data,size_t bytes);
#ifdef __cplusplus
}
#endif
