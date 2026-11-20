"""Extend the exact single backlight/SE2 owner with bounded camera PMIC access."""
from pathlib import Path
import hashlib,json,sys
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
PINS={
 'drivers/EldenBacklight/BacklightRuntime.cpp':'9418f7f7e3016e4d398a6d7d6b0f3053aa76b75309c61b65d827f617873cdcfa',
 'drivers/EldenBacklight/Platform.c':'6d5858ab192285fb8ecb49f18eb0d1968bf37880653098a499cd179f6c030f33',
 'drivers/EldenBacklight/Platform.h':'0ce5f80d24795b579b5f44198a4011d1c56464b59bc9585695959f55fea87f2b'}
def derive(variant='original'):
    data={};deltas=[]
    for n,d in PINS.items():
        b=(ROOT/n).read_bytes()
        if hashlib.sha256(b).hexdigest()!=d:raise ValueError('Qualified original backlight source changed: '+n)
        data[Path(n).name]=b.decode().replace('\r\n','\n')
    if variant=='pd-power':
        sys.path.insert(0,str(ROOT/'drivers/EldenBacklight'))
        from PdPowerSource import source
        body=source()
        if hashlib.sha256(body).hexdigest()!='c13b9554a10c986356d634d8c9f3a1d5ddaf8b03d20edbb91ef34fedd101d9a5':
            raise ValueError('Qualified original boot/PD backlight derivation changed')
        data['BacklightRuntime.cpp']=body.decode().replace('\r\n','\n')
    elif variant!='original':raise ValueError('Unknown backlight variant')
    def patch(name,before,after,why):
        if data[name].count(before)!=1:raise ValueError('Original shared-bus anchor changed: '+why)
        data[name]=data[name].replace(before,after);deltas.append({'file':name,'reason':why})
    patch('Platform.c','static int transfer(EldenBlPlatform *p,uint8_t reg,uint8_t *value,bool writing)',
          'static int transfer(EldenBlPlatform *p,uint8_t address,uint8_t reg,uint8_t *value,bool writing)','one original transfer body retains legacy address scope')
    patch('Platform.c','''    if(writing ? (reg!=4&&reg!=5&&reg!=0x0d) :
        (reg!=0&&reg!=4&&reg!=5&&reg!=7&&reg!=0x0d&&reg!=0x10&&reg!=0x11&&reg!=0x12&&reg!=0x13))return EldenBlBadArgument;''',
          '''    if(address==ELDEN_BL_ADDRESS){
        if(writing ? (reg!=4&&reg!=5&&reg!=0x0d) :
            (reg!=0&&reg!=4&&reg!=5&&reg!=7&&reg!=0x0d&&reg!=0x10&&reg!=0x11&&reg!=0x12&&reg!=0x13))return EldenBlBadArgument;
    }else if(address!=0x2f||!(reg==0x0e||(reg>=3&&reg<=9)||(!writing&&reg==0)))return EldenBlBadArgument;''',
          'WL2868C ID/voltage/enable only; original AW99706 whitelist unchanged')
    patch('Platform.c','    int rc=writing?geni_i2c_start_write(&p->bus,reg,*value):geni_i2c_start_read(&p->bus,reg);',
          '    p->bus.address=address;\n    int rc=writing?geni_i2c_start_write(&p->bus,reg,*value):geni_i2c_start_read(&p->bus,reg);',
          'actual original GENI engine queues selected chip address after idle proof')
    patch('Platform.c','    ++p->transfers;if(!writing)*value=p->bus.data;return 0;',
          '    p->bus.address=ELDEN_BL_ADDRESS;\n    ++p->transfers;if(!writing)*value=p->bus.data;return 0;',
          'restore legacy chip address only after real transfer/idle success')
    patch('Platform.c','return transfer(p,reg,value,false);','return transfer(p,ELDEN_BL_ADDRESS,reg,value,false);','original read wire unchanged')
    patch('Platform.c','return transfer(p,reg,&value,true);','return transfer(p,ELDEN_BL_ADDRESS,reg,&value,true);','original write wire unchanged')
    data['Platform.c']+='\nint elden_bl_platform_i2c10_read(void *p,uint8_t reg,uint8_t *value){return transfer(p,0x2f,reg,value,false);}\nint elden_bl_platform_i2c10_write(void *p,uint8_t reg,uint8_t value){return transfer(p,0x2f,reg,&value,true);}\n'
    patch('Platform.h','int elden_bl_platform_write(void *, uint8_t reg, uint8_t value);',
          'int elden_bl_platform_write(void *, uint8_t reg, uint8_t value);\nint elden_bl_platform_i2c10_read(void *,uint8_t,uint8_t *);\nint elden_bl_platform_i2c10_write(void *,uint8_t,uint8_t);',
          'internal bounded camera transfers, not raw user register API')
    patch('BacklightRuntime.cpp','#include "EldenBacklightResources.h"','#include "EldenBacklightResources.h"\n#include "SharedI2c10.h"','existing runtime carries shared port declaration')
    patch('BacklightRuntime.cpp','unsigned elden_recovery_deadline_remaining(void) __attribute__((weak_import));',
          'unsigned elden_recovery_deadline_remaining(void) __attribute__((weak_import));\nboolean_t ml_at_interrupt_context(void);','no IRQ context camera bus borrowing')
    patch('BacklightRuntime.cpp','    Region region[4];EldenBlPlatform platform={};EldenAw99706 chip={};',
          '    Region region[4];EldenBlPlatform platform={};EldenAw99706 chip={};\n    IOLock *serial=nullptr;uint32_t shared_camera_refs=0;bool shared_closing=false,shared_published=false,shared_released=false;\n    ~Context(){if(serial)IOLockFree(serial);}',
          'same Context serializes camera/brightness transactions and protects retained borrows')
    patch('BacklightRuntime.cpp','static uint32_t read(void *p,EldenBlSpace s,uint32_t r)',
          '}\n#include "SharedI2c10Methods.inc"\nnamespace {\nstatic uint32_t read(void *p,EldenBlSpace s,uint32_t r)',
          'methods reuse actual original Context; no separate MMIO owner')
    patch('BacklightRuntime.cpp','    int rc=elden_bl_platform_release(&c->platform);','    int rc=shared_bus_release(c);','cannot release bus with active camera borrow')
    patch('BacklightRuntime.cpp','    if(!rc){delete c;__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);}',
          '    if(!rc){if(!c->shared_published)delete c;__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);}',
          'published Context/maps retained until reset, not freed under port/status readers')
    patch('BacklightRuntime.cpp','    c->platform.io={c,read,write,now_us,delay_us,gcc_lock};',
          '    c->serial=IOLockAlloc();if(!c->serial){delete c;__atomic_store_n(&lease,0u,__ATOMIC_RELEASE);return;}\n    c->platform.io={c,read,write,now_us,delay_us,gcc_lock};',
          'actual sleepable serial mutex before any external bus publication')
    patch('BacklightRuntime.cpp','    EldenBacklightIo io={&c->platform,elden_bl_platform_read,elden_bl_platform_write};',
          '    if(!rc)shared_bus_publish(c);\n    EldenBacklightIo io={c,shared_backlight_read,shared_backlight_write};',
          'all original brightness/health operations use the same bus mutex')
    for reg in ('0x12','0x13','0x10','0x0d','4','5'):
        patch('BacklightRuntime.cpp','elden_bl_platform_read(&c->platform,'+reg+',',
              'shared_backlight_read(c,'+reg+',','original diagnostic read also uses single bus mutex: '+reg)
    return data,deltas
