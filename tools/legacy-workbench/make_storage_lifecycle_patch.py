"""Generate an idempotent board patch from the preserved root-check source."""
from pathlib import Path
import difflib

root = Path(__file__).resolve().parents[1]
source = root / 'edl_work/abi-20261106/NativeUfs.rootcheck.reference.cpp'
original = source.read_text(encoding='utf-8')
modified = original.replace('#include "EldenUfsIdentity.h"',
    '#include "EldenUfsIdentity.h"\n#include <libkern/c++/OSSymbol.h>', 1)
needle = 'OSDefineMetaClassAndStructors (NativeUfsBlock, IOBlockStorageDevice)\n'
replacement = needle + '''

static_assert(sizeof(IOBlockStorageDevice) == 0x90, "original storage class ABI changed");
static_assert(sizeof(IOService) == 0x88, "source IOService ABI changed");

// Source constructors can hold the original metaclass address before its
// owning kext has initialized it. Check actual runtime registration before
// touching UFS or allocating a block-device instance.
extern "C" __attribute__((noinline)) bool elden_ufs_storage_class_ready(void)
{
  const OSSymbol *name = OSSymbol::withCStringNoCopy("IOBlockStorageDevice");
  if (!name) return false;
  const OSMetaClass *registered = OSMetaClass::getMetaClassWithName(name);
  name->release();
  const OSMetaClass *parent = NativeUfsBlock::metaClass->getSuperClass();
  return registered && registered == parent && parent->getClassSize() == 0x90 &&
    parent->getSuperClass() == IOService::metaClass && IOService::metaClass->getClassSize() == 0x88;
}
'''
assert modified.count(needle) == 1
modified = modified.replace(needle, replacement, 1)
needle = '  unsigned diagnostic = 0;\n'
replacement = '''#if defined(ELDEN_INHERITED_UFS_DMA) && ELDEN_INHERITED_UFS_DMA
  if ((rootMode || probe >= 10) && !elden_ufs_storage_class_ready())
    panic("NATIVE_UFS: original storage class is not initialized or registered");
#endif
''' + needle
assert modified.count(needle) == 1
modified = modified.replace(needle, replacement, 1)
patch = ''.join(difflib.unified_diff(original.splitlines(keepends=True),
    modified.splitlines(keepends=True), fromfile='a/NativeUfs.cpp', tofile='b/NativeUfs.cpp'))
destination = Path('D:/Desktop/Git/UniXNU_elden/scripts/driver-patches/sheng-ufs-elden-storage-lifecycle.patch')
destination.write_text(patch, encoding='utf-8', newline='\n')
print(destination)
