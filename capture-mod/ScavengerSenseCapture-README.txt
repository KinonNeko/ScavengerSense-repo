Scavenger Sense Capture 0.1.0 beta — optional screenshot companion
Author: KShakes. GPL-3.0-or-later; licence included.

Install / uninstall
-------------------
The Scavenger Sense FOMOD offers Screenshot capture on Optional extras. It is
unchecked by default. The core mod does not import or require this DLL.
Alternatively install the standalone add-on ZIP as a separate MO2 mod.
Requires SKSE64 and Address Library matching Skyrim, plus SKSE Menu Framework
with RegisterHudElement and the matching ImGui 1.90.8 docking ABI (updated local
3.14 checked). No ESP, new save or asset generation is needed.

Restart SKSE after installing/enabling/disabling. If installed as a separate
MO2 mod, uncheck that mod to remove capture support. If merged into the main
package, reinstall cleanly with capture unchecked, preserving your own rules
and artwork, or remove ONLY ScavengerSenseCapture.dll and its companion files.
Keep your personal ScavengerSenseCapture.ini if you want its settings later.

Taking screenshots
------------------
Default keyboard shortcut: Ctrl + Shift + F10. It triggers once per press.
The key does not consume input; choosing Print Screen/F12/another mod's key
may also trigger its own screenshot. This does not replace vanilla or CS capture.

Menu Framework -> Scavenger Sense -> Capture:
- Enable/disable capture, select a key/modifiers and an output folder.
- Capture after closing menu: close the framework menu within 30 seconds.
  Capture waits for keyboard/mouse focus to be released before taking a shot.
- Save capture settings to keep changes; Reload discards unsaved edits and
  reloads settings/translations. Open screenshot folder shows the output.
- Status displays the result or an error. Details go to ScavengerSenseCapture.log
  beside the other SKSE logs (normally Documents/My Games/Skyrim Special
  Edition/SKSE, possibly under OneDrive). No capture notification is drawn into
  the image itself.

Default output: Screenshots/ScavengerSense next to SkyrimSE.exe. In this author's
MO2 setup that is E:/iniRePather-Skyrim/StockGame/Screenshots/ScavengerSense.
It is NOT Data or MO2 overwrite. PNG uses the current back-buffer resolution;
it does not add supersampling or a free camera. Files use unique timestamp,
process and sequence names; existing screenshots are not replaced.

Only one shot is processed at a time. GPU pixels are read on the render thread
without a blocking wait; PNG encoding/writing runs on a background worker.
Capture occurs immediately before the hooked DXGI Present/Present1, after UI
drawn earlier in that rendering chain. Other swap chains and DXGI test-only
presentations are passed through. No engine instruction offsets are patched.

Preview support limits
----------------------
This first version supports 8-bit SDR RGBA/BGRA back buffers, with MSAA resolve.
HDR/10-bit output is explicitly rejected rather than converted incorrectly.
D3D12/frame-generation wrappers may not expose a capturable D3D11 back buffer.
Other late render hooks, Community Shaders, upscaling, HDR and frame generation
can change the surface/order. Actual in-game appearance must be checked with
your configuration; the offline WARP/ImGui test does not certify those modes.
If it fails, keep the exact status/error and ScavengerSenseCapture.log, including
the capture dimensions/format. There is no silent fallback to an earlier,
overlay-free screenshot. Ordinary screenshots remain available independently.

Settings and translation
------------------------
No personal INI is shipped. Save in the menu to create
Data/SKSE/Plugins/ScavengerSenseCapture.ini (normally MO2 overwrite when new).
ScavengerSenseCapture.example.ini documents the defaults for manual editing.
Reinstallation does not replace a saved personal INI from this package.
Core presets/settings do not alter capture settings.

language=auto reads the core installer's language fragments and then the core
personal INI. Reload capture settings after changing the core menu language.
English is the fallback. Chinese text needs a suitable framework font/range.
For another translation, copy ScavengerSenseCapture_english.ini to
ScavengerSenseCapture_<language>.ini beside the DLL; edit only the right side
of [Translation] entries in UTF-8, then reload and choose its language code.
Missing/empty entries fall back to English. Diagnostic details remain English.

Source: https://github.com/KinonNeko/ScavengerSense-repo/tree/codex/optional-screenshot-addon
The source lives in plugin/capture. ScavengerSenseCapture_Request() is an
optional exported C function returning bool: queues a request, or returns false
if disabled/busy. Clients should resolve it dynamically, never require the DLL.

中文简要说明
------------
这是独立、可选的截图扩展；安装器默认不勾选，主模组不依赖它。
快捷键默认 Ctrl + Shift + F10，可在 Scavenger Sense -> Capture 页调整并保存。
“关闭菜单后截图”会等待菜单释放键鼠（最多30秒）。截图包含此前已绘制到提交画面的UI。
图片默认在游戏本体目录 Screenshots/ScavengerSense，PNG不覆盖旧图片。
新配置保存后通常在 MO2 overwrite，示例 INI 不会自动载入或覆盖个人配置。
首版支持8位SDR；HDR/10位与部分D3D12帧生成输出暂不支持，会显示错误。
CS/升频/帧生成的实际调用顺序仍需游戏内确认；离线图形测试不等于游戏实测。
独立安装到MO2后可直接禁用该扩展；合并安装时重新安装并取消勾选（先备份自定义文件）。
