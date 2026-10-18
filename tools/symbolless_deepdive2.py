#!/usr/bin/env python3
"""
symbolless_deepdive2.py — Targeted analyzer:
1. Disassembles the immediate caller of 0xe496620 (lifecycle dispatcher 1).
2. Binary-greps string addresses in .text to find cross-references.
3. Maps each interesting string to its first ~5 xrefs.
"""

import argparse
import json
import struct
import sys
from pathlib import Path

import lief
import capstone


def parse_elf(path: Path):
    binary = lief.parse(str(path))
    sections = {s.name: s for s in binary.sections}
    return binary, sections


def disasm_at(sections, vaddr: int, length: int = 1024) -> list:
    text = sections[".text"]
    base = text.virtual_address
    raw = bytes(text.content)
    if vaddr < base or vaddr >= base + len(raw):
        return []
    off = vaddr - base
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True
    return list(md.disasm(raw[off: off + length], vaddr))


def find_string_xrefs_binary(raw_text: str, str_vaddr: int,
                            text_bytes: bytes, rodata_bytes: bytes,
                            rodata_offset_in_file: int,
                            text_offset_in_file: int,
                            max_results: int = 16) -> list:
    """Search .text and .rodata for the literal address `str_vaddr`.

    The compiler may embed 8-byte LE pointers to strings in:
      - .text  (in literal pools near functions)
      - .rodata (in vtables, RTTI, exception tables)

    Returns list of (container, file_offset_in_container, vaddr_in_elf).
    """
    needle = struct.pack("<Q", str_vaddr)
    hits = []

    idx = 0
    while True:
        j = text_bytes.find(needle, idx)
        if j < 0:
            break
        # file offset = text_offset_in_file + j
        hits.append(("text", text_offset_in_file + j,
                     text_offset_in_file + j))
        idx = j + 1
        if len(hits) >= max_results:
            break

    idx = 0
    while True:
        j = rodata_bytes.find(needle, idx)
        if j < 0:
            break
        hits.append(("rodata", rodata_offset_in_file + j,
                     rodata_offset_in_file + j))
        idx = j + 1
        if len(hits) >= max_results:
            break

    return hits


