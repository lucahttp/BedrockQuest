#!/usr/bin/env python3
"""
symbolless_string_xrefs.py v3 — Combined-substring scanner.

For each interesting substring, find ALL occurrences in .rodata and
generate ADRP+ADD/LDR xrefs using the precomputed index. This catches
the real engine hooks that hide behind qualified names like
'MinecraftGame::setSize' or 'setRenderingSizeEiiE3$_0'.
"""

import argparse
import json
import struct
import time
from pathlib import Path

import lief
import capstone


INTERESTING_SUBSTRINGS = {
    # Engine lifecycle
    b"MinecraftGame::init && MinecraftGame::setSize!": "MinecraftGame::setSize",
    b"MinecraftGame::setSize!":                            "MinecraftGame::setSize (single)",
    b"MinecraftGame::startFrame()":                        "MinecraftGame::startFrame",
    b"MinecraftGame::endFrame()":                          "MinecraftGame::endFrame",
    b"MinecraftGame::onTick()":                            "MinecraftGame::onTick",
    b"MinecraftGame::update()":                            "MinecraftGame::update",
    b"MinecraftGame::_update()":                           "MinecraftGame::_update",
    b"AppPlatform::initialize()":                          "AppPlatform::initialize",
    # RenderDragon entry points
    b"RenderDragonInternal":                               "RenderDragonInternal",
    b"RenderDragonRenderingBackendE":                      "RenderDragonRenderingBackend",
    # setRenderingSize lambda — typeinfo name → real function exists
    b"setRenderingSizeEiiE3$_0":                           "App::setRenderingSize lambda",
    b"setRenderingSizeEiiE3$_0NS_9allocatorIS3_EEFvvEEE":   "App::setRenderingSize std::function",
    # Native surface callbacks
    b"onSurfaceChangedNative":                             "onSurfaceChangedNative (JNI callback)",
    b"onSurfaceCreatedNative":                             "onSurfaceCreatedNative (JNI callback)",
    # GameControllerManager
    b"GameControllerManager":                              "GameControllerManager",
    # Render target lifecycle
    b"RenderDragon/src/Platform/BgfxContext.cpp":          "RenderDragon BgfxContext",
}


def build_adrp_index(text_vaddr, text_bytes):
    idx = {}
    for off in range(0, len(text_bytes) - 4, 4):
        w = struct.unpack_from("<I", text_bytes, off)[0]
        if (w & 0x9F000000) != 0x90000000:
            continue
        immlo = (w >> 29) & 0b11
        immhi = (w >> 5) & 0x7FFFF
        imm = (immhi << 2) | immlo
        if imm & (1 << 20):
            imm -= (1 << 21)
        pc = text_vaddr + off
        adrp_target = ((pc & ~0xFFF) + (imm << 12)) & 0xFFFFFFFFFFFFFFFF
        page = adrp_target & ~0xFFF
        rn = w & 0x1F
        idx.setdefault(page, []).append((pc, rn, off))
    return idx


def find_xrefs(adrp_index, text_vaddr, text_bytes, str_vaddr, md):
    page = str_vaddr & ~0xFFF
    page_offset = str_vaddr & 0xFFF
    candidates = adrp_index.get(page, [])
    hits = []
    for pc, rn, off in candidates:
        for j in range(1, 5):
            ioff = off + j * 4
            if ioff + 4 > len(text_bytes):
                break
            ins = next(md.disasm(text_bytes[ioff: ioff + 4],
                                  text_vaddr + ioff), None)
            if ins is None:
                continue
            if ins.mnemonic == "add":
                ops = ins.op_str.lower().split(",")
                if len(ops) >= 3:
                    dst = ops[0].strip()
                    src = ops[1].strip()
                    imm_str = ops[2].strip()
                    if (dst == f"x{rn}" and src == f"x{rn}"
                            and imm_str.startswith("#")
                            and imm_str[1:].startswith("0x")):
                        try:
                            imm_val = int(imm_str[1:], 16)
                            if imm_val == page_offset:
                                hits.append((pc, ins.address,
                                             f"adrp X{rn},0x{page:x}; "
                                             f"add X{rn},X{rn},#0x{page_offset:x}"))
                                break
                        except ValueError:
                            pass
            elif ins.mnemonic.startswith("ldr") and f"x{rn}" in ins.op_str:
                if f"[x{rn}" in ins.op_str and f"#0x{page_offset:x}" in ins.op_str:
                    hits.append((pc, ins.address,
                                 f"adrp X{rn},0x{page:x}; "
                                 f"{ins.mnemonic} {ins.op_str}"))
                    break
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", type=Path)
    ap.add_argument("--out", type=Path,
                    default=Path("/Volumes/X10Pro/Projects/mcbedvr/tools/deepdive/"))
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    binary = lief.parse(str(args.elf))
    sections = {s.name: s for s in binary.sections}
    text = sections[".text"]
    rodata = sections[".rodata"]
    text_bytes = bytes(text.content)
    rodata_bytes = bytes(rodata.content)
    text_vaddr = text.virtual_address
    rodata_vaddr = rodata.virtual_address
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)

    print(f"text 0x{text_vaddr:x} ({len(text_bytes) // 1024} KB)")
    print(f"rodata 0x{rodata_vaddr:x} ({len(rodata_bytes) // 1024} KB)")

    t0 = time.time()
    print("\n[1/2] Building ADRP index ...")
    adrp_index = build_adrp_index(text_vaddr, text_bytes)
    print(f"      {len(adrp_index)} pages, "
          f"{sum(len(v) for v in adrp_index.values())} ADRPs, "
          f"{time.time() - t0:.1f}s")

    summary = {}
    print("\n[2/2] Scanning interesting substrings ...")
    for needle, desc in INTERESTING_SUBSTRINGS.items():
        # Find first occurrence of this exact substring
        idx = rodata_bytes.find(needle)
        if idx < 0:
            print(f"\n  '{needle.decode(errors='replace')}' NOT FOUND")
            continue
        vaddr = rodata_vaddr + idx

        xrefs = find_xrefs(adrp_index, text_vaddr, text_bytes, vaddr, md)
        print(f"\n  '{needle.decode(errors='replace')}' → "
              f"str @ 0x{vaddr:x}: {len(xrefs)} xrefs ({desc})")
        for adrp_pc, ref_pc, snip in xrefs[:5]:
            print(f"    {adrp_pc:#x}: {snip}")

        summary[needle.decode(errors='replace')] = {
            "description": desc,
            "str_vaddr": f"0x{vaddr:x}",
            "xref_count": len(xrefs),
            "xref_pcs": [f"0x{p:x}" for p, _, _ in xrefs[:10]],
        }

    with (args.out / "string_xrefs_v3.json").open("w") as f:
        json.dump(summary, f, indent=2)
    print(f"\nWritten: {args.out / 'string_xrefs_v3.json'}")


if __name__ == "__main__":
    main()