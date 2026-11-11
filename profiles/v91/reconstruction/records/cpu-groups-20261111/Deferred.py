"""SHA-bound, opt-in delayed secondary startup for the Elden ArmCpu input.

The board must supply elden_smp_recovery_ready before release is possible.
This adapter does not manufacture watchdog readiness or program any registers.
"""
import hashlib
from pathlib import Path

BASE_SHA256 = '815868520794ce7d7230adac373928a107a2bae6d427cc4513daa533919896dc'

DECLARATIONS = '''
#if defined(ELDEN_ARMCPU_DEFERRED) && ELDEN_ARMCPU_DEFERRED
// Supplied by the board recovery controller, not by a boot argument. Absent
// evidence leaves all secondaries queued. The hook must be nonblocking and
// verify the current recovery lease, including an independent reset fallback.
extern "C" bool elden_smp_recovery_ready(void) __attribute__((weak_import));
static volatile uint32_t gEldenSmpReleased = 0;
static class ArmCpu *volatile gEldenRegistered[ARMCPU_SMP_SLOTS];
#endif
'''

DRAIN_GUARD = '''
#if defined(ELDEN_ARMCPU_DEFERRED) && ELDEN_ARMCPU_DEFERRED
  if (__atomic_load_n(&gBootRegistered, __ATOMIC_ACQUIRE) == 0 ||
      __atomic_load_n(&gEldenSmpReleased, __ATOMIC_ACQUIRE) == 0 ||
      !elden_smp_recovery_ready || !elden_smp_recovery_ready()) { return; }
#endif
'''

PER_CPU_GUARD = '''
#if defined(ELDEN_ARMCPU_DEFERRED) && ELDEN_ARMCPU_DEFERRED
    // Recheck between registrations. A lost lease never consumes pending work.
    if (!elden_smp_recovery_ready()) { return; }
#endif
'''

RELEASE = '''
// Return values: 0 release accepted, -1 not opted in/boot not ready,
// -2 recovery evidence unavailable, -3 this build has no deferred path.
int ArmCpu::releaseSecondaries(void)
{
#if defined(ELDEN_ARMCPU_DEFERRED) && ELDEN_ARMCPU_DEFERRED
  if (__atomic_load_n(&gBootRegistered, __ATOMIC_ACQUIRE) == 0 || !gSmpWanted) { return -1; }
  if (!elden_smp_recovery_ready || !elden_smp_recovery_ready()) { return -2; }
  __atomic_store_n(&gEldenSmpReleased, 1U, __ATOMIC_RELEASE);
  drainPending();
  return 0;
#else
  return -3;
#endif
}

extern "C" int elden_armcpu_release_secondaries(void)
{
  return ArmCpu::releaseSecondaries();
}
'''

