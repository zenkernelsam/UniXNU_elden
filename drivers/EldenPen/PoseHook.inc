/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Included inside PenPressure after the exact-event helpers. Read the same
 * pending vendor event the original dispatch will append, before forwarding. */
struct Pose {bool valid=false;int16_t x=0,y=0;uint64_t timestamp=0;};
static volatile uint32_t poseConverted,poseMissing,poseRejected;
static uint8_t *eventData(OSObject *event){
    return *reinterpret_cast<uint8_t **>(reinterpret_cast<uint8_t *>(event)+ELDEN_PEN_EVENT_DATA_OFFSET);
}
static int16_t signedByte(uint8_t x){return (int16_t)(x<128?(int)x:(int)x-256);}
static Pose pendingPose(IOService *self){
    Pose result;
    void *expansion=*reinterpret_cast<void **>(reinterpret_cast<uint8_t *>(self)+ELDEN_PEN_DRIVER_EXPANSION_OFFSET);
    if(!expansion)return result;
    OSArray *pending=*reinterpret_cast<OSArray **>(reinterpret_cast<uint8_t *>(expansion)+ELDEN_PEN_PENDING_OFFSET);
    if(!pending||!OSDynamicCast(OSArray,pending)||pending->getCount()>16)return result;
    unsigned matches=0;
    for(unsigned i=0;i<pending->getCount();++i){
        OSObject *event=pending->getObject(i);
        if(!exactEvent(event)||call<uint32_t>(event,GetType)!=1)continue;
        uint8_t *data=eventData(event);
        if(!data||*reinterpret_cast<uint32_t *>(data)<36)continue;
        if(call<int32_t,uint32_t,IOOptionBits>(event,GetInteger,0x10000,0)!=0xffe0||
           call<int32_t,uint32_t,IOOptionBits>(event,GetInteger,0x10001,0)!=1)continue;
        if(++matches!=1)return Pose{};
        if(call<int32_t,uint32_t,IOOptionBits>(event,GetInteger,0x10003,0)!=8)return Pose{};
        const uint8_t *payload=call<uint8_t *,uint32_t,IOOptionBits>(event,27,0x10004,0);
        if(!payload||payload!=data+28||payload[0]!=0xe7||payload[1]!=1)return Pose{};
        result.x=signedByte(payload[2]);result.y=signedByte(payload[3]);
        if(result.x< -60||result.x>60||result.y< -60||result.y>60)return Pose{};
        result.timestamp=call<uint64_t>(event,16);result.valid=result.timestamp!=0;
    }
    return result;
}
static void applyPose(OSObject *event,const Pose &pose){
    if(!pose.valid||call<uint64_t>(event,16)!=pose.timestamp){__atomic_fetch_add(&poseMissing,1u,__ATOMIC_RELAXED);return;}
    uint8_t *data=eventData(event);
    if(!data||*reinterpret_cast<uint32_t *>(data)<ELDEN_PEN_POLAR_MIN_BYTES||
       *reinterpret_cast<uint32_t *>(data+4)!=11||
       *reinterpret_cast<uint32_t *>(data+ELDEN_PEN_ORIENTATION_OFFSET)!=0){
        __atomic_fetch_add(&poseRejected,1u,__ATOMIC_RELAXED);return;
    }
    EldenPenTilt angles;
    if(elden_pen_tilt(pose.x,pose.y,&angles)){__atomic_fetch_add(&poseRejected,1u,__ATOMIC_RELAXED);return;}
    uint32_t *orientation=reinterpret_cast<uint32_t *>(data+ELDEN_PEN_ORIENTATION_OFFSET);
    uint32_t *polar=reinterpret_cast<uint32_t *>(data+ELDEN_PEN_ALTITUDE_OFFSET);
    const uint32_t oldAltitude=polar[0],oldAzimuth=polar[1];
    // The original orientation setter is read-only. This exact bound store
    // mirrors digitizerEventWithPolarOrientation's type change, then uses the
    // original field setters/readbacks. Other union fields remain unchanged.
    *orientation=1;
    call<void,uint32_t,int32_t,IOOptionBits>(event,SetFixed,0xb000f,angles.altitude_degrees,0);
    call<void,uint32_t,int32_t,IOOptionBits>(event,SetFixed,0xb0010,angles.azimuth_degrees,0);
    if(call<int32_t,uint32_t,IOOptionBits>(event,GetFixed,0xb000f,0)!=angles.altitude_degrees||
       call<int32_t,uint32_t,IOOptionBits>(event,GetFixed,0xb0010,0)!=angles.azimuth_degrees){
        polar[0]=oldAltitude;polar[1]=oldAzimuth;*orientation=0;
        __atomic_fetch_add(&poseRejected,1u,__ATOMIC_RELAXED);return;
    }
    __atomic_fetch_add(&poseConverted,1u,__ATOMIC_RELAXED);
}