def find_adrp_add_lrefs(sections, str_vaddr: int,
                        max_results: int = 32) -> list:
    """Find ADRP + ADD/LDR pair references to str_vaddr in .text.

    ADRP followed within 1-4 insns by ADD Xn, Xn, #imm OR LDR Xm, [Xn, #imm].
    """
    text = sections[".text"]
    base = text.virtual_address
    raw = bytes(text.content)
    page = str_vaddr & ~0xFFF
    page_offset = str_vaddr & 0xFFF

    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True

    found = []
    for off in range(0, len(raw) - 4, 4):
        word = struct.unpack_from("<I", raw, off)[0]
        # ADRP encoding: 1 immlo 10000 immhi Rd
        if (word & 0x9F000000) != 0x90000000:
            continue
        immlo = (word >> 29) & 0b11
        immhi = (word >> 5) & 0x7FFFF
        imm = (immhi << 2) | immlo
        if imm & (1 << 20):
            imm -= (1 << 21)
        pc = base + off
        adrp_target = ((pc & ~0xFFF) + (imm << 12)) & 0xFFFFFFFFFFFFFFFF
        if adrp_target != page:
            continue
        rn = word & 0x1F
        # Look ahead up to 6 instructions for ADD or LDR using Xn == rn
        for j in range(1, 7):
            ioff = off + j * 4
            if ioff + 4 > len(raw):
                break
            ins = next(md.disasm(raw[ioff: ioff + 4], base + ioff), None)
            if ins is None:
                continue
            # Case 1: ADD Xn, Xn, #page_offset  (offset stays in page)
            if (ins.mnemonic == "add"
                    and f"x{rn}" in ins.op_str.lower()
                    and f"#{page_offset:x}" in ins.op_str.lower()):
                found.append((pc, f"adrp X{rn}, 0x{page:x}; add X{rn}, X{rn}, #0x{page_offset:x}"))
                break
            # Case 2: LDR X?, [Xn, #page_offset]  (direct load)
            if ins.mnemonic.startswith("ldr") and f"[x{rn}" in ins.op_str.lower():
                if f"#{page_offset:x}" in ins.op_str.lower() or f"#{page_offset:x}" in ins.op_str:
                    found.append((pc, ins.op_str))
                    break
    return found[:max_results]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    binary, sections = parse_elf(args.elf)
    text = sections[".text"]
    rodata = sections[".rodata"]
    text_bytes = bytes(text.content)
    rodata_bytes = bytes(rodata.content)
    text_offset_in_file = text.file_offset  # type: ignore[attr-defined]
    rodata_offset_in_file = rodata.file_offset  # type: ignore[attr-defined]
    text_vaddr = text.virtual_address
    rodata_vaddr = rodata.virtual_address

    print(f"text vaddr 0x{text_vaddr:x} (offset 0x{text_offset_in_file:x})")
    print(f"rodata vaddr 0x{rodata_vaddr:x} (offset 0x{rodata_offset_in_file:x})")

    # ---- 1. Disassemble immediate callers of jump tables ----
    print("\n=== Disassembly: caller 0x64c98c4 (of 0xe496620) ===")
    caller_insns = disasm_at(sections, 0x64c98c4, length=512)
    print(f"  {len(caller_insns)} instructions")
    for ins in caller_insns[:40]:
        print(f"  0x{ins.address:x}: {ins.mnemonic:<10} {ins.op_str}")
    if len(caller_insns) > 40:
        print(f"    ... +{len(caller_insns) - 40} more")

    print("\n=== Disassembly: caller 0x64c98e0 (of 0xe496624) ===")
    caller2_insns = disasm_at(sections, 0x64c98e0, length=512)
    print(f"  {len(caller2_insns)} instructions")
    for ins in caller2_insns[:40]:
        print(f"  0x{ins.address:x}: {ins.mnemonic:<10} {ins.op_str}")

    print("\n=== Disassembly: caller 0x6d5ba18 (of 0xe4a2214) ===")
    caller3_insns = disasm_at(sections, 0x6d5ba18, length=512)
    print(f"  {len(caller3_insns)} instructions")
    for ins in caller3_insns[:40]:
        print(f"  0x{ins.address:x}: {ins.mnemonic:<10} {ins.op_str}")

    # ---- 2. Binary grep string addresses ----
    print("\n=== String xrefs (binary grep for string vaddrs in .text/.rodata) ===")
    interesting_strings = {
        "MinecraftGame": [],
        "setRenderingSize": [],
        "AppPlatform": [],
        "LevelRenderer": [],
        "RenderContext": [],
        "RenderDragon": [],
        "TextureAtlas": [],
        "FrameBuffer": [],
        "onSurfaceChanged": [],
        "onSurfaceCreated": [],
        "VertexBuffer": [],
        "GameControllerManager": [],
        "SwapChain": [],
    }

    # For each interesting pattern, find first 5 addresses in .rodata
    for pat in list(interesting_strings.keys()):
        idx = 0
        while True:
            j = rodata_bytes.find(pat.encode("utf-8"), idx)
            if j < 0:
                break
            vaddr = rodata_vaddr + j
            hits = find_string_xrefs_binary(
                raw_text=str(vaddr),
                str_vaddr=vaddr,
                text_bytes=text_bytes,
                rodata_bytes=rodata_bytes,
                rodata_offset_in_file=rodata_offset_in_file,
                text_offset_in_file=text_offset_in_file,
                max_results=8,
            )
            # If no pointer hits, try ADRP+ADD search
            if not hits:
                adrp_hits = find_adrp_add_lrefs(sections, vaddr, max_results=4)
                if adrp_hits:
                    hits = [("adrp_add", pc, snippet)
                            for pc, snippet in adrp_hits]
            interesting_strings[pat].append({
                "str_vaddr": f"0x{vaddr:x}",
                "hit_count": len(hits),
                "first_5": [
                    {"container": h[0], "offset_in_file": f"0x{h[1]:x}"}
                    for h in hits[:5]
                ] if hits else [],
            })
            idx = j + 1
            if len(interesting_strings[pat]) >= 6:
                break

    # ---- 3. Final report ----
    print("\n=== Interesting string xref summary ===")
    summary = {}
    for pat, hits in interesting_strings.items():
        total_xrefs = sum(h["hit_count"] for h in hits)
        if total_xrefs > 0:
            print(f"\n  '{pat}' total xrefs across samples: {total_xrefs}")
            for h in hits[:2]:
                print(f"    str@ {h['str_vaddr']} → {h['hit_count']} ptr hits")
                for x in h["first_5"][:3]:
                    print(f"      {x['container']} @ file {x['offset_in_file']}")
            summary[pat] = {
                "total_xref_count_sampled": total_xrefs,
                "samples": hits[:10],
            }

    # ---- 5. Output ----
    with (args.out / "deepdive2_callers.json").open("w") as f:
        json.dump({
            "caller_0x64c98c4": [
                {"addr": f"0x{i.address:x}",
                 "mnemonic": i.mnemonic, "op": i.op_str}
                for i in caller_insns
            ],
            "caller_0x64c98e0": [
                {"addr": f"0x{i.address:x}",
                 "mnemonic": i.mnemonic, "op": i.op_str}
                for i in caller2_insns
            ],
            "caller_0x6d5ba18": [
                {"addr": f"0x{i.address:x}",
                 "mnemonic": i.mnemonic, "op": i.op_str}
                for i in caller3_insns
            ],
        }, f, indent=2)

    with (args.out / "deepdive2_string_xrefs.json").open("w") as f:
        json.dump(summary, f, indent=2)

    print(f"\nOutputs written to {args.out}/:")
    print(f"  - deepdive2_callers.json")
    print(f"  - deepdive2_string_xrefs.json")


if __name__ == "__main__":
    main()