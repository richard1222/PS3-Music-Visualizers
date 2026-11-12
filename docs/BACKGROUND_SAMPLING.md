# RR — Background shader sampling

- Audience: owner and contributors
- Date: November 12, 2026
- Success condition: determine whether the existing music background samples picture textures before attempting replacements.

## Current truth

**Proven:** The owner's `override/music_1/BACKGROUND.mnu` contains `COLOUR SHADER:int:1`. Offline inspection of the three background fragment programs found four texture reads in variant 0, none in variant 1 and none in variant 2.

**Inference:** If the selector binds the corresponding numbered variant as expected, replacing monthly background images alone will not display them through the configured music background. The complete selector-to-draw binding has not yet been independently established.

**Proven:** A bundled copy fragment program contains one texture read, from sampler 0 using input texture coordinates. This makes it a concrete lead for investigating a flat image, but does not establish which texture is bound, which screen pass uses it or whether it can be selected for music.

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

## Next

Trace the background selector and copy pass through their texture bindings and draw calls. Keep shared XMB background resources unchanged while determining whether a music-only path exists. Continue to distinguish timed movement from audio response.
