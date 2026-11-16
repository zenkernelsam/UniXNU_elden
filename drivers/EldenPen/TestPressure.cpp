/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Execute the actual hook with explicit IOHID/registry/memory peers. */
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cstddef>
#include "HidReport.h"
using IOOptionBits=unsigned;
static unsigned checks,forwards;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"line%d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
struct OSArray;
struct OSObject {
    uintptr_t *table;uint64_t unused[2];OSArray *children=nullptr;
    int kind=1;int32_t pressure=0,transducer=0;unsigned type=11;
    bool metaCast(const char *){return kind==1;}
};
static_assert(offsetof(OSObject,children)==24,"Exact target event child offset modeled");
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
#include "PressureHook.inc"
static uint32_t getType(void *p){return static_cast<OSObject *>(p)->type;}
static int32_t getInteger(void *p,uint32_t field,IOOptionBits){CHECK(field==0xb0004);return static_cast<OSObject *>(p)->transducer;}
static int32_t getFixed(void *p,uint32_t field,IOOptionBits){CHECK(field==0xb000a);return static_cast<OSObject *>(p)->pressure;}
static void setFixed(void *p,uint32_t field,int32_t value,IOOptionBits){CHECK(field==0xb000a);static_cast<OSObject *>(p)->pressure=value;}
int main(){
    driverTable[198]=(uintptr_t)&original;
    eventTable[18]=(uintptr_t)&getType;eventTable[23]=(uintptr_t)&getInteger;eventTable[25]=(uintptr_t)&getFixed;eventTable[26]=(uintptr_t)&setFixed;
    IOService driver,foreign;driver.table=driverTable;foreign.table=driverTable;
    CHECK(!PenPressure::install(nullptr));allocationFail=true;
    CHECK(!PenPressure::install(&driver)&&driver.references==0);allocationFail=false;
    CHECK(PenPressure::install(&driver)&&driver.references==1);
    CHECK(PenPressure::install(&driver)&&driver.references==1&&!PenPressure::install(&foreign));
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
    std::free(allocated);std::printf("PASS: %u actual scoped pressure-hook checks, 220-slot bounds and forwarding; OS services modeled\n",checks);
}
