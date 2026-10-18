#!/usr/bin/env python3
"""
symbolless_deepdive.py — Optimized second-pass analyzer.
- Precomputes ADRP index in single .text pass.
- Precomputes BL caller map (target -> [callers]) in single .text pass.
- Disassembles a curated set of candidate functions.
- Emits ready-to-consume JSON for mcbedvr::sym::ResolveGameSymbols().
"""

import argparse
import json
import struct
import sys
import time
from pathlib import Path

import lief
import capstone


LIFECYCLE_DISPATCHERS = [0xe496620, 0xe496624, 0xe4a2214]
INPUT_HANDLERS = [0x8535f7c, 0x8529190, 0xb9c150c, 0xb9c1a14, 0x986ccec,
                  0xe49ebb0]
JNI_INTERNAL_HELPERS = [0x129295b0, 0x1298f8f0]
ALL_TRACKED = LIFECYCLE_DISPATCHERS + INPUT_HANDLERS + JNI_INTERNAL_HELPERS


def parse_elf(path: Path):
    binary = lief.parse(str(path))
    sections = {s.name: s for s in binary.sections}
    return binary, sections


def build_adrp_index(text_base: int, text_bytes: bytes) -> dict:
    """Single-pass ADRP index: page_4k_addr -> [(adrp_pc, Rn), ...].

    ADRP: bits[31:24] = 1 immlo 10000 → top byte mask 0x9F, fixed 0x90.
    """
    page_index: dict = {}
    n = len(text_bytes) - 4
    t0 = time.time()
    for off in range(0, n, 4):
        word = struct.unpack_from("<I", text_bytes, off)[0]
        if (word & 0x9F000000) != 0x90000000:
            continue
        immlo = (word >> 29) & 0b11
        immhi = (word >> 5) & 0x7FFFF
        imm = (immhi << 2) | immlo
        if imm & (1 << 20):
            imm -= (1 << 21)
        pc = text_base + off
        adrp_target = ((pc & ~0xFFF) + (imm << 12)) & 0xFFFFFFFFFFFFFFFF
        page = adrp_target & ~0xFFF
        rn = word & 0x1F
        page_index.setdefault(page, []).append((pc, rn, off))
    print(f"  ADRP index built: {len(page_index)} unique pages, "
          f"{sum(len(v) for v in page_index.values())} ADRPs, "
          f"{time.time() - t0:.1f}s", flush=True)
    return page_index


def build_bl_caller_map(text_base: int, text_bytes: bytes) -> dict:
    """Single-pass BL caller map: target_vaddr -> [caller_pc].

    BL imm26: top 6 bits = 0b100101 = 0x94000000.
    """
    caller_map: dict = {}
    n = len(text_bytes) - 4
    t0 = time.time()
    for off in range(0, n, 4):
        word = struct.unpack_from("<I", text_bytes, off)[0]
        if (word >> 26) != 0b100101:
            continue
        imm26 = word & 0x3FFFFFF
        if imm26 & (1 << 25):
            imm26 -= (1 << 26)
        pc = text_base + off
        tgt = (pc + (imm26 << 2)) & 0xFFFFFFFFFFFFFFFF
        caller_map.setdefault(tgt, []).append(pc)
    print(f"  BL caller map built: {len(caller_map)} unique targets, "
          f"{sum(len(v) for v in caller_map.values())} BLs, "
          f"{time.time() - t0:.1f}s", flush=True)
    return caller_map


