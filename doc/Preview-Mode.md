# Preview Mode

Live preview for Markdown, HTML, XML, and CSV in a split view inside Notepad.

**MD++ markup reference:** [`MD++-markup.md`](MD++-markup.md) — how to write every supported construct.  
**MD++ test document:** open [`MD++-preview-test.md`](MD++-preview-test.md) to verify all extended Markdown features.

## Quick start

1. Open a `.md`, `.html`, `.xml`, or `.csv` file (or switch scheme to Markdown / HTML / XML / CSV).
2. Enable preview (two independent toolbar toggles next to **Close**):
   - **Preview Mode** (split-pane icon) — split view: editor + preview.
   - **Maximize Preview** (full-pane icon) — preview fills the window.
   - Only one button can be pressed at a time; clicking one mode turns the other off.
   - Press the active button again to turn preview off completely.
   - Same actions: **View → Preview Mode** / **View → Maximize Preview**; double-click the splitter switches split ↔ fullscreen while preview is on.
   - Or set `PreviewMode=1` or `PreviewMaximized=1` in `Notepad.ini` and restart.
3. Edit in the top pane (split mode) or hide the editor (maximize); preview refreshes after a short delay (~400 ms).

## Supported content

| Scheme | Preview behavior |
|--------|------------------|
| **Markdown (MD++)** | GFM via md4c (`MD_DIALECT_MDPP`): tables, task lists, strikethrough, footnotes, GFM admonitions (`> [!NOTE]`), `==highlight==`, `\|\|spoilers\|\|`, superscript/subscript, fenced code with highlight.js, `$KaTeX$` math, Mermaid diagrams, YAML frontmatter, `[TOC]`, mkdocs tabs, image attributes, Rentry-style syntax |
| **HTML** | Escaped source in a readable HTML wrapper |
| **XML** | Same as HTML |
| **CSV** | RFC-style table (comma-separated rows) |

### MD++ Markdown extensions

Full syntax with examples: **[`MD++-markup.md`](MD++-markup.md)**.

| Feature | Syntax |
|---------|--------|
| Admonitions | `> [!NOTE]`, `> [!WARNING]`, … |
| Highlight | `==text==` |
| Spoilers | `\|\|hidden\|\|` (click to reveal) |
| Math | `$E=mc^2$`, `$$\int f(x)dx$$` |
| Frontmatter | `---` YAML block at file start |
| TOC | `[TOC]` or `[TOC2]` |
| Tabs | `=== "Tab title"` + content |
| Image attrs | `![alt](url){300px:200px #left "title"}` |
| Rentry (compat) | `!!! info\|note\|warning\|success`, `%red%` / `%#hex%`, `-> center <-`, `!>spoiler` |

Other schemes do not offer preview; toggling preview on them has no effect.

## Layout

```
┌─────────────────────────────┐
│  Editor (Scintilla)         │
├─────────────────────────────┤  ← splitter (drag to resize)
│  Preview (WebView2)         │
└─────────────────────────────┘
```

- **Drag** the splitter to change pane heights (saved as `PreviewHeightPercent`).
- **Double-click** the splitter or use **View → Maximize Preview** to toggle full preview height.
- **View → Auto Enable Preview Mode** — turn preview on automatically when opening supported files.

## Links

- Clicking an `http://` or `https://` link opens it in the **system default browser**.
- The preview pane does **not** navigate to external sites.
- In-document anchors (`#section`) still scroll inside the preview.
- Context menu: **Open Link** (browser) and **Copy Link**.

## Clipboard (Ctrl+C)

- Select text in the preview, then **Ctrl+C** copies that selection (not the editor buffer).
- **Ctrl+A** selects all preview content.
- Context menu **Copy** / **Select All** also work.

## Zoom

- Hold **Ctrl** and scroll the **mouse wheel** over the preview area (including directly over WebView2 content).
- Range: **50%–250%**, step **10%**.
- Value is stored in `PreviewZoomPercent`.
- Zoom is delivered via `mdpp.js` → WebView2 `postMessage` → host handler (fallback: main-window `WM_MOUSEWHEEL` when cursor is outside WebView).

There are no on-screen zoom buttons.

