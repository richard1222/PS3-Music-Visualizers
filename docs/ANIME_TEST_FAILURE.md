# RR — Negative result: first native anime prototype

- Audience: owner and contributors
- Date: November 12, 2026
- Scope: a local HDD resource variant for the existing Gaia music visualizer
- Hypothesis: replace cloud textures and camera paths to show gently moving artwork.
- Success signal: visible artwork inside native Visual Player with continuing music and working controls.

## What happened

**Proven:** The owner reported a frozen console after the prototype was selected while Gaia was already loaded. Subsequent HTTP and FTP operations timed out. No screenshot of native anime rendering was obtained. The experiment failed its success condition.

The owner restarted the console. A bounded status request and screenshot then succeeded. The original Earth HDD slot matched its saved SHA-256, and webMAN accepted selection of that original slot. **Native Earth rendering after recovery has not been retested.**

## What this tells us

- **Proven:** The previous `(XMB)` guard was insufficient: that string also appears during native music playback.
- **Proven:** The test changed several variables together: 24 texture slots, camera paths and menu animation values. It cannot isolate a cause.
- **Proven:** The generated camera paths omitted the original `closed`, `loop` and `close_dt` trailer settings and used different line endings. These differences are locally observable.
- **Inference:** Changing mappings while the renderer was active could have mixed resource state. Malformed camera data, container layout and renderer memory limits are also candidates.
- **Unknown:** The actual freeze cause. None of these candidates is a proven diagnosis.

## Evidence and reproduction conditions

- Environment: webMAN MOD 1.47.50d; native Gaia had already displayed Jupiter.
- Failed prototype SHA-256: `cb9043126e1070f3cd8bec1196593cd2e8cb017b0bb84ddec13c0e21e7c34d48`.
- Unpacked sizes: creator base 2,726,896 bytes; prototype 3,343,552 bytes. The valid container checks do not establish a safe renderer allocation.
- Archive comparison found 65 resources with changed bytes; embedded code and shaders remained unchanged.
- An orchestration error allowed a dependent controller request before the installer process completed. Neither operation should be treated as a confirmed transition.
- Recovery evidence retained privately: original-selection receipt and XMB screenshot. They include local console information and are not published.
- No firmware visualizer file was overwritten; the test used an HDD remap.
- No screenshot of the freeze exists. The owner's report and request timeouts establish the observed failure, not its internal cause.

## Changes and next discriminating test

Automatic selection is now rejected before any network access. A regression test checks that neither HTTP nor FTP is called for an activation request. The offline QRC editor has synthetic tests; it is not a console-compatible artwork release.

Keep the failed prototype inactive. Inspect renderer constraints locally and preserve camera/menu bytes in the next offline comparison. Establish safe selection conditions before any further hardware experiment; then vary one factor at a time. No replacement viewer or audio-response claim is introduced.
