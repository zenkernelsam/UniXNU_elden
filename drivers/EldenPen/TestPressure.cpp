/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Execute the actual hook with explicit IOHID/registry/memory peers. */
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstddef>
#include "HidReport.h"
#include "Tilt.h"
using IOOptionBits=unsigned;
static unsigned checks,forwards;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct OSArray;
struct OSObject {
    uintptr_t *table;uint64_t unused[2]={};OSArray *children=nullptr;
    int kind=1;int32_t pressure=0,transducer=0;unsigned type=11;
    uint8_t padding[208]={};void *expansion=nullptr;uint64_t timestamp=0;
    bool metaCast(const char *){return kind==1;}
};
static_assert(offsetof(OSObject,children)==24,"Exact target event child offset modeled");
static_assert(offsetof(OSObject,expansion)==256,"Exact driver expansion pointer offset modeled");
struct IORegistryEntry:OSObject {unsigned references=0;void retain(){references++;}};
struct IOService:IORegistryEntry {};
struct OSArray:OSObject {
    static constexpr int Kind=2;OSObject *items[17]{};unsigned count=0;
    OSArray(){kind=Kind;}unsigned getCount(){return count;}OSObject *getObject(unsigned i){CHECK(i<count);return items[i];}
};
#define OSDynamicCast(T,x) ((x)&&(x)->kind==T::Kind?static_cast<T *>(x):nullptr)
static uintptr_t driverTable[220],eventTable[37];
static bool allocationFail;
static void *allocated;
static void *IOMalloc(size_t size){CHECK(size==220*sizeof(uintptr_t));if(allocationFail)return nullptr;return allocated=std::malloc(size);}
static void original(IOService *,OSObject *,IOOptionBits){forwards++;}
#define ELDEN_PEN_DRIVER_VPTR reinterpret_cast<uintptr_t>(driverTable)
#define ELDEN_PEN_EVENT_VPTR reinterpret_cast<uintptr_t>(eventTable)
#define ELDEN_PEN_ORIGINAL_DISPATCH reinterpret_cast<uintptr_t>(&original)
#define ELDEN_PEN_EVENT_DATA_OFFSET 16
#define ELDEN_PEN_DRIVER_EXPANSION_OFFSET 256
#define ELDEN_PEN_PENDING_OFFSET 368
#define ELDEN_PEN_ORIENTATION_OFFSET 64
#define ELDEN_PEN_ALTITUDE_OFFSET 68
#define ELDEN_PEN_POLAR_MIN_BYTES 76
#include "PressureHook.inc"
static uint32_t getType(void *p){return static_cast<OSObject *>(p)->type;}
static uint8_t *dataOf(void *p){return reinterpret_cast<uint8_t *>(static_cast<OSObject *>(p)->unused[1]);}
static int32_t getInteger(void *p,uint32_t field,IOOptionBits){
    if(field==0xb0004)return static_cast<OSObject *>(p)->transducer;
    uint8_t *data=dataOf(p);CHECK(data);
    if(field==0x10000)return *reinterpret_cast<uint16_t *>(data+16);
    if(field==0x10001)return *reinterpret_cast<uint16_t *>(data+18);
    if(field==0x10003)return *reinterpret_cast<uint32_t *>(data+24);
    CHECK(false);return 0;
}
static int32_t getFixed(void *p,uint32_t field,IOOptionBits){
    if(field==0xb000a)return static_cast<OSObject *>(p)->pressure;
    CHECK(field==0xb000f||field==0xb0010);CHECK(dataOf(p));
    return *reinterpret_cast<int32_t *>(dataOf(p)+(field==0xb000f?68:72));
}
static bool failPoseWrite;
static void setFixed(void *p,uint32_t field,int32_t value,IOOptionBits){
    if(field==0xb000a){static_cast<OSObject *>(p)->pressure=value;return;}
    CHECK(field==0xb000f||field==0xb0010);CHECK(dataOf(p));
    if(!failPoseWrite)*reinterpret_cast<int32_t *>(dataOf(p)+(field==0xb000f?68:72))=value;
}
static uint64_t timestamp(void *p){return static_cast<OSObject *>(p)->timestamp;}
static uint8_t *getData(void *p,uint32_t field,IOOptionBits){CHECK(field==0x10004);return dataOf(p)+28;}
int main(){
    driverTable[198]=(uintptr_t)&original;
    eventTable[16]=(uintptr_t)&timestamp;eventTable[18]=(uintptr_t)&getType;eventTable[23]=(uintptr_t)&getInteger;
    eventTable[25]=(uintptr_t)&getFixed;eventTable[26]=(uintptr_t)&setFixed;eventTable[27]=(uintptr_t)&getData;
    IOService driver,foreign;driver.table=driverTable;foreign.table=driverTable;
    CHECK(!PenPressure::install(nullptr));allocationFail=true;
    CHECK(!PenPressure::install(&driver)&&driver.references==0);allocationFail=false;
    CHECK(PenPressure::install(&driver)&&driver.references==1);
    CHECK(PenPressure::install(&driver)&&driver.references==1&&!PenPressure::install(&foreign));
    uintptr_t *ours=driver.table;driver.table=driverTable;
    CHECK(!PenPressure::install(&driver));driver.table=ours;
    CHECK(driver.table!=driverTable&&driver.table[198]==(uintptr_t)&PenPressure::dispatch);
    for(unsigned i=0;i<220;++i)if(i!=198)CHECK(driver.table[i]==driverTable[i]);
    OSObject event;event.table=eventTable;event.pressure=32768;
    PenPressure::dispatch(&driver,&event,0);CHECK(event.pressure==32768000&&forwards==1);
    PenPressure::dispatch(&driver,&event,0);CHECK(event.pressure==32768000&&forwards==2&&PenPressure::rejected==1);
    event.pressure=10;PenPressure::dispatch(&foreign,&event,0);CHECK(event.pressure==10&&forwards==3);
    event.transducer=1;PenPressure::dispatch(&driver,&event,0);CHECK(event.pressure==10);event.transducer=0;
    OSArray children;OSObject pen,finger;pen.table=finger.table=eventTable;pen.pressure=65536;finger.pressure=500;finger.transducer=1;
    children.count=2;children.items[0]=&pen;children.items[1]=&finger;event.children=&children;
    PenPressure::dispatch(&driver,&event,0);CHECK(pen.pressure==65536000&&finger.pressure==500&&event.pressure==10);
    children.count=17;PenPressure::dispatch(&driver,&event,0);CHECK(PenPressure::rejected==2);
    event.children=nullptr;event.pressure=-1;PenPressure::dispatch(&driver,&event,0);CHECK(event.pressure==-1&&PenPressure::rejected==3);
    uintptr_t unknown[37]{};event.table=unknown;event.pressure=10;PenPressure::dispatch(&driver,&event,0);CHECK(event.pressure==10);
    // Pending metadata has not yet been appended by original dispatch. Use
    // the actual hook on that pending array, with timestamp-bound samples.
    struct Expansion {uint8_t gap[368];OSArray *pending;};Expansion exp={};
    static_assert(offsetof(Expansion,pending)==368,"Original pending-event offset");
    alignas(uint32_t) uint8_t vendorData[64]={},penData[128]={};
    auto word=[](uint8_t *p,unsigned at,uint32_t value){std::memcpy(p+at,&value,4);};
    word(vendorData,0,sizeof(vendorData));word(vendorData,4,1);word(vendorData,16,0x0001ffe0);word(vendorData,24,8);
    vendorData[28]=0xe7;vendorData[29]=1;vendorData[30]=45;
    word(penData,0,sizeof(penData));word(penData,4,11);
    OSObject vendor;vendor.table=eventTable;vendor.type=1;vendor.timestamp=42;vendor.unused[1]=(uintptr_t)vendorData;
    OSArray pending;pending.count=1;pending.items[0]=&vendor;exp.pending=&pending;driver.expansion=&exp;
    pen.unused[1]=(uintptr_t)penData;pen.timestamp=42;pen.pressure=32768;event.table=eventTable;event.children=&children;
    children.count=2;PenPressure::dispatch(&driver,&event,0);
    CHECK(*reinterpret_cast<uint32_t *>(penData+64)==1&&*reinterpret_cast<int32_t *>(penData+68)==45*65536&&*reinterpret_cast<int32_t *>(penData+72)==0);
    CHECK(PenPressure::poseConverted==1&&pending.count==1&&finger.pressure==500);
    word(penData,64,0);word(penData,68,111);pen.pressure=1;vendor.timestamp=43;
    PenPressure::dispatch(&driver,&event,0);CHECK(*reinterpret_cast<uint32_t *>(penData+64)==0&&*reinterpret_cast<int32_t *>(penData+68)==111);
    vendor.timestamp=42;pending.count=2;pending.items[1]=&vendor;pen.pressure=1;
    PenPressure::dispatch(&driver,&event,0);CHECK(*reinterpret_cast<uint32_t *>(penData+64)==0);pending.count=1;
    word(vendorData,24,7);pen.pressure=1;PenPressure::dispatch(&driver,&event,0);CHECK(*reinterpret_cast<uint32_t *>(penData+64)==0);word(vendorData,24,8);
    vendorData[30]=61;pen.pressure=1;PenPressure::dispatch(&driver,&event,0);CHECK(*reinterpret_cast<uint32_t *>(penData+64)==0);vendorData[30]=45;
    failPoseWrite=true;pen.pressure=1;PenPressure::dispatch(&driver,&event,0);
    CHECK(*reinterpret_cast<uint32_t *>(penData+64)==0&&*reinterpret_cast<int32_t *>(penData+68)==111);failPoseWrite=false;
    word(penData,0,72);pen.pressure=1;PenPressure::dispatch(&driver,&event,0);CHECK(*reinterpret_cast<uint32_t *>(penData+64)==0);
    std::free(allocated);std::printf("PASS: %u actual scoped pressure-hook checks, 220-slot bounds and forwarding; OS services modeled\n",checks);
}
