# RR — Native music UI image and motion probe

> Latest result: the audio-player full-screen profile now displays static Miku artwork on hardware, with native controls. See [native Miku result](NATIVE_MIKU_SUCCESS.md). The motion candidate described below remains untested.

> Historical offline-build record. A later **static** variant was tested and showed no artwork. An XMB reload after removing its mapping was followed by owner-reported recovery mode. The motion variant described below remains untested. See the superseding [UI test result](NATIVE_UI_TEST_RESULT.md); the next target is an offline audio-player full-screen candidate. Further console activation is suspended.

- Audience: owner and contributors
- Date: November 12, 2026
- Scope: local resource authoring for the existing sound visualizer plugin
- Success condition: preserve the native UI and embed the approved picture with a valid animation reference; actual playback visibility and motion are separate hardware acceptance conditions.

## Current truth

**Proven:** A local candidate adds one image plane to the existing empty `page_soundvisualizer0` in `soundvisualizer_plugin.rco`. It embeds the approved Miku artwork as a 512×288 RGBA image and adds a finite 24-second zoom/fade definition. The source targets the existing music UI, not a browser or separate player.

**Proven:** Independent extraction of the final candidate retained the intended objects, animation references and every image byte. Removing the two added image/plane nodes and the added animation tree restores the original UI structure semantically. Original pages, native event callbacks, controls and localized text remain intact. Image storage was changed to uncompressed.

**Unknown:** Whether page 0 is visible during full-screen playback, its stacking order against the renderer, its coordinate scale, its memory impact and whether its load event starts the animation. No candidate was uploaded, remapped or activated on a console.

**Not established:** Continuous rotation between characters or audio response. The motion definition is finite and timed. The original Wave/Canyon/Gaia renderers have not been replaced.

## In plain language

This is an authored native-resource prototype, rather than another viewer. The picture gently grows from 97% to 100% scale and back while its opacity changes, leaving the existing renderer available behind it if the page is drawn. The local checks prove that the resource contains those instructions; they do not prove the PS3 displays or executes them.

## Evidence

| Item | Result |
| --- | --- |
| Original sound visualizer RCO | 8,780 bytes |
| Final local motion candidate | 612,088 bytes |
| Candidate SHA-256 | `877a87f890df01e7e4e96e7284a9a4ba163715319d3a0bde260ca2dd8a7dce40` |
| Added image resources / planes / animations | 1 / 1 / 1 |
| Timed animation | 12 seconds to grow/fade, 12 seconds to return |
| Recursive events or infinite loop | none |
| Console uploads or firmware writes | none |
| Host suite | 47 tests pass |

The native UI resource was acquired read-only. Its boot-logo sibling resource was inspected but not modified. The image's source credit stays with the private artwork files. Sony resources, tool binaries, compiled candidates and artwork are excluded from the public source update.

No new console screenshot exists: this candidate remains local. The owner's prior recovery screenshot does not establish native artwork rendering.

## Negative results and repair

The first compact XML build silently omitted the appended image. Matching the dumped XML's declaration and indentation retained the entry; a regression check now covers that formatting.

Further extraction found incorrect image payloads despite a successful compiler exit code. Resource compression changes alone did not satisfy the byte comparison. For the private uncompressed candidate, the image-data section was rebuilt from the exact source GIMs within the declared allocation. The section length matched, all bytes outside it remained unchanged, and independent extraction then recovered all eight images byte-identically.

An initial static control candidate also passed after that repair. The moving candidate passed the same checks. Intermediate failed files are retained as evidence and are not installable releases. Compiler exit status alone is insufficient validation.

## Source and tests

`tools/prepare_music_plane.py` prepares XML only. Its optional `--motion` flag adds the finite timed definition. It refuses another plugin, an occupied target page, missing native plane templates and duplicate probe labels. It preserves the original page events and copies no controls into the new plane. Tests use synthetic XML and placeholder headers.

Native animation format reference: [PS3 RCOXML animations](https://www.psdevwiki.com/ps3/RCOXML_Animations). Local attribute names follow the inspected Rcomage configuration used for the build. This resource work does not execute Sony module code on the PC.

## Next

Trace the sound visualizer's page lifecycle and establish a reversible test method before hardware use. Verify static visibility before movement, then page closure and existing controls. Connect audio-driven changes only after their data source and update lifetime are established. Use only the selected seven characters for later artwork rotation; the one-image probe is a controlled integration input.
