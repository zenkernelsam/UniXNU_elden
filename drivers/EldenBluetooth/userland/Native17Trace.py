# SPDX-License-Identifier: BSD-2-Clause-Patent
"""Preserve native XPC/errx behavior while emitting a bounded kdebug marker.

Direct syscall179 bypasses libc errno handling; original call results/arguments
and LR/SP are restored. Applies only to the exact Native17 input image.
"""
from Native17 import BASE, branch, mov

MARKER = 0xfe700018
MAGIC = 0x4254

def apply(image):
    if not image.routed:
        raise ValueError('Native17 routing required')
    xpc = image.emit('xpc_result_trace', [
        0xa9bf7bfd,  # stp x29,x30,[sp,#-16]!
        0xaa0003f3,  # mov x19,x0: original replaced instruction
        0xaa0003e2,  # arg2=result
        0xaa1403e3,  # arg3=service name (live callee-saved x20)
        mov(0, MARKER & 0xffff), 0x72a00000 | ((MARKER >> 16) << 5),
        mov(1, 1), mov(4, MAGIC),
        0xd2800000 | (179 << 5) | 16, 0xd4001001,  # syscall kdebug_trace64
        0xaa1303e0, 0xa8c17bfd, 0xd65f03c0])
    image.put(BASE + 0x568c5c, [branch(BASE + 0x568c5c, xpc, True)], [0xaa0003f3])
    err = image.emit('errx_trace', [
        0xa9be07e0,  # stp x0,x1,[sp,#-32]!
        0xf9000bfe,  # str x30,[sp,#16]
        0xaa0003e2, 0xaa0103e3,  # arg2=status, arg3=format string
        mov(0, MARKER & 0xffff), 0x72a00000 | ((MARKER >> 16) << 5),
        mov(1, 2), mov(4, MAGIC),
        0xd2800000 | (179 << 5) | 16, 0xd4001001,
        0xf9400bfe, 0xa8c207e0,
        lambda pc: branch(pc, BASE + 0x708fbc)])
    for site,word in ((BASE + 0x4bc45c,0x940932d8),(BASE + 0x669c08,0x94027ced)):
        image.put(site,[branch(site,err,True)],[word])
    return image
