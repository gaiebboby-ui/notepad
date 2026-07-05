# Fork-specific changes (Notepad)

This document summarizes changes made in this fork relative to upstream [zufuliu/notepad4](https://github.com/zufuliu/notepad4). Internal source names (`Notepad4.cpp`, `Notepad4.vcxproj`, …) are unchanged; the **product name** is **Notepad**.

| Upstream | This fork |
|----------|-----------|
| `Notepad4.exe` | `Notepad.exe` |
| `Notepad4.ini` | `Notepad.ini` |
| `Notepad4 DarkTheme.ini` | `Notepad DarkTheme.ini` |
| Release zip `Notepad4_*.zip` | `Notepad_*.zip` |

See also: [Preview Mode](Preview-Mode.md), [Build](wiki/Build-Notepad.md), [CI/CD](wiki/CI-CD.md).

---

## Preview Mode

Split editor view: source on top, live preview on bottom (Trident / MSHTML).

- **Supported schemes:** Markdown, HTML, XML, CSV
- **Markdown:** rendered with [md4c](https://github.com/mity/md4c) (GFM dialect), GitHub-like CSS (light / dark)
- **HTML / XML:** escaped document preview
- **CSV:** HTML table preview
- **UI:** draggable splitter, double-click splitter or menu to maximize preview
- **Zoom:** **Ctrl + mouse wheel** over the preview pane (50–250%, step 10%). On-screen zoom buttons were removed.
- **Context menu:** Copy, Select All, Open/Copy Link, Copy Image (or image URL)
- **Selection:** text in preview is selectable; themed `::selection` colors
- **Dark theme:** preview scrollbars and browser chrome styled when dark shell theme is active

**Menu:** View → Preview Mode, Maximize Preview, Auto Enable Preview Mode  
**Toolbar:** Preview Mode button — second from the right, before Close (custom split-pane icon)

**Settings in `Notepad.ini`:**

| Key | Default | Description |
|-----|---------|-------------|
| `PreviewMode` | `0` | Remember preview on/off |
| `PreviewAuto` | `0` | Auto-enable preview when opening supported files |
| `PreviewHeightPercent` | `50` | Preview pane height (1–99) when not maximized |
| `PreviewMaximized` | `0` | Preview takes full client area |
| `PreviewZoomPercent` | `100` | Preview zoom level |

**Debug log:** `NotepadPreview.log` next to the executable. Disable: set environment variable `NP2_PREVIEW_LOG=0`.

**Sources:** `src/PreviewMode.cpp`, `src/PreviewMode.h`, `src/md4c/`

---

## Dark theme

- Default dark scheme file: `doc/Notepad DarkTheme.ini` (copied to the output folder on x64 Release build)
- Shell dark mode integration: `src/DarkMode.cpp`, `src/DarkMode.h`
- Preview pane respects application theme (CSS + native scrollbars)

---

## Toolbar

- Default UI scale increased by **20%**: `NP2_TOOLBAR_UI_SCALE_PERCENT` = **173** (was 144) in `src/config.h`
- Preview toolbar button uses dedicated bitmap index (see `tools/images/Preview.svg`)
- Default button order ends with: `… Scheme, Scheme Config | Preview | Close`

To reset toolbar layout after an upgrade, remove `ToolbarButtons` from `Notepad.ini` or use **View → Customize Toolbar**.

---

## Build & CI

- MSVC: `build\VisualStudio\build.bat Build x64 Release`
- Locales: `locale\build.bat Build x64 Release` (MSBuild target **`Notepad4_zh-Hans_`** — do not rename to `Notepad_`)
- Package: `build\make_zip.bat MSVC x64 Release Locale`
- MinGW: `src/md4c/*.c` linked from `build/mingw/notepad4.mk`
- Toolbar bitmaps: `python tools/build_toolbar.py` (requires `cairosvg`, `Pillow`; see `tools/ImageTool.py`)

GitHub Actions: `.github/workflows/main.yml` — single job **MSVC x64** on `windows-2022`, artifact **`Notepad_x64`** (`Notepad_i18n_x64_*.zip`).  
AppVeyor: `appveyor.yml` — same x64 build (optional mirror).

**CI must include** (commit before push): `PreviewMode.*`, `DarkMode.*`, `src/md4c/`, `doc/Notepad DarkTheme.ini`, updated `res/Toolbar*.bmp`, `tools/images/Preview.svg`.

---

## Fixed issues (this fork)

| Issue | Fix |
|-------|-----|
| Locale build failed in CI | Restored MSBuild target `Notepad4_zh-Hans_` in `locale/build.bat` |
| Preview button wrong icon | Dedicated `Preview.svg` + toolbar slot index 27 |
| Preview zoom buttons cluttered UI | Removed; Ctrl + wheel only |
| CI zip artifacts not found | Renamed paths to `Notepad_*.zip` in workflow / AppVeyor |
| MinGW link missing preview | Added md4c objects to `notepad4.mk` |
| Hang on large Markdown | Deferred updates via `APPM_PREVIEW_UPDATE`; md4c streaming |
| Context menu wrong position | `GetCursorPos()` instead of incorrect MSHTML coordinate transform |
| Dark preview scrollbars | `ApplyPreviewBrowserChrome()` on IE server + scrollbar HWNDs |
