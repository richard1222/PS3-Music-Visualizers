![Platform](https://img.shields.io/badge/Platform-PlayStation_3-003791)
![Target](https://img.shields.io/badge/Target-Built--in_Visual_Player-blue)
![Development](https://img.shields.io/badge/Development-Native_integration-orange)
![License](https://img.shields.io/badge/Code-MIT-blue)

# Saber PS3 Music Visualizers

Custom resources for the **built-in PS3 music player's Visual Player**. Credit to **Richard1222 / Saber**.

The goal is full-screen female-character artwork from **Fate/stay night, One Piece, Hatsune Miku and High School DxD**, plus planet options, inside the existing PS3 music player. **Four pictures covering the selected seven girls now display and transition over native Wave visuals.** Framing preserves the full pictures. The current timed sequence lasts 320 seconds; continuous looping, higher image quality and artwork beat response remain unfinished. See the [gallery hardware result](docs/NATIVE_GALLERY_RESULT.md).

The project's branding, documentation and verification style reference [Saber PSN Unlock Tool](https://github.com/richard1222/PS3-Trophy-Unlocker-2026). This project does not access trophies.

## Current development

See [current status](docs/STATUS.md) and [native integration research](docs/NATIVE_INTEGRATION.md).

| Workstream | Evidence | Limit |
| --- | --- | --- |
| Planet pack | 14 creator QRCs validated; 15 HDD slots including the owner's original Earth independently read back; Jupiter observed in native Gaia | Other planets have not each been observed |
| Resource selection | Original Earth selection accepted after recovery from an anime-test freeze | Automatic selection is disabled; an XMB status string cannot establish that the renderer is unloaded |
| Anime artwork | Four reviewed images covering exactly Rin, Saber, Nami, Robin, Miku, Rias and Akeno | Selected artwork is ready; native rotation remains unfinished |
| Native anime integration | All four selected pictures observed in native music playback; owner confirmed every picture; corrected framing accepted | Four 256×144 textures; finite 320-second timed fades; long-session stability, continuous looping and artwork beat response unverified |
| Previous failures | Broad Gaia prototype froze; the first soundvisualizer UI page showed no artwork; a subsequent reload was followed by owner-reported recovery mode | Failures remain documented. Successful audio-player test used unloaded-module checks and no XMB reload. See [hardware result](docs/NATIVE_MIKU_SUCCESS.md) |
| Host checks | 56 tests pass | Tests do not establish console rendering or music playback |

## Scope correction

The initial commit included an ES5 browser-viewer prototype. The owner clarified that the artwork must fit the **built-in PS3 visualizer**, so that source and its setup instructions have been removed from the current branch. The history is preserved; it is not the current implementation or installation route.

## Quick start: native planet resources

Python 3.12 or later and webMAN MOD are required. Obtain the planet ZIP from [DeViL303's creator post](https://www.psx-place.com/threads/research-modifying-the-gaia-visualization-custom_render_plugin-earth-qrc.27756/page-33). The inspected archive SHA-256 is pinned in the preparation tool.

```text
python tools/prepare_planets.py downloads/planets.zip local/planets
python tools/install_hdd.py --planets local/planets
```

The second command prints a plan without network access. To install, add `--host YOUR_PS3_LAN_IP --apply --receipt receipts/install.json`. Use your own console address and remain on XMB. Files go only into the HDD resource slots; the tool does not overwrite firmware, change the theme or touch trophies.

Optionally supply `--original path/to/your/earth.qrc` when preparing resources. It adds a first slot for the original Earth, shifting the downloaded planet slot numbers. Consult the generated manifest. **Automatic selection (`--activate`) is disabled after a console freeze.** The installer stages files only. Do not change a resource mapping while music or its Visual Player is active; `(XMB)` is also reported during playback and cannot establish that switching is safe. See the [failed experiment](docs/ANIME_TEST_FAILURE.md).

For updates, `--previous-receipt receipts/previous.json` permits replacement only when the existing file matches that prior receipt, with a preserved backup. Existing different files are otherwise refused.

## Artwork preparation for development

```text
python tools/fetch_art.py local/art-original
```

On Windows, `tools/fit_art.ps1 -InputFolder FULL_INPUT_PATH -OutputFolder FULL_NEW_OUTPUT_PATH` creates 1280x720 copies while retaining the complete image and source credits. These tools prepare research inputs. **They do not inject artwork into the native visualizer.** Downloaded artwork is excluded from Git.

## Validation

For local resource research, `python tools/audit_native.py BASE.qrc CANDIDATE.qrc --report NEW_REPORT.json` compares resource changes, JPEG frame profiles, camera/menu changes and unpacked size. It makes no console connection. See the [offline texture study](docs/OFFLINE_TEXTURE_STUDY.md) for the one-tile comparison and its limits.

```text
python -m unittest discover -s tests -v
```

Tests cover malformed resources, table bounds, path escapes, changed files, failed staging, backed-up updates and rename recovery. GitHub Actions runs host checks without connecting to a PS3.

## Layout

```text
tools/       Native resource inspection, preparation and HDD installation
tests/       Synthetic resource and transport checks
docs/        Current truth, sources and native integration research
.github/     Source-only checks
```

## Credits and license

Original project code: Richard1222 / Saber, [MIT](LICENSE). Planet pack: DeViL303; texture source: [Solar System Scope](https://www.solarsystemscope.com/textures/). Integration: webMAN MOD by aldostools and contributors. See [third-party notices](THIRD_PARTY_NOTICES.md).

Sony firmware, downloaded planet resources and third-party anime artwork are not included or relicensed by this repository. The planet pack contains individual globe replacements, not a simultaneous solar-system simulation.
