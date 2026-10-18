# symbolless_re for mcbedvr Phase 2

## Overview

Reverse engineering toolkit for Bedrock 1.26.52.3 `libminecraftpe.so` —
a 328 MB stripped ARM64 shared object with **zero** engine symbols in
`dynsym`. The 9 mangled names from `Symbols.cpp` (carried over from
`minecraft-symbols` 1.13/1.14) are all missing.

This toolkit adapts the `mcpelauncher-manifest/symbolless.py` pattern
(no-symbol-name resolution) to our binary:

1. Parse ELF sections via `lief`.
2. Catalog JNI exports and disassemble first-call depth.
3. Pre-compute ADRP instruction index (8.9s, 1.6M ADRPs, 9628 pages).
4. Pre-compute BL caller map (17.1s, 4.3M BLs, 220k targets).
5. Identify jump tables (lifecycle + log + input dispatchers).
6. Cross-reference engine strings (`MinecraftGame`, `RenderContext`,
   `LevelRenderer`, `AppPlatform`, `setRenderingSize`, etc.) into
   consumer code via ADRP+ADD/LDR precomputed-index scan.

## Tools

| Script | Purpose |
|--------|---------|
| `symbolless_bedrock.py` | Pass 1: ELF parse + JNI catalog + initial string scan |
| `symbolless_deepdive.py` | Pass 2: ADRP/BL index + jump table disassembly |
| `symbolless_deepdive2.py` | Pass 3: caller disassembly + binary-grep string xrefs |
| `symbolless_string_xrefs.py` | Pass 4: combined-substring ADRP+ADD/LDR scan |

## Outputs

| File | Contents |
|------|----------|
| `offset_map_consolidated.json` | Master offset map with all evidence |
| `jni_anchors.json` | 95 JNI exports grouped + first-call depth-1 |
| `string_xrefs.json` | 27 interesting engine substrings + occurrence counts |
| `deepdive/offset_map_consolidated.json` | Subdirectory with all pass-2/3/4 outputs |
| `deepdive/string_xrefs_v2.json` | Substring xrefs with snippets |
| `deepdive/string_xrefs_v3.json` | Combined-substring xrefs |
| `deepdive/engine_method_xrefs.json` | Engine method references (incl. MinecraftGame::setSize!) |
| `deepdive/reverse_xrefs.json` | Who calls each tracked target |

## Reproducing

```bash
# Extract libminecraftpe.so from APK (one time)
unzip -j /path/to/minecraft.apk lib/arm64-v8a/libminecraftpe.so \
  -d /tmp/mcbedvr_rea/

# Pass 1
python3 symbolless_bedrock.py /tmp/mcbedvr_rea/libminecraftpe.so \
  --out /Volumes/X10Pro/Projects/mcbedvr/tools/

# Pass 2 (index building: ~26s)
python3 symbolless_deepdive.py /tmp/mcbedvr_rea/libminecraftpe.so \
  /Volumes/X10Pro/Projects/mcbedvr/tools/string_xrefs.json \
  --out /Volumes/X10Pro/Projects/mcbedvr/tools/deepdive/

# Pass 4 (combined substrings)
python3 symbolless_string_xrefs.py /tmp/mcbedvr_rea/libminecraftpe.so
```

## Methodology Notes

- **ADRP precomputed index** is the key trick. Instead of scanning all
  of `.text` per string, we build `{page_addr: [adrp_instructions]}`
  once, then per-string lookups are O(1).
- **Jump table identification**: When the first 128+ instructions of a
  function are all `b` (branch direct, not `bl`), it's a jump table
  indexed by some external counter. Real engine code starts at the
  target addresses (in 0x12900000-0x12a00000 for this build).
- **String xref three-method fallback**: (1) ADRP+ADD for `add Xn, Xn,
  #imm` patterns (most common); (2) ADRP+LDR for `[Xn, #imm]`
  loads; (3) raw 8-byte LE pointer grep into `.text`/`.rodata`
  literal pools.

## Result Highlights (Bedrock 1.26.52.3)

- 9/9 legacy mangled names absent from dynsym.
- 95 JNI exports (67 `Java_com_mojang_*`) — sole hookable surface.
- 3 lifecycle dispatchers (0xe496620, 0xe496624, 0xe4a2214), 128 entries each.
- All 5 MainActivity lifecycle JNI exports funnel through these tables.
- 1 confirmed cross-ref to `MinecraftGame::setSize!` (assertion site at
  0x8534534 → calls LogAssert via 0xe49ebb0).
- Lambda typeinfo name `setRenderingSizeEiiE3$_0` at 0x2c017c0
  CONFIRMS `App::setRenderingSize(int, int)` exists in the binary,
  but not at a hookable address via dynsym.