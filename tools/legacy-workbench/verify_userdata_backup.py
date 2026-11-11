"""Verify packed ciphertext and construct a standard sparse raw recovery image.

No device access. Unallocated filesystem blocks are zero holes; retained blocks
are byte-exact. NTFS sparse allocation is enabled before extending logical size.
"""
from pathlib import Path
import hashlib,json,os,ctypes,msvcrt,shutil,time

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'edl_work/prepartition-128g-20261106'

def main():
    m=json.loads((OUT/'userdata-cipher-backup.json').read_text())
    assert m['independent_device_read_match'] and m['source_unmounted']
    archive=OUT/'userdata-allocated-cipher.bin';target=OUT/'userdata-recovery.sparse.img'
    assert archive.stat().st_size==m['packed_bytes'] and not target.exists()
    assert shutil.disk_usage(OUT).free>m['packed_bytes']+(1<<30)
    kernel32=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel32.DeviceIoControl.argtypes=[ctypes.c_void_p,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.c_void_p,ctypes.c_ulong,ctypes.POINTER(ctypes.c_ulong),ctypes.c_void_p]
    kernel32.DeviceIoControl.restype=ctypes.c_int
    full=hashlib.sha256();packed=0;previous_end=0;started=time.monotonic()
    with archive.open('rb') as src,target.open('x+b') as dst:
        returned=ctypes.c_ulong()
        if not kernel32.DeviceIoControl(msvcrt.get_osfhandle(dst.fileno()),0x900c4,None,0,None,0,ctypes.byref(returned),None):
            raise OSError(ctypes.get_last_error(),'NTFS sparse marking failed; file not extended')
        # The Windows CRT truncate routine can physically zero-fill extension.
        # Native SetEndOfFile preserves the NTFS sparse holes instead.
        kernel32.SetFilePointerEx.argtypes=[ctypes.c_void_p,ctypes.c_longlong,ctypes.c_void_p,ctypes.c_ulong]
        kernel32.SetFilePointerEx.restype=ctypes.c_int
        kernel32.SetEndOfFile.argtypes=[ctypes.c_void_p]
        kernel32.SetEndOfFile.restype=ctypes.c_int
        handle=msvcrt.get_osfhandle(dst.fileno())
        if not kernel32.SetFilePointerEx(handle,m['original_logical_bytes'],None,0) or not kernel32.SetEndOfFile(handle):
            raise OSError(ctypes.get_last_error(),'Sparse logical sizing failed')
        if not kernel32.SetFilePointerEx(handle,0,None,0):
            raise OSError(ctypes.get_last_error(),'Sparse file rewind failed')
        for e in m['extents']:
            assert e['packed_offset']==packed and e['offset']>=previous_end and e['bytes']>0
            assert e['offset']+e['bytes']<=m['original_logical_bytes']
            dst.seek(e['offset']);left=e['bytes'];h=hashlib.sha256()
            while left:
                data=src.read(min(left,1<<20));assert data
                full.update(data);h.update(data);dst.write(data);left-=len(data);packed+=len(data)
            assert h.hexdigest()==e['sha256'],'per-extent packed checksum mismatch'
            previous_end=e['offset']+e['bytes']
        assert not src.read(1) and packed==m['packed_bytes'] and full.hexdigest()==m['sha256']
        dst.flush();os.fsync(dst.fileno())
    print('Packed backup verified; rereading the sparse recovery image at original offsets.',flush=True)
    restored=hashlib.sha256()
    with target.open('rb') as src:
        for e in m['extents']:
            src.seek(e['offset']);left=e['bytes'];h=hashlib.sha256()
            while left:
                data=src.read(min(left,1<<20));assert data
                restored.update(data);h.update(data);left-=len(data)
            assert h.hexdigest()==e['sha256'],'sparse recovery extent mismatch'
    assert restored.hexdigest()==m['sha256']
    kernel32.GetCompressedFileSizeW.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_ulong)]
    kernel32.GetCompressedFileSizeW.restype=ctypes.c_ulong
    high=ctypes.c_ulong();low=kernel32.GetCompressedFileSizeW(str(target),ctypes.byref(high));allocated=(high.value<<32)|low
    result={'packed_all_extents_verified':True,'sparse_recovery_all_extents_verified':True,
            'retained_data_sha256':restored.hexdigest(),'logical_bytes':target.stat().st_size,
            'ntfs_allocated_bytes':allocated,'sparse_file':str(target),'seconds':time.monotonic()-started,
            'limitations':'Free filesystem blocks are zero holes, not forensic preservation. Hardware decryption is not attempted on PC.'}
    (OUT/'backup-verification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2),flush=True)

if __name__=='__main__':main()
