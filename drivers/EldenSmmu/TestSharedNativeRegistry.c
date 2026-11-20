/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host-only physical reset of actual production registry; never native-linked. */
#include "BootRoute.c"
#include <string.h>
void elden_smmu_test_platform_reset(void){
    state=EldenSmmuBootOpen;owner=NULL;intended=published=(EldenSmmuBootRoute){0};
    set_epoch=set_count=set_mode=reservation_ready=0;
    memset(planned_set,0,sizeof(planned_set));memset(published_set,0,sizeof(published_set));
}