Manual regression checks: [MD++-preview-test.md §11](./MD++-preview-test.md#11-регрессии-preview-ручная-проверка).

## Preview context menu

Right-click in the preview:

- **Copy** / **Select All**
- **Open Link** / **Copy Link** (when over a hyperlink)

## Theme

- **Light:** GitHub-like styling (similar to github.com markdown body).
- **Dark:** Used when Notepad dark shell theme is active (`Notepad DarkTheme.ini` / dark style theme).
- YAML `access.theme` in frontmatter can force `light` / `dark` / `auto`.

## Settings (`Notepad.ini`)

```ini
PreviewMode=0
PreviewAuto=0
PreviewHeightPercent=50
PreviewMaximized=0
PreviewZoomPercent=100
```

| Key | Meaning |
|-----|---------|
| `PreviewMode` | `1` = start with preview enabled (for supported schemes) |
| `PreviewAuto` | `1` = enable preview when opening a supported file |
| `PreviewHeightPercent` | Bottom pane size when not maximized (1–99) |
| `PreviewMaximized` | `1` = preview uses entire client height |
| `PreviewZoomPercent` | Zoom level for preview content |

## Saving Markdown (raw text)

Preview is read-only and never rewrites the file. `EditSaveFile` writes the Scintilla buffer as-is; there is **no** Markdown auto-formatter.

Optional global save options (Settings) can still change bytes on disk:

- **Fix line endings** (`FixLineEndings`)
- **Strip trailing blanks** (`FixTrailingBlanks` / `bAutoStripBlanks`)

For ASCII art or Mermaid source that relies on trailing spaces, keep strip trailing blanks **off**.

## Troubleshooting

### Preview shows “Preview unavailable”

- Install the Evergreen [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/) and restart, **or** use the portable **`Notepad_x64_with_WebView2`** artifact (bundles Fixed Version Runtime 131.x under `WebView2\`).
- Check `NotepadPreview.log` beside `Notepad.exe`.

### Slim vs with_WebView2 builds

| Artifact | Runtime | Notes |
|----------|---------|--------|
| `Notepad_x64` | System Evergreen | Smaller zip; needs WebView2 installed on the machine |
| `Notepad_x64_with_WebView2` | Fixed Version **131.0.2903.146** in `WebView2\` | Self-contained Preview; aimed at Windows 10 **19041/19044**-class systems and matching SDK `1.0.2903.40` |

Licenses for the bundled runtime ship inside `WebView2\` plus root `NOTICE-WebView2.txt` / [`License.txt`](../License.txt).

### Mermaid / KaTeX / code highlighting missing

- Ensure `preview/` folder sits next to `Notepad.exe` (copied on MSVC build).
- Use fenced blocks: ` ```mermaid `, ` ```python `, or `$...$` for math.

### Preview is empty or stale

- Ensure the window is large enough (preview needs non-zero height).
- Check `NotepadPreview.log` beside `Notepad.exe`.
- Set `NP2_PREVIEW_LOG=0` to disable logging if the log file is unwanted.

### Resize stripe, zoom, or Rentry admonition regressions

Run the manual checklist in [MD++-preview-test.md §11](./MD++-preview-test.md#11-регрессии-preview-ручная-проверка) (zoom, fast resize, content width, Rentry `!!! note`).

During fast resize the preview container and WebView2 default background follow the shell theme (`#0d1117` dark / white light) so gaps should not flash white in dark mode.

### Preview does not appear for Markdown

- Confirm scheme is **Markdown** (not Plain Text).
- Very large files are capped (`PREVIEW_MAX_TEXT_BYTES` ≈ 2 MiB in code).

### Toolbar button missing or old position

Remove custom layout from ini:

```ini
; delete or reset this line:
ToolbarButtons=...
```

Or use **View → Customize Toolbar** to restore defaults.

## Implementation notes

- Module: `src/PreviewMode.cpp`, `src/PreviewFrontmatter.cpp`, `src/PreviewPreprocessor.cpp`
- Markdown: `src/md4c/` with `MD_DIALECT_MDPP`
- Browser: **WebView2** (Edge Chromium); SDK under `third_party/webview2/`
- Runtime: Evergreen by default; optional fat zip bundles Fixed Version 131 under `WebView2\`
- Assets: `preview/` mapped as virtual host `np2.preview` (mermaid, katex, highlight.js, `mdpp.css`, `mdpp.js`)
- Updates: timer + posted `APPM_PREVIEW_UPDATE` to avoid re-entrancy hangs
- Editor monospace for Markdown code/tables: `font:$(Code)` in `src/EditLexers/stlMarkdown.cpp`
