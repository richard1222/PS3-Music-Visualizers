# Native Miku background — RR hardware result

- Audience: console owner and contributors.
- Date: November 12, 2026.
- Scope: one static artwork plane inside the existing full-screen music UI.
- Success condition: Miku visibly renders in the native music player, with its native control panel available.

## Current result

**Proven:** The audio-player full-screen candidate displayed Miku on two separate opens of the selected music track. The owner independently confirmed seeing Miku. The native control panel subsequently opened over the image and closed normally, leaving the image and native track information visible.

The static-image integration has hardware evidence. Animation, beat response and rotation through the seven selected characters remain unfinished.

## Evidence

| Class | Claim | Evidence |
| --- | --- | --- |
| Proven | Full-screen native artwork | Private captures `audio-ui-open-022.png` and `audio-ui-reopen-025.png`; owner confirmation |
| Proven | Native control panel opens | Private capture `audio-ui-panel-026.png` |
| Proven | Control panel closes while artwork stays visible | Private capture `audio-ui-panel-closed-027.png` |
| Proven | Audio-player module was unloaded before remapping | Read-only native module inventory, repeated in the test helper immediately before mapping |
| Proven | Original backup and staged candidate matched local hashes | Independent HDD readbacks before mapping |
| Inference | Earlier target page was the wrong route | Static visualizer code references page 1, while the failed probe targeted page 0; dynamic page construction was not excluded |
| Unknown | Long-session stability and persistence after reboot | Not tested |
| Unknown | Animation, beat response and roster rotation | Not tested in this working resource |

The captures contain third-party artwork and stay private. The public repository contains the builder, synthetic tests and this account of the result, rather than artwork or Sony resources.

## How it works

The working plane is attached to `full_screen_page` in the existing `audioplayer_plugin.rco`. The native track information and playback controls remain available. It uses a temporary HDD resource mapping; firmware bytes were not overwritten. The artwork input is 512 by 288 pixels, so it looks soft at full-screen resolution.

Read-only module inspection identified the native full-screen callback. Its handler at module address `0x9dc` stores the page pointer and returns. The page is referenced by native setup code around `0xb654`; this supports targeting the audio-player UI rather than the separate soundvisualizer resource.

The test used Evilnat 4.93 and webMAN MOD 1.47.50d with Cobra enabled. A prior status check reported Cobra disabled and the module API unavailable; testing resumed only after the owner restored Cobra and both checks succeeded.

- Working resource: 2,308,672 bytes.
- SHA-256: `358c985bfdbf556e0b3f801d6fc48348e00340f515a43bfd8e7c69aa7d1fbbbc`.
- Existing assets were recovered byte-identically by independent extraction before testing.
- Host suite remains 51 passing tests. No source change requiring a new test run occurred after that result.
- No XMB reload or forced native-module unload was used for this successful test.

The [previous failed test and recovery incident](NATIVE_UI_TEST_RESULT.md) remain part of the record. One intermediate control-panel attempt found XMB instead of the music screen; the reason for that intervening return is unknown. The later, directly observed control-panel check passed. This does not establish unattended stability.

## Next

Preserve the working static candidate as the baseline. Prepare a separate finite movement candidate and compare it offline before any test. Change mappings only after independently confirming that the audio-player module is unloaded. Higher-resolution artwork, movement, beat response, character rotation and reboot persistence each need their own evidence.

The supported resource profile is `--target audioplayer` in `tools/prepare_music_plane.py`. That tool prepares XML only; it is not a public console activator.

webMAN's [reload implementation](https://github.com/aldostools/webMAN-MOD/blob/master/include/init/vsh.h) can take logout or application-launch paths. Its old private reload shortcut stays disabled following the earlier incident. Public [PAF interface declarations](https://github.com/aldostools/webMAN-MOD/blob/master/vsh/paf.h) support the widget lookup and page activation identification used in the static trace.
