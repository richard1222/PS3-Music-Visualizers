# Native music UI — RR negative result

- Audience: maintainers and the console owner.
- Date: November 12, 2026.
- Scope: adding one static artwork plane to the existing music UI.
- Success signal: artwork visibly renders inside the native music player while normal playback controls work.

## What happened

**Proven:** The first UI candidate did not meet the success signal. Earth and Wave rendered, but the added artwork was absent. After removing the temporary resource mapping, an XMB reload was followed by lost network access and an owner report of recovery mode. The owner subsequently reported exiting recovery. A later read confirmed network access and the original soundvisualizer resource hash.

The project has no demonstrated native anime rendering. The recovery incident also means a successful offline resource check cannot justify another console activation by itself.

## What this tells us

- **Proven:** The tested soundvisualizer page did not produce a visible image in the observed Earth and Wave screens.
- **Inference:** The audio-player resource is a stronger target: it has a `full_screen_page` with the native `AudioPlayerPlugin::OnFullScreenPageInit` callback, plus the actual music controls and track-information pages.
- **Unknown:** Why the first image stayed invisible, which resource state caused recovery, whether the new page accepts a visible child plane, and whether animation callbacks run there.
- **Unknown:** Audio response. A timed zoom/fade definition is not an audio-driven visualizer.

## Evidence and conditions

- Console environment: Evilnat 4.93, webMAN MOD 1.47.50d.
- The static soundvisualizer candidate was supplied through a temporary HDD mapping. Firmware files were not overwritten.
- Private screen captures showed original Earth and Wave with no artwork. Screenshots, console receipts, firmware and artwork remain private.
- The first candidate's compiled resource passed independent extraction before testing; that did not establish runtime safety or visibility.
- After recovery, the original soundvisualizer resource matched SHA-256 `3ea814217250eba2dd55fee9e4afddd12607c7a26452cc756ab2290295b52e3b`.
- The new, **offline-only** audio-player candidate is 2,308,672 bytes, SHA-256 `358c985bfdbf556e0b3f801d6fc48348e00340f515a43bfd8e7c69aa7d1fbbbc`.
- Independent extraction of that candidate preserved the existing assets byte for byte and the existing UI definitions semantically. It has not been uploaded or activated.
- Host suite: 51 tests pass, including explicit resource-profile selection, native callback validation and preservation of populated pages and controls. The roster remains seven characters.

## Next discriminating work

Inspect native full-screen page initialization, visibility, stacking and resource lifecycle offline before selecting a new console test. The private helper's old static activation and XMB reload actions now refuse execution before any network request. Keep the restored original UI active.

The builder accepts an explicit offline audio-player profile:

```text
python tools/prepare_music_plane.py OWNER_AUDIOPLAYER_DUMP.xml OWNER_ART.gim NEW_PROBE.xml --target audioplayer
```

This produces research XML only. It is not an installation command, a tested console resource or evidence of audio response.
