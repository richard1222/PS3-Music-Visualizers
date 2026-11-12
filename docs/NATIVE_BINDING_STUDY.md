# RR — Native texture-binding study

- Audience: owner and contributors
- Date: November 12, 2026
- Scope: private local inspection of three modules read from the owner's console; no renderer changes or controller input
- Success condition: identify a texture-loading route in the existing player that could support flat artwork; distinguish a static reference from an observed runtime binding.

## Current truth

**Proven:** The owner's `custom_render_plugin`, `qgl_canyon_app` and `qgl_gaia_app` were read without console writes. Their locally rebuilt ELF segments passed bounds checks and the unpacker reported no errors. Modules and decoded instructions remain private.

**Proven:** Local instruction inspection found address-construction sequences referencing Wave's monthly background paths and its background shaders in `custom_render_plugin`. The monthly day/night DDS inputs in the owner's `lines.qrc` are **64×32 pixels with five mip levels**, not full-screen HD artwork inputs.

**Proven:** Canyon's app contains an address-construction sequence referencing `canyon/textures/DDS/canyon_normalmap.dds`. The inspected app does not contain the literal slideshow shader names found among the QRC's bundled assets.

**Inference:** Replacing that normal map with a character picture would affect terrain shading rather than establish a flat photo display. The missing slideshow strings weaken the idea that those bundled shaders expose a ready-to-use photo mode, but do not prove that no dynamically constructed or indirect route exists.

**Unknown:** A usable flat-image binding, runtime dimensions and allocations for a replacement background, and safe renderer shutdown before remapping. No anime prototype was activated in this study.

## In plain language

The globe is not the only texture route, but the alternatives currently identified serve small background gradients or terrain lighting. Finding a shader file in an archive is not enough: the program must load it, bind the picture and draw it in the correct place. That route is still being traced.

## Evidence

| Module or resource | Static finding |
| --- | --- |
| `custom_render_plugin` | Monthly day path reference at module virtual address `0x5781c`; night path at `0x578f0` |
| `custom_render_plugin` | Background-shader reference sequences at `0x573b0`, `0x57404`, `0x57458`, `0x574a8`, `0x574f8` |
| `qgl_canyon_app` | Normal-map path reference at `0x9a44` |
| `qgl_gaia_app` | Camera-path reference sequences at `0x14a78`, `0x14c98`, `0x22e4c` |
| Wave monthly textures | 24 DDS resources: 12 day and 12 night; inspected examples are 64×32, five mip levels |

Addresses refer to the owner's module versions, not a portable patch or instruction to modify firmware. A bounded linear scan finds candidate address references; it does not establish executed call paths. Private reports retain contexts and file hashes. No new rendering screenshot exists because this study made no rendering changes.

The private SELF parser was obtained from [zecoxao's PS3 loader](https://github.com/zecoxao/ida_console_py_ps3/tree/master/loaders/ps3lib), source blobs `aa343e830a8ce5b3f9949549a7ae9c2fc2bf3a9b` and `db5a1442d3eb11164f7d8312bfa4101d2a650437`. It was inspected before local use and is not vendored or relicensed here. Disassembly used the already available Capstone library.

## Next

Trace how the monthly textures are sampled and what fixes their dimensions before preparing any changed Wave resource. Continue investigating native Canyon's audio-driven geometry separately from the artwork plane. The original Earth selection remains the recovery configuration; the failed prototype remains outside selectable slots.
