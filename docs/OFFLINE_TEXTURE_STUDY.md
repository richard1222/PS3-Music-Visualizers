# RR — Offline texture study after recovery

- Audience: owner and contributors
- Date: November 12, 2026
- Success condition: prepare a one-texture comparison that preserves the native container layout and every camera, menu and code resource. Hardware rendering is a separate acceptance condition.

## Current truth

**Proven:** A local candidate changes one existing JPEG tile in the Jupiter resource already observed in native Gaia. Both unpack to **3,991,408 bytes**; table locations and sizes match. No resources were added or removed. Cameras, menus and embedded code are byte-identical. This candidate has **not** been uploaded or selected on a console.

**Proven:** The failed prototype's 24 JPEG headers match its creator base: baseline, 8-bit, 512×512, three components with matching sampling and quantization-table selectors. This checks headers only; it does not establish successful decoding on the PS3.

**Proven:** The working Jupiter resource is larger unpacked than the failed prototype (3,343,552 bytes). Absolute unpacked size alone is therefore insufficient to explain this failure. The failed build still grew relative to its own base and changed cameras and menus together.

**Unknown:** The freeze cause, the renderer's allocation rules, a supported flat full-screen artwork route and actual audio response for anime artwork.

## In plain language

The next comparison is deliberately smaller: change one picture tile and preserve the scene that already worked. This can separate texture/container problems from camera changes. It does not yet create a full-screen anime visualizer or prove a safe hardware transition.

The failed test was moved, with a matching SHA-256 and verified readback, outside webMAN's numbered selection directory. It remains preserved for analysis. No new test was activated.

## Evidence

| Input or result | Value |
| --- | --- |
| Working Jupiter base SHA-256 | `930acc556f93dd112dd20a250e720eb4243b6f93bdbff69c3f7c1ac31f342e47` |
| Offline candidate SHA-256 | `4f6ea8530dfabf97c55d6500fc2f0ccc61ef8ff36721ab6bdc19e58105c69678` |
| Changed resource | `earth/flashrom/clouds/0/1/000_000.jpg` |
| Raw-size growth | 0 bytes |
| Camera/menu/code changes | 0 / 0 / 0 |
| Local host tests | 32 pass |
| Native anime rendering | Unverified |

The private comparison reports retain individual file metadata. Assets, console addresses and console screenshots are excluded from publication. No new console-rendering screenshot exists for this offline candidate; the earlier recovery screenshot does not prove artwork rendering.

## Tools and limits

`tools/native_textures.py` inspects JPEG frame headers and offers a fixed-size replacement function. It refuses larger images, changed frame profiles, non-JPEG targets, overlapping tables and shared texture allocations. It preserves the old allocation tail while shortening the declared JPEG length when necessary. **Acceptance by this function is not PS3 compatibility certification.**

`tools/audit_native.py` compares local resources and writes metadata only. It never connects to a console. Host checks use synthetic containers and JPEG headers; they are not a substitute for an image decoder or hardware observation.

## Research direction and next step

[Canyon reconstruction research](https://github.com/vortigauntlet/Spotify-PS3/blob/main/docs/canyon-visualizer.md) reports an audio-callback route to native Canyon, with Gaia started without those callbacks. Its author's rendering implementation is a separate application and explicitly unverified on a TV; we use its static findings as leads, not as an implementation of this project's requested native modification.

[DeViL303's texture experiments](https://www.psx-place.com/threads/research-modifying-the-gaia-visualization-custom_render_plugin-earth-qrc.27756/page-33) document globe layers, not a flat image plane. The distinction matters: moving artwork around a sphere does not meet the full-screen request, and motion by itself does not establish music response.

Next, establish the native texture-binding route for a flat plane and verify safe renderer shutdown before any resource selection. Keep the failed build inactive and preserve automatic-selection rejection. No further hardware experiment was performed in this study.
