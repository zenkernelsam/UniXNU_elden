#!/usr/bin/env python3
"""Apply exact owned edits after the existing pinned UFS patch stack."""
from pathlib import Path
import argparse,hashlib,json
def once(text,old,new,label):
    if new in text:return text
    if text.count(old)!=1:raise SystemExit('Review changed UFS/RamBlock source: '+label)
    return text.replace(old,new,1)
def patch_ufs(folder):
    p=folder/'NativeUfs.cpp';text=p.read_text();before=hashlib.sha256(p.read_bytes()).hexdigest()
    text=once(text,'#include "RootCommands.h"',
        '#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE\n#include "EldenDataCommandGate.inc"\n#endif\n#include "RootCommands.h"','owner declarations')
    text=once(text,'#include "EldenSdamRecovery.inc"',
        '#include "EldenSdamRecovery.inc"\n#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE\n#include "EldenDataInspect.inc"\n#endif','read-only inspector hook')
    anchor='extern "C" unsigned native_ufs_store_mode (void) { return storeMode (); }'
    text=once(text,anchor,'#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE\n#include "EldenDataStore.inc"\n#endif\n'+anchor,'fixed store adapter')
    for name,call in [('native_ufs_store_open','eldenDataStoreOpen(blocks,firstLba,why)'),
                      ('native_ufs_store_read','eldenDataStoreRead(rel,blocks,dst)'),
                      ('native_ufs_store_write','eldenDataStoreWrite(rel,blocks,src)'),
                      ('native_ufs_store_sync','eldenDataStoreSync()')]:
        start=text.index('extern "C" int '+name+' (');at=text.index('\n{',start)+2
        body='\n#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE\n  return '+call+';\n#endif'
        if not text[at:].startswith(body):text=text[:at]+body+text[at:]
    p.write_text(text,newline='\n')
    p=folder/'RootCommands.h';text=p.read_text()
    old='''  if (direction == 1 || (cdb[0] != 0 && cdb[0] != 0x12 && cdb[0] != 0x25 && cdb[0] != 0x28 &&
      !(cdbLength == 6 && cdb[0] == 0x1b && !cdb[1] && !cdb[2] && !cdb[3] && cdb[4] == 0x10 && !cdb[5])))
    return kIOReturnBadArgument;'''
    new='''#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE
  const bool scopedData=eldenDataCommandAllowed(lun,cdb,cdbLength,direction,dataBytes);
#else
  const bool scopedData=false;
#endif
  if (!scopedData && (direction == 1 || (cdb[0] != 0 && cdb[0] != 0x12 && cdb[0] != 0x25 && cdb[0] != 0x28 &&
      !(cdbLength == 6 && cdb[0] == 0x1b && !cdb[1] && !cdb[2] && !cdb[3] && cdb[4] == 0x10 && !cdb[5]))))
    return kIOReturnBadArgument;'''
    text=once(text,old,new,'read-only command filter')
    old='''static bool hostInWindow (uint64_t lba, uint32_t blocks)
{'''
    new=old+'''\n#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE
  return eldenDataWriteRange(lba,blocks);
#endif'''
    text=once(text,old,new,'every WRITE range')
    start=text.index('static bool hostStoreOpen (');end=text.index('static IOReturn hostWrite4k (',start)
    old='''#if defined(NATIVE_UFS_READ_ONLY) && NATIVE_UFS_READ_ONLY
  return false;
#endif'''
    new='''#if defined(NATIVE_UFS_READ_ONLY) && NATIVE_UFS_READ_ONLY && !(defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE)
  return false;
#endif
#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE
  if (!gEldenDataLayout.valid || gEldenDataCommand.fence.mode!=2 ||
      gEldenDataCommand.fence.closed || gEldenDataCommand.fence.poisoned ||
      first!=gEldenDataCommand.fence.window.first || blocks!=gEldenDataCommand.fence.window.blocks ||
      !elden_data_contains(gEldenDataLayout.data,first,blocks))return false;
#endif'''
    body=once(text[start:end],old,new,'only verified Data pool can open writes')
    text=text[:start]+body+text[end:]
    old='''  if (!gHost.ready || (!gStoreOpen && !gLogOpen)) return kIOReturnNotReady;'''
    new='''#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE
  if (!gHost.ready || !eldenDataSyncScope()) return kIOReturnNotReady;
#else
  if (!gHost.ready || (!gStoreOpen && !gLogOpen)) return kIOReturnNotReady;
#endif'''
    text=once(text,old,new,'cache-sync reservation')
    start=text.index('static IOReturn hostWrite4k (');end=text.index('static IOReturn hostSyncCache (',start)
    if 'IOLockLock (gHost.lock);' not in text[start:end] or 'hostLock (' in text[start:end]:
        raise SystemExit('Review changed serialized WRITE owner')
    p.write_text(text,newline='\n')
    return {'native_before_sha256':before,'native_after_sha256':hashlib.sha256((folder/'NativeUfs.cpp').read_bytes()).hexdigest(),
            'commands_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'original_read_only_define_retained':True}
def patch_overlay(folder):
    p=folder/'PersistOverlay.h';text=p.read_text()
    anchor='''    if (header == 0 && discard && !ro) header = 3;'''
    old=anchor+'   // the caller asked to start over: handled like a stale overlay'
    new=old+'''
#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE
    // A kernel/seed update must never turn an existing Data store into a fresh
    // one. Only a wholly zero blank header plus explicit reviewed initialization
    // can be initialized; damaged, foreign and stale headers are preserved.
    if (header != 0) {
      unsigned nonzero=0;for(unsigned i=0;i<kBlock;i++)nonzero|=block[i];
      if(header!=1 || nonzero)return Open::Failed;
      if(!ro) {
        extern "C" int native_ufs_store_initialize_allowed(void) __attribute__((weak_import));
        if(!native_ufs_store_initialize_allowed || !native_ufs_store_initialize_allowed())return Open::Failed;
      }
    }
#endif'''
    # C-linkage declarations have namespace scope, not function scope.
    new=new.replace('        extern "C" int native_ufs_store_initialize_allowed(void) __attribute__((weak_import));\n','')
    text=once(text,old,new,'preserve foreign/stale Data')
    anchor='namespace persist {'
    new='''#if defined(ELDEN_DATA_STORE) && ELDEN_DATA_STORE
extern "C" int native_ufs_store_initialize_allowed(void) __attribute__((weak));
#endif
'''+anchor
    text=once(text,anchor,new,'reviewed initialization provider')
    p.write_text(text,newline='\n')
    return {'overlay_sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'stale_seed_is_refused':True}
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--ufs',type=Path);p.add_argument('--ramblock',type=Path)
    a=p.parse_args();out={}
    if a.ufs:out.update(patch_ufs(a.ufs))
    if a.ramblock:out.update(patch_overlay(a.ramblock))
    print(json.dumps(out,indent=2))
if __name__=='__main__':main()
