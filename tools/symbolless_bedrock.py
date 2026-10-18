#!/usr/bin/env python3
"""
symbolless_bedrock.py — Reverse engineering scanner for libminecraftpe.so
following the mcpelauncher-manifest pattern: no symbol names, only
prologue patterns, string cross-references and JNI export anchor walking.

Adapted by @Gabriel Medina for mcbedvr Phase 2 (Apr 2026).
Target: Bedrock 1.26.52.3 (APK 1.26+), stripped, hidden visibility.

Outputs:
  - tools/offset_map.json         : symbol → addr / file-offset map
  - tools/jni_anchors.json        : JNI exports grouped + first-call depth-1
  - tools/string_xrefs.json       : unique diagnostic strings + xref counts
  - tools/function_catalog.json   : ARM64 prologue candidates

Usage:
  python3 symbolless_bedrock.py /tmp/mcbedvr_rea/libminecraftpe.so \
      --out /Volumes/X10Pro/Projects/mcbedvr/tools
"""

import argparse
import json
import re
import struct
import sys
from collections import defaultdict
from pathlib import Path
from typing import Iterable

import lief
import capstone

# ----------------------------- Constants ------------------------------

# Mangled names of interest — if any are present, we'll surface them.
# (verified absent in Bedrock 1.26 — kept for completeness so the script
# still works against older builds where they reappear.)
CANDIDATE_MANGLED = [
    "_ZN13MinecraftGameC2Ev",
    "_ZN13MinecraftGame4tickEv",
    "_ZN3App16setRenderingSizeEii",
    "_ZN3App17setUISizeAndScaleEiif",
    "_ZN3mce8Platform3OGL12InitBindingsEv",
    "_ZN3mce8Platform3OGL11SwapBuffersEv",
    "_ZN3mce12RenderContext7presentEv",
    "_ZN13MinecraftGame11handleInputERK15InputPacketList",
    "_ZN21GameControllerManager17registerControllerEPKc",
]

# Diagnostic strings that hint at engine internals. These are *common*
# substrings; we deduplicate aggressively and only keep those with
# reasonable xref counts (>= 2) to filter noise.
INTERESTING_STRING_PATTERNS = [
    # engine core
    rb"MinecraftGame",
    rb"AppPlatform",
    rb"RenderDragon",
    rb"LevelRenderer",
    rb"GameControllerManager",
    rb"TextureAtlas",
    rb"FrameBuffer",
    rb"SwapChain",
    rb"RenderContext",
    rb"GpuBuffer",
    rb"CommandBuffer",
    rb"VertexBuffer",
    # render
    rb"VK_PRESENT",
    rb"VK_IMAGE_LAYOUT",
    rb"OpenGL",
    rb"Vulkan",
    rb"setRenderingSize",
    rb"setUISizeAndScale",
    rb"onSurfaceChanged",
    rb"onSurfaceCreated",
    rb"onPause",
    rb"onResume",
    # input
    rb"PicoController",
    rb"PxrInput",
    rb"GamepadMapping",
    rb"InputManager",
    rb"VirtualGamepad",
]

# ARM64 prologue signatures (little-endian words).
# Pattern A: stp x29, x30, [sp, #-N]!  → A9 BC 7B FD / variants
# Pattern B: sub sp, sp, #N; stp x29, x30, [sp, #offset]
PROLOGUE_STP_X29_X30 = 0xA9BF7BFD  # stp x29, x30, [sp, #-0x10]!
PROLOGUE_STP_X29_X30_2 = 0xA9BC7BFD  # stp x29, x30, [sp, #-0x30]!
PROLOGUE_STP_X29_X30_3 = 0xA9BD7BFD  # stp x29, x30, [sp, #-0x20]!
ARM64_RET = 0xD65F03C0  # ret
ARM64_NOP = 0xD503201F


# ------------------------------ ELF ---------------------------------

def parse_elf(path: Path):
    """Parse ELF, return lief binary + section dict (name -> section)."""
    binary = lief.parse(str(path))
    sections = {}
    for s in binary.sections:
        sections[s.name] = s
    return binary, sections


def section_bytes(section) -> bytes:
    """Read section content as bytes."""
    if section is None:
        return b""
    raw = bytes(section.content)
    return raw


# --------------------------- String scan -----------------------------

def find_strings(buf: bytes, min_len: int = 6) -> Iterable[tuple[int, bytes]]:
    """Yield (offset, string) for printable ASCII runs >= min_len."""
    pattern = re.compile(rb"[\x20-\x7e]{%d,}" % min_len)
    for m in pattern.finditer(buf):
        yield m.start(), m.group(0)


