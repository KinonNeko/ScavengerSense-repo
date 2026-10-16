# Optional screenshot companion — 2026-10-16 inspection

Requested: an optional, separately installable screenshot plugin, useful for
showcase images and debugging. Core remains **Scavenger Sense 0.10.1 beta**;
the companion is **Scavenger Sense Capture 0.1.0 beta**, a preview. No public
core version/tag is replaced. Source branch: `codex/optional-screenshot-addon`.

## Implementation and installation contract

- Separate `ScavengerSenseCapture.dll`, built from plugin/capture. The core DLL
  has no import/dependency on it and none of plugin/src's production files were
  changed. The companion resolves the existing framework HUD registration API
  and adds Scavenger Sense/Capture to the menu. It checks the host ImGui ABI.
- Optional extras offers Screenshot capture, unchecked by default in both
  installer languages. Also provide a six-file standalone MO2 package. No ESP,
  new game, BodySlide, animation or other asset generation is required.
- Default Ctrl+Shift+F10; configurable Windows VK key/modifiers/output folder.
  A menu action queues capture until menu input is released, with a 30-second
  timeout. One in-flight shot is permitted. Keys are not swallowed, so a shared
  Print Screen/Steam/CS binding may independently save another image.
- Chain the current swap-chain Present and Present1 COM entries, preserving the
  previous function, arguments and return value. Capture the back buffer before
  the chained call, including UI already drawn. Skip DXGI_PRESENT_TEST and
  unrelated swap chains. Do not patch engine instructions or assume that an
  ImGui draw-list callback is the final composited game image.
- Queue GPU copy/resolve on the rendering thread. Poll without a blocking wait
  on later presents; map padded rows into CPU-owned RGBA. Encode opaque SDR PNG
  on a threadpool worker using WIC. A temporary file is renamed only after
  successful encoding; do not replace existing images. Bound image allocation
  and use a five-second GPU timeout. Report failures in the menu and log.
- Default output is Screenshots/ScavengerSense next to SkyrimSE.exe. In this
  instance: E:/iniRePather-Skyrim/StockGame/Screenshots/ScavengerSense. Files are
  outside Data/MO2 overwrite. Capture does not add supersampling or free camera.
- Ship only an example INI. Saving creates a personal ScavengerSenseCapture.ini;
  reinstalling the package cannot overwrite it. Capture settings are separate
  from core presets. Discover UTF-8 companion translation packs; auto language
  reads core setup fragments and then its personal INI at startup/reload.

## Evidence

`tools/build.ps1 -Test` builds both Release DLLs and passes **eight native suites**.
The new screenshot suite passes **28 checks** using a hidden test window and a
real D3D11 WARP device. Its own ImGui implementation is linked only into this
isolated test executable, never either production DLL.

- An early snapshot contains only the blue background. After two overlapping
  ImGui foreground rectangles are drawn, the chained Present snapshot contains
  the blue background, green first layer and red second layer at exact pixels.
- The background-written PNG is decoded through WIC and compared byte-for-byte
  with the expected opaque pixel buffer, including a Unicode output directory.
- Verify no overwrite, unique names, VK/UTF-8 INI round trip, bounded invalid
  settings, failed-save preservation, repeat hook installation, test presents,
  unrelated swap chains and restoring the original hook.
- Resize to 97x73 tests staging row pitch and the final pixel. Test BGRA channel
  conversion, sRGB bytes, four-sample MSAA resolve and explicit rejection of
  floating-point HDR and 10-bit buffers. Present1 is exercised on this system.
- The seven prior core/ABI/runtime suites pass, including the existing 47
  feature and 803 display assertions and synthetic address-library probes.
- Companion PE: x64, SKSE metadata 0.1.0.0, release CRT, request export; all
  **14** imported framework functions exist in the updated local host. Core
  metadata remains 0.10.1.0; it has no companion import. Host ImGui 1.90.8
  docking sizes still match the compiler (IO 14616, Style 1140, etc.).
- All 19 companion menu strings have Chinese translations; its 129 distinct
  Chinese ideographs exist in the bundled font. Core translation and menu-ID
  checks still pass. No core preset values were changed.
- FOMOD ModConfig5.0.xsd and payload validation pass. **132 routes / 300 actual
  production config loads** cover every language/preset/control path with the
  companion selected/unselected, absence of leaked DLLs, preset parity and
  personal-INI precedence. No personal capture INI is shipped or file collision
  selected. Generated installer: 39 payload folders, 76 files.
- ZIP member bytes, CRCs, DLL build bytes/metadata and ESP bytes are checked by
  tools/package_capture.py and tools/package_release.py.

## Artifacts

Standalone:
`C:\Users\Administrator\Downloads\ScavengerSenseCapture-0.1.0-beta.zip`

- 296,984 bytes, 6 members.
- SHA256 `58dfc7688c89ff860c5fe118af48e5e131c2cc805ab9a80dff205bff13a0b40c`.
- DLL SHA256 `3cd5898559436f6573b96bd0c8a7d93c2fe7715fec0e131f58fdf54a147778b9`.

Full FOMOD preview:
`C:\Users\Administrator\Downloads\ScavengerSense-0.10.1-beta-capture-preview.zip`

- 16,928,703 bytes, 76 members.
- SHA256 `a3af14408e7c47f1fa92aa4b3affc10ac87d052f3465b766759ea8b0ae23df08`.
- Core freshly rebuilt as 0.10.1, SHA256
  `37cd80c599b7b59bad4a51e955196546930217ac6f991007a33243f8316278cf`.
- ESP SHA256 unchanged:
  `b9b6c75f39a8ac58379c39d7fd8f5c029a97f6fe6bb980d0c360992e11b249bf`.

The earlier 0.10.1 ZIP is preserved. Member manifests are the adjacent
2026-10-16-capture-manifest.json and 2026-10-16-capture-fomod-manifest.json.

## Local debug deployment

Selected profile J61-BFCO has one new enabled entry, Scavenger Sense - Screenshot
Add-on, installed at E:/iniRePather-Skyrim/mods/Scavenger Sense - Screenshot Add-on.
It contains the standalone six files and local MO2 metadata. All original
profile lines are preserved; plugins.txt is unchanged. The core remains the
previously installed DLL, SHA256
`e658b9f1ed64a870188b86287da045c2d69f212d48dcbdd5febc3728bff6ba6f`.
All 35 protected core/config/framework/profile files retain their hashes.

Backup: E:/dev/ScavengerSense-backups/2026-10-16-capture-addon. It contains the
previous modlist/plugins/personal INI, protected hashes and native/build/route
evidence. Machine-readable result: 2026-10-16-capture-validation.json.
Refresh MO2 and restart SKSE before testing; disabling the separate companion
mod removes capture on the next restart without changing the main mod.

## Verification limits and manual check

No Skyrim session, interactive FOMOD install or actual game screenshot was
performed. **WARP is a graphics integration test, not in-game confirmation**.
This preview supports 8-bit SDR RGBA/BGRA buffers. HDR/10-bit formats are rejected;
D3D12/frame-generation wrappers may not expose a usable D3D11 surface. An outer
render hook may still alter/composite after our capture point. Community Shaders,
upscaling, frame generation and HDR ordering are not certified by this test.

In game: trigger a sense with tags, bars, the ring and tracks visible, press
Ctrl+Shift+F10, and inspect the saved PNG against the screen. Check queued capture
after menu close, a changed key/folder and disabling/re-enabling the addon. If
layers are absent, keep ScavengerSenseCapture.log with the buffer dimensions,
format and the exact screenshot method. There is no fallback to the original
screenshot path that silently omits overlays. No Nexus upload/comment is made.
