# Scavenger Sense source workspace

Read README.md, CLAUDE.md and the latest report under reports/ before editing.
This is the source repository. The MO2 mods tree is a separate installed copy.

- Build and run offline checks with `./tools/build.ps1 -Test`; keep plugin/build.
- CommonLib is pinned to a released commit. cimgui must stay ABI-compatible
  with the runtime Menu Framework; do not update it blindly to upstream HEAD.
  Use ImGuiCompat.h with extern/imgui-abi's pinned docking headers. cimgui's
  generated C structs have different layouts and must not be re-enabled.
  Run check_framework_abi.py against a changed framework DLL; symbol presence
  alone does not establish ABI compatibility.
- Settings need defaults, loader, clamps, writer, menu and translations.
- Main-thread code owns game objects. Render code consumes snapshots and
  creates D3D resources; do not dereference TESForm from rendering.
- Validate translations, ImGui IDs, PE metadata/imports, FOMOD and installer
  routes. XSD validation was also performed for the 0.10 installer.
  Use `check_installer_routes.py --probe plugin/build/ConfigProbe.exe` to check
  the production loader, preset parity and personal-INI precedence in temporary
  Data trees; XML-only checks do not establish the effective configuration.
- Deployment detects the selected MO2 profile. Check the current modlist and
  plugins first; preserve personal INIs, rule customisations and assigned titles.
- Do not call compilation, synthetic executable probes or XML checks in-game
  verification. Record actual user/game results separately with a date.
- Keep Main.cpp, CMakeLists.txt and fomod/info.xml version numbers consistent.
