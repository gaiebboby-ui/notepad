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

Split editor view: source on top, live preview on bottom (**WebView2**).

- **Supported schemes:** Markdown, HTML, XML, CSV
- **Markdown:** rendered with [md4c](https://github.com/mity/md4c) (GFM dialect), GitHub-like CSS (light / dark); fenced **Mermaid** diagrams via offline `preview/mermaid.min.js`
- **HTML / XML:** escaped document preview
- **CSV:** HTML table preview
- **UI:** draggable splitter, double-click splitter or menu to maximize preview
- **Zoom:** **Ctrl + mouse wheel** over the preview pane (50–250%, step 10%)
- **Links:** `http`/`https` open in the **system browser** (preview never navigates away)
- **Runtime:** system Evergreen by default; fat zip may ship Fixed Version under `WebView2\`
- **Clipboard:** Ctrl+C / Ctrl+A work on preview selection (main accelerators skipped while preview has focus)
- **Context menu:** Copy, Select All, Open Link, Copy Link
- **Dark theme:** preview CSS follows application dark style theme

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

**Sources:** `src/PreviewMode.cpp`, `src/PreviewMode.h`, `src/md4c/`, `third_party/webview2/`, `preview/`

**Raw save:** no Markdown auto-format on save; only optional global EOL / trailing-blank fixes apply.

---

## Dark theme

- Default dark scheme file: `doc/Notepad DarkTheme.ini` (copied to the output folder on x64 Release build)
- Shell dark mode integration: `src/DarkMode.cpp`, `src/DarkMode.h`
- Preview pane respects application theme (CSS + native scrollbars)

---

## Toolbar

- Default UI scale increased by **20%**: `NP2_TOOLBAR_UI_SCALE_PERCENT` = **173** (was 144) in `src/config.h`
- Preview toolbar button uses dedicated bitmap index (see `tools/images/Preview.svg`)
- Default button order ends with: `… Scheme, Scheme Config | Preview | Maximize Preview | Exit | [wide gap] | Markdown format group`
- **Markdown format toolbar** (enabled for Markdown lexer only): Bold, Italic, Strikethrough, Code, Code Block, Heading▾, List▾, Link, Image, Quote, HR — after Exit with a wide separator gap
- Link and Image both treat the selection as the URL (`[](sel)` / `![alt](sel)`)
- Insert/wrap via `EditApplyMarkdownFormat()` → `EditEncloseSelection` / `EditModifyLines` (editor caret/selection only; Preview refreshes after buffer change)
- Dark toolbar: light `COLORSCHEME` button edges so icons stay visible
- Accelerators for Bold/Italic are reserved as commented stubs in `IDR_MAINWND` (Ctrl+Shift+B/I conflict with existing commands)

To reset toolbar layout after an upgrade, remove `ToolbarButtons` from `Notepad.ini` or use **View → Customize Toolbar**.

---

## Build & CI

- MSVC: `build\VisualStudio\build.bat Build x64 Release`
- Locales: `locale\build.bat Build x64 Release` (MSBuild target **`Notepad4_zh-Hans_`** — do not rename to `Notepad_`)
- Package: `build\make_zip.bat MSVC x64 Release Locale`
- Fat (optional Fixed Version WebView2): `powershell -File build\make_zip_webview2.ps1` with pin `build/webview2-fixed-version.json` (**131.0.2903.146**)
- MinGW: `src/md4c/*.c` linked from `build/mingw/notepad4.mk`
- Toolbar bitmaps: `python tools/build_toolbar.py` (requires `cairosvg`, `Pillow`; see `tools/ImageTool.py`)

GitHub Actions: `.github/workflows/main.yml` — MSVC x64 on `windows-2022`, artifacts **`Notepad_x64`** and **`Notepad_x64_with_WebView2`** (fat when `WEBVIEW2_FIXED_CAB_URL` / cached cab is available).  
AppVeyor: `appveyor.yml` — same x64 slim build (optional mirror).

**CI must include** (commit before push): `PreviewMode.*`, `DarkMode.*`, `src/md4c/`, `third_party/webview2/`, `preview/mermaid.min.js`, `doc/Notepad DarkTheme.ini`, updated `res/Toolbar*.bmp`, `tools/images/Preview.svg`, `build/webview2-fixed-version.json`, `build/make_zip_webview2.ps1`.

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
