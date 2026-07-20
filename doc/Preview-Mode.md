# Preview Mode

Live preview for Markdown, HTML, XML, and CSV in a split view inside Notepad.

## Quick start

1. Open a `.md`, `.html`, `.xml`, or `.csv` file (or switch scheme to Markdown / HTML / XML / CSV).
2. Enable preview:
   - **View → Preview Mode**, or
   - Toolbar **Preview Mode** (split-pane icon, before **Close**), or
   - Set `PreviewMode=1` in `Notepad.ini` and restart.
3. Edit in the top pane; preview refreshes after a short delay (~400 ms).

## Supported content

| Scheme | Preview behavior |
|--------|------------------|
| **Markdown** | GFM via md4c: tables, task lists, strikethrough, fenced code, links, images; fenced ` ```mermaid ` blocks render as diagrams (Mermaid.js) |
| **HTML** | Escaped source in a readable HTML wrapper |
| **XML** | Same as HTML |
| **CSV** | RFC-style table (comma-separated rows) |

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

- Hold **Ctrl** and scroll the **mouse wheel** over the preview area.
- Range: **50%–250%**, step **10%**.
- Value is stored in `PreviewZoomPercent`.

There are no on-screen zoom buttons.

## Preview context menu

Right-click in the preview:

- **Copy** / **Select All**
- **Open Link** / **Copy Link** (when over a hyperlink)

## Theme

- **Light:** GitHub-like styling (similar to github.com markdown body).
- **Dark:** Used when Notepad dark shell theme is active (`Notepad DarkTheme.ini` / dark style theme).

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

- Install the Evergreen [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/) and restart.
- Check `NotepadPreview.log` beside `Notepad.exe`.

### Mermaid diagrams missing

- Ensure `preview/mermaid.min.js` sits next to `Notepad.exe` (copied on MSVC build).
- Use a fenced block: ` ```mermaid ` … ` ``` `.

### Preview is empty or stale

- Ensure the window is large enough (preview needs non-zero height).
- Check `NotepadPreview.log` beside `Notepad.exe`.
- Set `NP2_PREVIEW_LOG=0` to disable logging if the log file is unwanted.

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

- Module: `src/PreviewMode.cpp`, `src/PreviewMode.h`
- Markdown: `src/md4c/` (compiled as C, `__cdecl` `md_html`)
- Browser: **WebView2** (Edge Chromium); SDK under `third_party/webview2/`
- Assets: `preview/mermaid.min.js` mapped as virtual host `np2.preview`
- Updates: timer + posted `APPM_PREVIEW_UPDATE` to avoid re-entrancy hangs
- Editor monospace for Markdown code/tables: `font:$(Code)` in `src/EditLexers/stlMarkdown.cpp`
