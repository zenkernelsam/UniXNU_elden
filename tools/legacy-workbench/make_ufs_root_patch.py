"""Generate board root-check changes against the already SM8750-patched source."""
import argparse
import difflib
from pathlib import Path

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a = p.parse_args()
    parts = []
    for name in ('NativeUfs.cpp','RootLayout.h','RootWindowRead.h','GccUfs.h'):
        old = (a.source/name).read_text()
        new = old
        if name == 'NativeUfs.cpp':
            marker = '  UfsSmmuPlan plan = planUfsSmmu '
            assert new.count(marker) == 1
            before = r'''#if defined(ELDEN_INHERITED_UFS_DMA) && ELDEN_INHERITED_UFS_DMA
  auto inherited = eldenUfsIdentity(stream, dma, bytes,
    [smmu](unsigned offset) { return read32(smmu, offset); });
  gRootDiagnostic.smmuId1 = inherited.id1; gRootDiagnostic.smmuId2 = inherited.id2;
  gRootDiagnostic.smmuSlot = inherited.slot;
  gRootDiagnostic.smr = inherited.smr; gRootDiagnostic.s2cr = inherited.s2cr;
  gRootDiagnostic.cbar = inherited.cbar; gRootDiagnostic.cba2r = inherited.cba2r;
  gRootDiagnostic.sctlr = inherited.sctlr;
  if (inherited.error) panic("NATIVE_UFS inherited DMA refused slot=%u smr=%x s2cr=%x cbar=%x sctlr=%x",
    inherited.slot, inherited.smr, inherited.s2cr, inherited.cbar, inherited.sctlr);
  report[0] = inherited.slot | (inherited.bank << 16);
  report[1] = inherited.cbar; report[2] = inherited.sctlr;
  IOLog("NativeUfs: inherited SID60 identity slot=%u CB=%u; no SMMU writes\n", inherited.slot, inherited.bank);
  return;
#endif
'''
            new = new.replace(marker,before+marker,1)
            new = new.replace('#include <IOKit/IOService.h>', '#include <IOKit/IOService.h>\n#include "EldenUfsIdentity.h"\nstatic unsigned gRootLun = 0;',1)
            needle = 'static uint64_t gRootLunLast = UINT32_MAX;'
            assert needle in new
            new = new.replace('gRootLunLast = gLuns[0].last;', 'if (gLuns[gRootLun].blockBytes != 4096) { *why = "selected LUN capacity unavailable"; return false; }\n      gRootLunLast = gLuns[gRootLun].last;')
            new = new.replace('hostScsi (0, tur,', 'hostScsi (gRootLun, tur,')
            new = new.replace('LUN 0 not ready','selected LUN not ready').replace('LUN0 last=','selected LUN last=')
            needle = '  unsigned probe = 0, rootMode = 0;'
            assert needle in new
            new = new.replace(needle,needle+'\n  PE_parse_boot_argn("native-ufs-lun", &gRootLun, sizeof(gRootLun));\n  if (gRootLun != 0 && gRootLun != 4) panic("NATIVE_UFS: elden root LUN must be 0 or 4");')
            needle = 'IOReturn NativeUfsBlock::doAsyncReadWrite '
            assert new.count(needle) == 1
            fallback = r'''#if !defined(NATIVE_VERIFIED_BLOCK) || !NATIVE_VERIFIED_BLOCK
volatile uint64_t native_ufs_read_ticks = 0;
static inline uint64_t timerTicks(void) {
  uint64_t ticks;
  __asm__ volatile("isb\n\tmrs %0, cntpct_el0" : "=r" (ticks));
  return ticks;
}
#endif
'''
            new = new.replace(needle,fallback+needle,1)
        if name in ('NativeUfs.cpp','RootLayout.h','RootWindowRead.h'):
            new = new.replace('hostRead4k (0,','hostRead4k (gRootLun,')
        if name == 'GccUfs.h':
            lines = []
            for line in new.splitlines(True):
                if line.startswith('#define UFS_ROOT_PARTITION'):
                    lines.extend(['#ifndef UFS_ROOT_PARTITION\n',line,'#endif\n'])
                else:
                    lines.append(line)
            new = ''.join(lines)
        parts.extend(difflib.unified_diff(old.splitlines(True),new.splitlines(True),fromfile='a/'+name,tofile='b/'+name))
    a.output.write_text(''.join(parts))
    print(a.output)

if __name__ == '__main__':
    main()
