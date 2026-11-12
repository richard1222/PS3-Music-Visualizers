# Seven selected girls — RR native gallery result

- Date: November 12, 2026.
- Audience: owner and contributors.
- Success condition: all four selected pictures appear in the existing PS3 music player, retain their proportions, and transition while native Wave visuals remain visible.

## Current truth

**Proven:** All four pictures now appear on the console: Miku, Rin/Saber, Nami/Robin and Rias/Akeno. The owner confirmed that every picture was present. Captures show Miku, Nami/Robin, the Rias/Akeno-to-Miku transition and Rin/Saber with native Wave effects and track information.

**Proven:** The owner accepted the corrected Rin/Saber framing after the oversized and undersized attempts. That framing now applies to every picture. A centered 1824-by-1026 plane occupies 95 percent of each dimension of the tested 1920-by-1080 output. Full source images, including portrait letterboxing, are retained without stretching.

**Unknown:** Continuous looping, long-session stability, reboot persistence and artwork response to audio. The authored native fade sequence lasts 320 seconds and ends on Miku. Its transitions are timed; visible Wave movement does not prove that artwork follows musical beats.

## What changed

The image plane cloned from the stock UI inherited native size overrides. Changing its width alone did not fix the oversized image. Clearing those overrides on the new plane exposed the explicit pixel dimensions. The first explicit size was too small; the near-full-screen correction was then observed and accepted. Stock controls and their layout overrides were preserved.

The four-picture resource initially displayed Miku but later planes were blank. Equal base opacity, delayed initialization and a single large composite did not produce the requested result. A direct image-byte substitution in the working Miku resource proved the Fate texture could display. The four-picture test succeeded after reducing each texture from 512-by-288 to 256-by-144 pixels.

**Inference:** Memory/resource limits explain the larger variants' blank images. The resolution reduction is observed evidence; no allocator failure or exact memory ceiling was measured. The builder now rejects total artwork above the tested 600,000-byte envelope rather than assuming larger resources are safe.

## Evidence

- Console: Evilnat 4.93, webMAN MOD 1.47.50d, Cobra enabled.
- Working resource: 2,309,928 bytes; SHA-256 `835d9e6745f936953b1bbbfefcf86e76d5da57c3bd5b2a23dc355835b0d27fc9`.
- Artwork: four GIM textures, each 147,584 bytes, total 590,336 bytes.
- Independent extraction retained all source texture bytes, native controls, localized text and original page callbacks.
- Private captures: `fate-fit-open-047.png`, `small-open-049.png`, `small-phase-050.png`, `small-phase-051.png`, `small-phase-052.png`.
- Owner statements confirmed every picture appeared and accepted the corrected fit.
- Host suite: 56 tests pass, including preservation of stock layout overrides, clearing them only on the new artwork plane, finite fade targets and rejection of the larger unsuccessful texture budget.
- No firmware file overwrite, XMB reload or forced Sony module unload was used. Mappings were changed only after read-only checks established that the native music modules were unloaded. Earlier working resources remain preserved.

Screenshots and compiled resources contain third-party artwork or Sony assets and stay private. Public source contains the builders and synthetic tests.

## Operator scope

`tools/prepare_music_gallery.py` prepares offline XML using four reviewed GIM inputs in this order: Miku, Rin/Saber, Nami/Robin, Rias/Akeno. It refuses another image count, oversized artwork, an occupied full-screen page, unexpected native callbacks, duplicate probe labels and an existing output. It does not activate a resource on the console.

```text
python tools/prepare_music_gallery.py OWNER_AUDIOPLAYER_DUMP.xml NEW_GALLERY.xml MIKU.gim FATE.gim ONE_PIECE.gim DXD.gim
```

The present fit is calibrated for 1080p output. Source textures are low resolution and look soft when enlarged. Higher-quality storage within the measured working envelope and a continuous sequence remain future work; preserve this baseline before testing either.

Native layout-override fields are documented in the [PS3 RCO object format](https://www.psdevwiki.com/ps3/RCOXML_Objects). The older local Rcomage configuration calls them `unknown17` through `unknown22`; the builder clears these fields only on the added image plane.