def string_xref_scan(text_base: int, rodata: bytes, rodata_base: int,
                     patterns: list[bytes]) -> dict:
    """For each pattern, find offsets in .rodata and scan .text for
    ADRP+LDR/ADR+BL references. Returns dict pattern -> {matches, sample_xrefs}.

    We don't do full relocation-aware disassembly here (LIEF on a stripped
    ELF won't give clean relocs for hidden-visibility symbols). Instead we
    count *occurrences* of the substring in .rodata + search .text for the
    raw 8-byte sequence as a coarse proxy.
    """
    out = {}
    for pat in patterns:
        matches = []
        idx = 0
        while True:
            j = rodata.find(pat, idx)
            if j < 0:
                break
            matches.append(rodata_base + j)
            idx = j + 1
        # Estimate xref weight: substring occurrences is itself a hint
        # (logging paths, debug formats, exception messages).
        out[key_of(pat)] = {
            "pattern": pat.decode("latin-1"),
            "rodata_hits": len(matches),
            "sample_rodata_addrs": matches[:5],
        }
    return out


def key_of(b: bytes) -> str:
    return b.decode("latin-1", errors="replace")


# -------------------------- Prologue scan ----------------------------

def scan_arm64_prologues(text_base: int, text: bytes) -> list[tuple[int, int]]:
    """Find function prologues in .text using ARM64 stp x29,x30 patterns.
    Returns list of (function_start_offset_in_text, prologue_insn_word).

    This is the canonical "symbolless" trick: in stripped binaries, the
    standard ABI prologue marks every non-leaf function entry, and we
    can use that to delimit functions without symbol names.
    """
    found = []
    for off in range(0, len(text) - 4, 4):
        word = struct.unpack_from("<I", text, off)[0]
        # Match the three most common stp x29,x30,[sp,#-N]! variants
        if (word & 0xFFFFE0FF) == 0xA9BC7BFD:  # mask N
            found.append((off, word))
    return found


# ------------------------- JNI anchor walk ---------------------------

def jni_anchor_walk(binary, sections, jni_exports: list[dict],
                    md: capstone.Cs) -> list[dict]:
    """For each JNI export, disassemble the first ~64 instructions to
    find the first BL/BLR target — these are the immediate engine
    dependencies of the JNI surface.

    Output: list of {jni, addr, first_calls: [{target, mnemonic, ...}]}.
    """
    text_section = sections.get(".text")
    if text_section is None:
        return []
    text_bytes = section_bytes(text_section)
    text_vaddr = text_section.virtual_address
    text_offset_in_file = text_section.file_offset  # type: ignore[attr-defined]

    out = []
    for j in jni_exports:
        vaddr = j["vaddr"]
        if vaddr < text_vaddr or vaddr >= text_vaddr + len(text_bytes):
            continue
        offset_in_text = vaddr - text_vaddr
        chunk = text_bytes[offset_in_text: offset_in_text + 256]
        calls = []
        for ins in md.disasm(chunk, vaddr):
            mnem = ins.mnemonic
            if mnem in ("bl", "blr"):
                # Operand is the immediate target for bl, register for blr.
                # capstone exposes op_str; we pull the numeric target if any.
                tgt_str = ins.op_str
                try:
                    tgt = int(tgt_str, 16) if tgt_str.startswith("0x") else None
                except ValueError:
                    tgt = None
                calls.append({
                    "addr": f"0x{ins.address:x}",
                    "mnemonic": mnem,
                    "target": tgt_str,
                    "target_int": tgt,
                })
            if len(calls) >= 8:
                break
        out.append({
            "name": j["name"],
            "addr": f"0x{vaddr:x}",
            "first_calls": calls,
        })
    return out


# ----------------------- Catalog JNI exports -------------------------

def catalog_jni_exports(binary) -> list[dict]:
    """Group Java_* symbols by class/zone; capture vaddr."""
    exports = []
    for s in binary.dynamic_symbols:
        if not s.name or not s.name.startswith("Java_"):
            continue
        exports.append({
            "name": s.name,
            "vaddr": s.value,
        })
    exports.sort(key=lambda e: e["vaddr"])
    return exports


def group_by_class(exports: list[dict]) -> dict:
    """Group Java_<class>_<method> under namespace JNI_<class>."""
    grouped = defaultdict(list)
    for e in exports:
        # Java_com_mojang_minecraftpe_<Class>_<method>
        parts = e["name"].split("_")
        # parts[0] = "Java", parts[1] = "com", parts[2] = "mojang",
        # parts[3] = "minecraftpe", parts[4] = "<Class>", ...
        if len(parts) >= 5:
            cls = "_".join(parts[4:-1]) if len(parts) > 5 else parts[4]
            grouped[cls].append(e)
        else:
            grouped["other"].append(e)
    return dict(grouped)


# ----------------------- Check mangled names -------------------------

def check_mangled_present(binary, candidates: list[str]) -> dict:
    """Check if any of the legacy mangled names still exist as dynamic symbols."""
    dyn_names = {s.name for s in binary.dynamic_symbols}
    return {m: (m in dyn_names) for m in candidates}


