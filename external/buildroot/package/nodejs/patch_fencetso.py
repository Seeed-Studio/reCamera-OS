#!/usr/bin/env python3
"""Rewrite fence.tso (0x3000000f) -> fence iorw,iorw (0x0ff0000f) inside
executable segments of a 64-bit ELF. fence iorw,iorw is strictly stronger
than fence.tso, so the substitution is always architecturally safe on cores
without Ztso."""
import struct, sys

FENCE_TSO = 0x8330000F
FENCE_FULL = 0x0FF0000F

def patch(path, out):
    d = bytearray(open(path, "rb").read())
    assert d[:4] == b"\x7fELF" and d[4] == 2, "not ELF64"
    e_phoff = struct.unpack_from("<Q", d, 0x20)[0]
    e_phentsize = struct.unpack_from("<H", d, 0x36)[0]
    e_phnum = struct.unpack_from("<H", d, 0x38)[0]
    total = 0
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, p_flags = struct.unpack_from("<II", d, off)
        if p_type != 1 or not (p_flags & 1):  # PT_LOAD & PF_X
            continue
        p_offset, p_vaddr, _, p_filesz = struct.unpack_from("<QQQQ", d, off + 8)
        for o in range(p_offset, p_offset + p_filesz - 3, 2):  # 2-byte aligned (RVC)
            if struct.unpack_from("<I", d, o)[0] == FENCE_TSO:
                struct.pack_into("<I", d, o, FENCE_FULL)
                total += 1
    open(out, "wb").write(d)
    print(f"{path}: patched {total} fence.tso -> {out}")

if __name__ == "__main__":
    patch(sys.argv[1], sys.argv[2])
