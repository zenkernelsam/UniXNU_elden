#!/usr/bin/env python3
"""Convert a raw image to Android sparse format (fastboot-friendly), pure Python.

Usage: raw2simg.py IN.raw OUT.simg [block_bytes]
Chunks are capped at CHUNK_BLOCKS*block_bytes of data (1 MiB by default) so
each fastboot download stays small.
"""
import os
import struct
import sys

MAGIC = 0xED26FF3A
TYPE_RAW = 0xCAC1
TYPE_FILL = 0xCAC2
CHUNK_BLOCKS = 256  # 1 MiB with 4 KiB blocks


def main():
    src, dst = sys.argv[1], sys.argv[2]
    blk = int(sys.argv[3]) if len(sys.argv) > 3 else 4096
    size = os.path.getsize(src)
    if size % blk:
        sys.exit(f"size {size} not a multiple of {blk}")
    total_blks = size // blk
    zero = bytes(blk)

    with open(src, 'rb') as f, open(dst, 'wb') as o:
        # placeholder header; total_chunks patched at the end
        o.write(struct.pack('<IHHHHIIII', MAGIC, 1, 0, 28, 12, blk,
                            total_blks, 0, 0))
        chunks = 0
        buf = bytearray()
        while True:
            blocks = f.read(blk * CHUNK_BLOCKS)
            if not blocks:
                break
            if len(blocks) % blk:
                sys.exit("short block read")
            # split this batch into runs of all-zero / non-zero blocks
            n = len(blocks) // blk
            i = 0
            while i < n:
                b = blocks[i * blk:(i + 1) * blk]
                is_zero = (b == zero)
                j = i + 1
                while j < n and ((blocks[j * blk:(j + 1) * blk] == zero) == is_zero):
                    j += 1
                if is_zero:
                    # FILL chunk: chunk_sz = expanded bytes (blocks*blk), total_sz = 12+4
                    buf += struct.pack('<HHII', TYPE_FILL, 0, (j - i) * blk, 16)
                    buf += struct.pack('<I', 0)
                else:
                    data = blocks[i * blk:j * blk]
                    buf += struct.pack('<HHII', TYPE_RAW, 0, len(data), len(data) + 12)
                    buf += data
                chunks += 1
                i = j
            if len(buf) >= 4 << 20:
                o.write(buf)
                buf.clear()
        o.write(buf)
        o.seek(20)
        o.write(struct.pack('<I', chunks))
    print(f"wrote {dst}: {size} bytes, {total_blks} blocks, {chunks} chunks")


if __name__ == '__main__':
    main()
