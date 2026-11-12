# RR — Background shader sampling

- Audience: owner and contributors
- Date: November 12, 2026
- Success condition: determine whether the existing music background samples picture textures before attempting replacements.

## Current truth

**Proven:** The owner's `override/music_1/BACKGROUND.mnu` contains `COLOUR SHADER:int:1`. Offline inspection of the three background fragment programs found four texture reads in variant 0, none in variant 1 and none in variant 2.

**Proven, static:** A subsequent local trace established the selector calculation: the renderer loads its selector, multiplies it by four and addresses the shader pointer at `renderer + 0x14 + 4 * selector`. Initialization stores variants 0, 1 and 2 at offsets `0x14`, `0x18` and `0x1c`. Thus value 1 addresses variant 1. This is a static binding result; no runtime draw was observed in this study.

**Inference:** Replacing monthly background images alone will not display them through the configured music background, because the selected fragment program contains no texture reads. A new image binding and sampling path is required.

**Proven, static:** The copy fragment program contains one texture read, from sampler 0 using input texture coordinates. It is stored separately at renderer offset `0x20`. Its copy helper receives internal texture slots `0x2c` and `0x30` at the two direct call sites inspected. The surrounding background passes set 64×32 viewports. This is an internal transition path, not an established loader for external artwork.

**Unknown:** A supported native route to display detailed artwork full-screen and react to audio. No renderer change or hardware test was performed here.

## In plain language

Some existing backgrounds calculate colors instead of reading a picture. Changing a picture file cannot affect a shader that never reads it. The next requirement is a verified image-binding path, rather than a larger background texture or another camera edit.

## Evidence

Read-only inspection of the owner's resource produced:

| Fragment program | Microcode bytes | Instructions | Texture reads | Samplers |
| --- | ---: | ---: | ---: | --- |
| `back_colours0` | 8,608 | 395 | 4 | 0–3 |
| `back_colours1` | 1,264 | 51 | 0 | none |
| `back_colours2` | 32 | 1 | 0 | none |
| `back_colours_cpy` | 16 | 1 | 1 | 0 |
| `bg_copy` | 432 | 20 | 6 | 0–1 |

`tools/inspect_fragment.py` is a new read-only metadata inspector. It bounds revision-6 fragment programs, walks instruction framing, skips inline constants and counts texture opcodes. It does not execute shaders, assess branch reachability or modify executable resources. All five inspected programs ended exactly at their declared microcode boundary.

Bit-field and opcode references: [RPCS3 fragment format](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Program/RSXFragmentProgram.h) and [RPCS3 fragment opcodes](https://github.com/RPCS3/rpcs3/blob/master/rpcs3/Emu/RSX/Program/Assembler/FPOpcodes.h). The inspector is original project code, not a copied decompiler. Tests use generated input rather than Sony shaders.

**Proven:** 40 host tests pass. The added checks cover sampler extraction, inline constants, truncation, missing termination, incorrect profile, incorrect lengths and uninspected trailing microcode. These tests validate the inspector's stated scope, not console compatibility.

No new rendering screenshot exists because no console test was activated. Private shader metadata reports are retained; firmware resources and decoded instructions remain unpublished.

## Approved Miku input

The owner approved the smiling Miku picture. A local one-tile Gaia candidate now uses that picture as its input. Its unpacked size remains 3,991,408 bytes, with zero camera, menu or code changes. Candidate SHA-256: `3d1ea43ff2a290f9d1507fb967d2d215fa4d8861e1e945502cf64a0dc497d99d`.

This is one quarter-image tile in a globe texture allocation, not an implementation of full-screen artwork. It has not been uploaded or selected. It preserves a controlled texture test while the flat-image route is investigated.

## Selector and copy-pass trace

| Finding | Local module virtual address |
| --- | --- |
| Selector setter writes renderer field `0x1c8` | `0x4fdf0` |
| Read selector | `0x52ae8` |
| Multiply selector by four | `0x52af4` |
| Add shader-array offset `0x14` | `0x52b40` |
| Copy helper uses separate shader slot `0x20` | `0x526bc` |
| Pass internal texture slot `0x2c` to copy helper | `0x5294c` |
| Pass internal texture slot `0x30` to copy helper | `0x53164` |
| Background viewport setup receives width 64, height 32 | `0x52820`, `0x52fbc` |

These addresses identify observations in the owner's private module; they are not portable patches. The receipt is tied to the private ELF hash. Firmware bytes, instruction listings and private receipts remain unpublished.

**Inference:** Setting `COLOUR SHADER` to 3 would index the separate copy object rather than a fourth ordinary background variant. The normal background update addresses parameter members through offset `0x114`, whereas the copy object is constructed as a different, smaller wrapper. This is not a verified compatible layout, and no selector-3 prototype is prepared or activated.

**Unknown:** Whether indirect callers supply other copy sources. The two direct call sites do not prove an exhaustive runtime call graph.

## Next

The selector and two direct copy sources are now traced statically. Next, identify a music-only image allocation/binding hook and its lifetime, with an independent rollback path, before preparing a native modification. The existing copy routine alone does not load the approved picture. Keep shared XMB background resources unchanged and continue to distinguish timed movement from audio response.