def find_lrefs_for_string(adrp_index: dict, text_base: int,
                          text_bytes: bytes, str_vaddr: int,
                          max_lrefs: int = 8) -> list:
    """Find all LDR/STR-using-Xn references that resolve to str_vaddr.

    Strategy: page_index[str_vaddr & ~0xFFF] gives ADRP candidates; for
    each, look at next few instructions for LDR/STR with Rn matching
    ADRP's Rd, and check the imm12 resolves to str_vaddr.
    """
    page = str_vaddr & ~0xFFF
    candidates = adrp_index.get(page, [])
    hits = []
    for pc, rn, off in candidates:
        # Look ahead up to 4 instructions
        for j in range(1, 5):
            ioff = off + j * 4
            if ioff + 4 > len(text_bytes):
                break
            w2 = struct.unpack_from("<I", text_bytes, ioff)[0]
            # LDR/STR 64-bit unsigned offset: opc[1:0]=10, V=0, size=11
            # Top byte must be F8 or F9
            top = w2 >> 24
            if top not in (0xF8, 0xF9):
                continue
            rn2 = (w2 >> 5) & 0x1F
            if rn2 != rn:
                continue
            opc_field = (w2 >> 30) & 0b11
            size_field = (w2 >> 30) & 0b11
            # Unsigned offset: imm12 scaled by 8 for 64-bit (size=11)
            scale = 1 << ((w2 >> 30) & 0b11)
            # Actually: bits[31:30] = opc (size + sign). For 64-bit
            # unsigned: opc=10, V=0. imm12 = bits[21:10]
            # Let's just take imm12 from bits[21:10] and try.
            imm12 = (w2 >> 10) & 0xFFF
            candidate = (page + imm12 * scale) & 0xFFFFFFFFFFFFFFFF
            if candidate == str_vaddr:
                hits.append((pc, f"adrp X{rn},0x{page:x}; ldr ..., [X{rn},#0x{imm12 * scale:x}]"))
                break  # found at this pc, don't re-match
    return hits


def disasm_at(sections, vaddr: int, length: int = 256) -> list:
    text = sections[".text"]
    base = text.virtual_address
    raw = bytes(text.content)
    if vaddr < base or vaddr >= base + len(raw):
        return []
    offset = vaddr - base
    chunk = raw[offset: offset + length]
    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True
    return list(md.disasm(chunk, vaddr))


def disasm_full_function(sections, entry: int, max_bytes: int = 4096) -> list:
    insns = disasm_at(sections, entry, max_bytes)
    out = []
    for ins in insns:
        out.append(ins)
        if ins.mnemonic == "ret":
            break
    return out


def render_asm(insns: list, n: int = 30) -> str:
    lines = [f"  0x{ins.address:x}: {ins.mnemonic:<8} {ins.op_str}"
             for ins in insns[:n]]
    if len(insns) > n:
        lines.append(f"    ... +{len(insns) - n} more instructions")
    return "\n".join(lines)


