/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Independent original-command decoder reused under actual native methods. */
#define main previous_cci_main
#include "TestCci.c"
#undef main
void *elden_camera_cci_peer_create(unsigned controller,int sensor){CciPeer *p=calloc(1,sizeof(*p));CHECK(p);setup(p,controller);p->sensor_model=sensor;return p;}
int elden_camera_cci_peer_read(void *v,unsigned reg,uint32_t *out){return read_reg(v,reg,out);}
int elden_camera_cci_peer_write(void *v,unsigned reg,uint32_t value){return write_reg(v,reg,value);}
uint64_t elden_camera_cci_peer_now(void *v){return ((CciPeer *)v)->now;}
void elden_camera_cci_peer_delay(void *v,unsigned us){((CciPeer *)v)->now+=us;}
void elden_camera_cci_peer_fault(void *v,unsigned fault){CciPeer *p=v;
    if(fault==1)p->stall_halt=1;if(fault==2)p->status_error=1u<<27;if(fault==3)p->reg[0]++;
    if(fault==4)p->fail_at=p->ops+1;if(fault==5)p->stall_queue=1;
}
unsigned elden_camera_cci_peer_stat(void *v,unsigned field){CciPeer *p=v;
    if(field==0)return p->starts;if(field==1)return p->reset_count;if(field==2)return p->stream_enabled;
    if(field==3)return p->sensor_writes;if(field==4)return p->sensor_reads;if(field==5)return p->data_port_writes;return p->writes;
}
void elden_camera_cci_peer_destroy(void *v){free(v);}
