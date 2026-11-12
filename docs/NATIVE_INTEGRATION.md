# Native integration research

The requested target is the existing PS3 music player, using its Visual Player modes. A separate browser, photo viewer or new music app does not satisfy that requirement.

## Existing renderer paths

- Gaia loads `qgl/earth.qrc`; the creator's planet pack modifies that existing globe renderer.
- Canyon loads `qgl/canyon.qrc`; its stock resource contains presets, a normal-map texture and shader objects. The presence of unused slideshow shaders alone does not establish an available full-screen photo mode.
- Wave uses the existing `qgl/lines.qrc` resources, including music-specific overrides. Its texture and background paths still need inspection.
- The [native binding study](NATIVE_BINDING_STUDY.md) identifies local instruction references to monthly background textures and Canyon's normal map. It has not established a usable HD artwork plane.
- The [background sampling study](BACKGROUND_SAMPLING.md) identifies which bundled fragment programs contain texture reads. The music preset records variant 1, which contains no texture reads; selector-to-draw binding is still under investigation.

The local firmware files are research inputs and remain unpublished. Resource validation is bounded and does not execute embedded code.

## After the first freeze

The [failed experiment](ANIME_TEST_FAILURE.md) changed textures and camera/menu data together. Automatic selection is disabled. The [offline one-tile study](OFFLINE_TEXTURE_STUDY.md) now preserves the container layout and scene data; it has not been installed. Full-screen artwork and audio response remain separate, unmet acceptance criteria.

## Acceptance criteria

1. Full-screen female-character artwork is visible inside the built-in music player.
2. Existing track playback and Visual Player switching still work.
3. Planet options remain available and the original Earth can be restored.
4. The working Rias/Rin/Miku XMB icons and the selected theme are preserved.
5. Original resources are backed up; prototypes use reversible HDD remaps where supported.
6. Timed animation and audio-driven animation are labeled according to observed behavior.

## Sources

- [Sony Visual Player controls](https://manuals.playstation.net/document/en/ps3/current/music/panel.html)
- [DeViL303 Gaia research and planet download](https://www.psx-place.com/threads/research-modifying-the-gaia-visualization-custom_render_plugin-earth-qrc.27756/page-33)
- [webMAN commands and remapping](https://github.com/aldostools/webMAN-MOD/wiki/Web-Commands)
- [Q-Games on Gaia](https://www.q-games.com/en/2007-12-18-2/)
