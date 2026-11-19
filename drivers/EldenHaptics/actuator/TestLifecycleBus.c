/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Actual AW/GENI driver with the existing register/delay peer. No hardware. */
#define main prior_bus_main
#include "../TestBus.c"
#undef main
void *elden_actuator_peer_create(EldenAwChip chips[2],const uint8_t *fw,size_t n){
    Model *m=calloc(1,sizeof(*m));CHECK(m);initial(m);acquire(m);m->firmware=fw;m->firmware_bytes=n;
    EldenAwIo io={m,chip_read,chip_write,chip_admit,chip_firmware,now,chip_delay};
    for(unsigned i=0;i<2;i++){
        CHECK(!elden_aw_bind(&chips[i],&io,(uint8_t)(0x5a+i)));
        CHECK(!elden_aw_initialize(&chips[i],fw,n,64));m->motor[i].reg[1]=16;
    }
    return m;
}
uint64_t elden_actuator_peer_now(void *v){return ((Model *)v)->now;}
void elden_actuator_peer_advance(void *v,unsigned us){((Model *)v)->now+=us;}
void elden_actuator_peer_fail_stop(void *v){Model *m=v;m->stall=m->drop_cancel=m->drop_abort=1;}
unsigned elden_actuator_peer_starts(void *v){Model *m=v;return m->motor[0].starts+m->motor[1].starts;}
void elden_actuator_peer_destroy(void *v){free(v);}