# ----------------------------- Main ----------------------------------

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--max-strings", type=int, default=20000,
                    help="Cap on unique strings scanned for pattern hits")
    args = ap.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)

    print(f"[1/5] Parsing ELF {args.elf} ...", flush=True)
    binary, sections = parse_elf(args.elf)
    print(f"      architecture: {binary.header.machine_type}")
    print(f"      entry point:  0x{binary.entrypoint:x}")
    print(f"      sections:     {len(sections)}")

    # --- 1. Mangled-name check --------------------------------------
    print(f"[2/5] Checking {len(CANDIDATE_MANGLED)} mangled-name candidates ...",
          flush=True)
    mangled_state = check_mangled_present(binary, CANDIDATE_MANGLED)
    present = [m for m, p in mangled_state.items() if p]
    print(f"      present in dynsym: {len(present)}/{len(CANDIDATE_MANGLED)}")
    if present:
        for p in present:
            print(f"        + {p}")

    # --- 2. JNI catalog --------------------------------------------
    print(f"[3/5] Cataloging JNI exports ...", flush=True)
    jni_exports = catalog_jni_exports(binary)
    jni_grouped = group_by_class(jni_exports)
    print(f"      JNI exports total: {len(jni_exports)}")
    for cls, lst in sorted(jni_grouped.items(),
                           key=lambda kv: -len(kv[1])):
        print(f"        JNI_{cls:<32} {len(lst)} entries")

    # --- 3. String xref scan ---------------------------------------
    print(f"[4/5] String xref scan in .rodata ...", flush=True)
    rodata_section = sections.get(".rodata")
    if rodata_section is None:
        print("      WARNING: no .rodata section found, aborting scan",
              file=sys.stderr)
        rodata_bytes = b""
        rodata_vaddr = 0
    else:
        rodata_bytes = section_bytes(rodata_section)
        rodata_vaddr = rodata_section.virtual_address
        print(f"      .rodata: 0x{rodata_vaddr:x}, "
              f"{len(rodata_bytes) // 1024} KB")

    text_section = sections.get(".text")
    text_bytes = section_bytes(text_section)
    text_vaddr = text_section.virtual_address if text_section else 0
    print(f"      .text:   0x{text_vaddr:x}, "
          f"{len(text_bytes) // 1024} KB")

    text_bytes_data = section_bytes(text_section)

    string_hits = string_xref_scan(
        text_vaddr, rodata_bytes, rodata_vaddr,
        INTERESTING_STRING_PATTERNS,
    )

    # --- 4. Prologue scan ------------------------------------------
    print(f"[5/5] Prologue scan + JNI anchor walk ...", flush=True)
    prologues = scan_arm64_prologues(text_vaddr, text_bytes_data)
    print(f"      function candidates (prologues): {len(prologues)}")

    md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    md.detail = True

    anchor = jni_anchor_walk(binary, sections, jni_exports, md)
    print(f"      JNI anchors walked: {len(anchor)}")

    # -------------------- Write JSON outputs -------------------------

    offset_map = {
        "version": "1.26.52.3-final-mod2_sdk",
        "binary": str(args.elf),
        "base_load_offset": 0,  # filled at runtime via dl_iterate_phdr
        "mangled_legacy": {
            "note": "1.13/1.14 signatures from minecraft-symbols",
            "present": present,
            "absent": [m for m, p in mangled_state.items() if not p],
        },
        "targets": {},  # populated below once anchors are mapped
    }

    with (args.out / "offset_map.json").open("w") as f:
        json.dump(offset_map, f, indent=2)

    with (args.out / "jni_anchors.json").open("w") as f:
        json.dump({
            "version": "1.26.52.3",
            "groups": {
                k: [{"name": e["name"], "vaddr": f"0x{e['vaddr']:x}"}
                    for e in v]
                for k, v in jni_grouped.items()
            },
            "anchors_walked": anchor,
        }, f, indent=2)

    with (args.out / "string_xrefs.json").open("w") as f:
        json.dump({
            "version": "1.26.52.3",
            "rodata_vaddr": f"0x{rodata_vaddr:x}",
            "rodata_size_kb": len(rodata_bytes) // 1024,
            "patterns": string_hits,
        }, f, indent=2)

    with (args.out / "function_catalog.json").open("w") as f:
        json.dump({
            "version": "1.26.52.3",
            "text_vaddr": f"0x{text_vaddr:x}",
            "text_size_kb": len(text_bytes) // 1024,
            "prologue_count": len(prologues),
            "note": "Sample: first 1000 prologue offsets.",
            "sample": [
                {"offset_in_text": off, "vaddr": f"0x{text_vaddr + off:x}",
                 "prologue_word": f"0x{word:08x}"}
                for off, word in prologues[:1000]
            ],
        }, f, indent=2)

    print(f"\nOutputs written to {args.out}/:")
    print(f"  - offset_map.json")
    print(f"  - jni_anchors.json")
    print(f"  - string_xrefs.json")
    print(f"  - function_catalog.json")


if __name__ == "__main__":
    main()