DIAGNOSTICS = '''
extern "C" {
volatile uint32_t elden_gic_groups_before=0,elden_gic_groups_after=0,elden_gic_typer2=0;
}
int ArmCpu::guardGroups(void)
{
#if defined(ELDEN_ARMCPU_DEFERRED) && ELDEN_ARMCPU_DEFERRED
  if(!gDist||__atomic_load_n(&gEldenSmpReleased,__ATOMIC_ACQUIRE))return -1;
  const int result=elden_gic_groups::enable(
    [](){return read32(gDist,0);},[](uint32_t value){write32(gDist,0,value);},
    [](){return waitClear(gDist,0,1U<<31);},
    [](){return elden_smp_recovery_ready&&elden_smp_recovery_ready();},
    &elden_gic_groups_before,&elden_gic_groups_after);
  if(!result)elden_gic_typer2=read32(gDist,0x0c);
  return result;
#else
  return -1;
#endif
}
extern "C" int elden_armcpu_guard_groups(void){return ArmCpu::guardGroups();}
// Use the registered native processor and the existing signalCPU implementation.
// No new interrupt number, redistributor layout or clock policy is introduced.
processor_t ArmCpu::guardProcessor(unsigned id)
{
#if defined(ELDEN_ARMCPU_DEFERRED) && ELDEN_ARMCPU_DEFERRED
  if(id>=ARMCPU_SMP_SLOTS)return nullptr;
  ArmCpu *cpu=__atomic_load_n(&gEldenRegistered[id],__ATOMIC_ACQUIRE);
  if(!cpu||(id!=0&&armcpu_smp_step[id]!=7))return nullptr;
  return cpu->machProcessor;
#else
  (void)id;return nullptr;
#endif
}
int ArmCpu::guardIpi(unsigned id)
{
#if defined(ELDEN_ARMCPU_DEFERRED) && ELDEN_ARMCPU_DEFERRED
  uint64_t mpidr;__asm__ volatile("mrs %0,mpidr_el1":"=r"(mpidr));
  int self=ml_get_cpu_number(static_cast<uint32_t>(mpidr));
  if(self<0||self>=ARMCPU_SMP_SLOTS||id>=ARMCPU_SMP_SLOTS)return -1;
  ArmCpu *source=__atomic_load_n(&gEldenRegistered[self],__ATOMIC_ACQUIRE);
  ArmCpu *target=__atomic_load_n(&gEldenRegistered[id],__ATOMIC_ACQUIRE);
  if(!source||!target||!guardProcessor(id))return -2;
  source->signalCPU(target);return 0;
#else
  (void)id;return -3;
#endif
}
extern "C" processor_t elden_armcpu_guard_processor(unsigned id){return ArmCpu::guardProcessor(id);}
extern "C" int elden_armcpu_guard_ipi(unsigned id){return ArmCpu::guardIpi(id);}
'''

def apply(source: bytes) -> bytes:
    if hashlib.sha256(source).hexdigest() != BASE_SHA256:
        raise ValueError('ArmCpu input differs from the audited CPU8 source')
    text = source.decode('utf-8')
    replacements = [
        ('static volatile uint32_t gBootRegistered = 0;', 'static volatile uint32_t gBootRegistered = 0;' + DECLARATIONS),
        ('public:\n  IOService *probe', 'public:\n  static int releaseSecondaries(void);\n  static processor_t guardProcessor(unsigned);\n  static int guardIpi(unsigned);\n  static int guardGroups(void);\n  IOService *probe'),
        ('void ArmCpu::drainPending (void)\n{', 'void ArmCpu::drainPending (void)\n{' + DRAIN_GUARD),
        ('  for (unsigned id = 1; id < ARMCPU_SMP_SLOTS; id++) {\n    ArmCpu *cpu',
         '  for (unsigned id = 1; id < ARMCPU_SMP_SLOTS; id++) {' + PER_CPU_GUARD + '    ArmCpu *cpu'),
        ('bool ArmCpu::finishSecondary (void)', RELEASE + '\nbool ArmCpu::finishSecondary (void)'),
        ('if (gBootRegistered != 0) { drainPending (); }',
         'if (__atomic_load_n(&gBootRegistered, __ATOMIC_ACQUIRE) != 0) { drainPending (); }'),
        ('  armcpu_registered = 1;', '  armcpu_registered = 1;\n#if defined(ELDEN_ARMCPU_DEFERRED) && ELDEN_ARMCPU_DEFERRED\n  __atomic_store_n(&gEldenRegistered[logicalId],this,__ATOMIC_RELEASE);\n#endif'),
        ('  armcpu_smp_step[logicalId] = 2;', '  armcpu_smp_step[logicalId] = 2;\n#if defined(ELDEN_ARMCPU_DEFERRED) && ELDEN_ARMCPU_DEFERRED\n  __atomic_store_n(&gEldenRegistered[logicalId],this,__ATOMIC_RELEASE);\n#endif'),
        ('// PSCI conduit comes from', (Path(__file__).parent/'GicGroups.h').read_text().replace('#pragma once\n','')+'\n'+DIAGNOSTICS+'\n// PSCI conduit comes from'),
    ]
    for before, after in replacements:
        if text.count(before) != 1:
            raise ValueError('Expected one ArmCpu integration anchor: ' + before[:70])
        text = text.replace(before, after)
    return text.encode('utf-8')
