![Platform](https://img.shields.io/badge/Platform-PlayStation_3-003791)
![Development](https://img.shields.io/badge/Development-0.1.0_prototype-orange)
![License](https://img.shields.io/badge/Code-MIT-blue)
![Credit](https://img.shields.io/badge/Credit-Richard1222_%2F_Saber-brightgreen)

# Saber PS3 Music Visualizers

Planet visualizer setup and an experimental anime artwork viewer for a PS3 with webMAN MOD. **Credit to Richard1222 / Saber.**

The planet options use the PS3's native Gaia/Earth visualizer. The anime viewer is a separate ES5 browser prototype with full-screen artwork and timed particles. **It does not capture XMB audio, respond to beats, or replace a native visualizer plugin.** Music continuity and browser behavior must be checked on hardware before calling that workflow working.

Project structure, status reporting, backups and validation take their reference from [Saber PSN Unlock Tool](https://github.com/richard1222/PS3-Trophy-Unlocker-2026). This project does not access trophies or reuse its award workflows.

## Current development

Version 0.1.0 is source development, not a finished PKG release. Read [current status](docs/STATUS.md).

| Component | Current evidence | Limit |
| --- | --- | --- |
| Creator's planet pack | 14 QRC files downloaded and checked; one optional original Earth slot | Artwork and firmware resources are not distributed here |
| HDD installation | 15 planet slots and initial three-image web viewer independently read back on one owner's console | Broad firmware compatibility is untested |
| Planet selection | webMAN accepted a fixed Jupiter command with a malformed HTTP status line | Native rendering is not yet confirmed |
| Anime viewer | ES5 source and online artwork download tool | Timed animation; no audio analysis; hardware test pending |
| Requested gallery | Female characters from Fate/stay night, One Piece, Hatsune Miku and High School DxD | Gallery expansion is in progress |

## Features

- QRC size, compression and table-bound checks before upload.
- Creator archive pinned by SHA-256 and an explicit file allowlist.
- HDD-only installer with dry run by default, staging readbacks and preservation of existing different files.
- Updates require a previous receipt matching the existing file and preserve its backup.
- Separate native planet resources and browser artwork; no theme replacement.
- Download credits kept beside local artwork. Third-party images are excluded from Git.

## Quick start

Use Python 3.12 or later. Obtain the planet ZIP from [DeViL303's original post](https://www.psx-place.com/threads/research-modifying-the-gaia-visualization-custom_render_plugin-earth-qrc.27756/page-33). Its inspected SHA-256 is recorded in `tools/prepare_planets.py`.

```text
python tools/prepare_planets.py downloads/planets.zip local/planets
python tools/fetch_art.py source/art
python tools/install_hdd.py --planets local/planets --web source
```

The last command prints a plan without using the network. To install, add `--host YOUR_PS3_LAN_IP --apply --receipt receipts/install.json`. Use your own console address. The PS3 must be on XMB with webMAN FTP available.

`--original path/to/your/earth.qrc` adds an original-Earth slot when preparing the planet pack. Slot numbers then shift by one; consult the generated manifest before selecting a slot. `--activate N` requests a fixed webMAN Earth resource, then attempts a separate mapping readback. A failed mapping check is reported rather than silently treated as rendering success.

Open `http://YOUR_PS3_LAN_IP/dev_hdd0/PS3-Music-Visualizers/index.html` in the PS3 browser to test the anime prototype. Choose artwork and hide the controls. The native music player's Visual Player backgrounds and this browser viewer are separate modes. [Sony's music control-panel guide](https://manuals.playstation.net/document/en/ps3/current/music/panel.html) documents the native player.

## Validation

```text
python -m unittest discover -s tests -v
node --check source/visualizer.js
```

The initial 15 host tests cover malformed resources, path escapes, changed files and failed staging. Host tests do not establish sound playback, frame rate or console rendering. GitHub Actions runs source checks without accessing a PS3.

## Repository layout

```text
source/      ES5 browser viewer; local artwork is ignored
tools/       QRC inspection, pack preparation, downloads and HDD installation
tests/       Synthetic resource and transport regression checks
docs/        Status, provenance and next steps
.github/     Source-only checks
```

## Credits and license

Original project code: Richard1222 / Saber, [MIT](LICENSE). Planet pack: DeViL303; planet texture source: [Solar System Scope](https://www.solarsystemscope.com/textures/). webMAN MOD: aldostools and contributors. Anime images retain their original creators' rights; see [notices and sources](THIRD_PARTY_NOTICES.md). No third-party artwork or Sony firmware resources are licensed by this repository's MIT license.