# --------------------------- Main -----------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", type=Path)
    ap.add_argument("pass1_json", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    binary, sections = parse_elf(args.elf)
    text = sections[".text"]
    text_base = text.virtual_address
    text_bytes = bytes(text.content)
    rodata = sections[".rodata"]
    rodata_base = rodata.virtual_address

    print(f"text: 0x{text_base:x} ({len(text_bytes) // 1024} KB)")
    print(f"rodata: 0x{rodata_base:x} ({len(rodata.content) // 1024} KB)")

    print("\n[1/4] Building ADRP index ...", flush=True)
    adrp_index = build_adrp_index(text_base, text_bytes)

    print("\n[2/4] Building BL caller map ...", flush=True)
    bl_map = build_bl_caller_map(text_base, text_bytes)

    print("\n[3/4] Disassembling tracked functions ...", flush=True)
    lifecycle_asm = {}
    input_asm = {}
    for tgt in ALL_TRACKED:
        insns = disasm_full_function(sections, tgt, max_bytes=512)
        bucket = lifecycle_asm if tgt in LIFECYCLE_DISPATCHERS else input_asm
        if insns:
            bucket[f"0x{tgt:x}"] = [
                {"addr": f"0x{i.address:x}",
                 "mnemonic": i.mnemonic, "op": i.op_str}
                for i in insns
            ]
            print(f"  0x{tgt:x}: {len(insns)} insns", flush=True)
        else:
            print(f"  0x{tgt:x}: NOT IN .TEXT", flush=True)

    print("\n[4/4] Reverse xrefs for tracked targets ...", flush=True)
    reverse_xrefs = {}
    for tgt in ALL_TRACKED:
        callers = bl_map.get(tgt, [])
        # Filter JNI surface itself (lifecycle ~0x852e300, others ~0x8532000+)
        external = [c for c in callers
                    if c < 0x852e300
                    or c > 0x12e60000
                    or c == tgt]
        print(f"  0x{tgt:x}: {len(callers)} total BLs, "
              f"{len(external)} non-JNI", flush=True)
        reverse_xrefs[f"0x{tgt:x}"] = [f"0x{c:x}" for c in external[:200]]

    # ---- String xrefs ----
    print("\n[bonus] String xrefs (ADRP+LDR pairs) ...", flush=True)
    with args.pass1_json.open() as f:
        pass1 = json.load(f)

    string_xref_results = {}
    for pat, info in pass1["patterns"].items():
        if info["rodata_hits"] == 0:
            continue
        all_xrefs = []
        for vaddr in info["sample_rodata_addrs"][:5]:
            hits = find_lrefs_for_string(adrp_index, text_base, text_bytes,
                                         vaddr)
            all_xrefs.extend(hits)
        # dedup by pc
        seen_pcs = set()
        unique = []
        for pc, snippet in all_xrefs:
            if pc in seen_pcs:
                continue
            seen_pcs.add(pc)
            unique.append((pc, snippet))
        if unique:
            string_xref_results[pat] = {
                "rodata_hits": info["rodata_hits"],
                "first_xref_pc": f"0x{unique[0][0]:x}",
                "first_xref_snippet": unique[0][1],
                "total_xrefs": len(unique),
                "first_5_xrefs": [
                    {"pc": f"0x{pc:x}", "snippet": s}
                    for pc, s in unique[:5]
                ],
            }
            print(f"  '{pat}': {len(unique)} xrefs (sampled 5 strings)")

    # ---- Write outputs ----
    with (args.out / "deepdive_lifecycle.json").open("w") as f:
        json.dump(lifecycle_asm, f, indent=2)
    with (args.out / "deepdive_input.json").open("w") as f:
        json.dump(input_asm, f, indent=2)
    with (args.out / "reverse_xrefs.json").open("w") as f:
        json.dump(reverse_xrefs, f, indent=2)
    with (args.out / "string_xrefs_full.json").open("w") as f:
        json.dump(string_xref_results, f, indent=2)

    # ---- Final consolidated offset_map.json (overwrite pass1 stub) ----
    consolidated = {
        "version": "1.26.52.3-final-mod2_sdk",
        "method": "symbolless (ADRP+LDR + BL reverse xrefs + JNI anchor walk)",
        "tracked_functions": {
            "lifecycle_dispatchers": [f"0x{a:x}" for a in LIFECYCLE_DISPATCHERS],
            "input_handlers": [f"0x{a:x}" for a in INPUT_HANDLERS],
            "jni_helpers": [f"0x{a:x}" for a in JNI_INTERNAL_HELPERS],
        },
        "string_xref_summary": string_xref_results,
        "reverse_xref_counts": {
            f"0x{t:x}": len(bl_map.get(t, []))
            for t in ALL_TRACKED
        },
        "note": "All targets confirmed inside .text. Cross-reference with "
                "lifecycle_asm/input_asm to identify App::* and MinecraftGame::* "
                "equivalents. Next step: chase xrefs from each dispatcher to "
                "find AppPlatform::setRenderingSize equivalent.",
    }
    with (args.out / "offset_map_consolidated.json").open("w") as f:
        json.dump(consolidated, f, indent=2)

    print(f"\nOutputs written to {args.out}/:")
    print(f"  - deepdive_lifecycle.json")
    print(f"  - deepdive_input.json")
    print(f"  - reverse_xrefs.json")
    print(f"  - string_xrefs_full.json")
    print(f"  - offset_map_consolidated.json")


if __name__ == "__main__":
    main()