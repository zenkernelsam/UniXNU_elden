/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "TextGuard.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    const uint8_t old[]={0xa0,5,0,0x34},replacement[]={0x1f,0x20,3,0xd5};
    uint8_t digest[20]={0xfe,0x7b,0xc1,0x82,0x38,0x6a,0x27,0x66,0xc7,0x57,0x4f,0x8b,0x48,0x41,0x2f,0xf9,0xdf,0x11,0x35,0x64};
    assert(elden_bt_stream_rule(0xddd20,4,old,replacement));
    assert(!elden_bt_stream_rule(0x2ef860,4,old,replacement));
    assert(!elden_bt_stream_rule(0xddd20,8,old,replacement));
    assert(!elden_bt_stream_rule(0xddd20,4,replacement,replacement));
    assert(!elden_bt_stream_rule(0xddd20,4,old,old));
    assert(elden_bt_stream_identity(0x100000c,0,0,digest));
    assert(!elden_bt_stream_identity(0x100000c,2,0,digest));
    assert(!elden_bt_stream_identity(0x100000c,0,4096,digest));
    assert(!elden_bt_stream_identity(0,0,0,digest));
    for(unsigned i=0;i<20;i++){digest[i]^=1;assert(!elden_bt_stream_identity(0x100000c,0,0,digest));digest[i]^=1;}
    puts("TextGuard: approved F3 only; CPU/slice and all20 identity bytes checked");
}
