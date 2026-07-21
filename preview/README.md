# Preview assets (MD++)

Offline assets copied next to `Notepad.exe` as `preview/` on build.

| File | Purpose |
|------|---------|
| `mermaid.min.js` | Mermaid 11.4.1 — diagram rendering |
| `katex.min.js` / `katex.min.css` | KaTeX 0.16.11 — LaTeX math (`$...$`, `$$...$$`) |
| `highlight.min.js` | highlight.js 11.10.0 — fenced code syntax highlighting |
| `github.min.css` / `github-dark.min.css` | highlight.js themes (light / dark) |
| `mdpp.css` | MD++ preview styles (admonitions, spoilers, tabs, TOC, …) |
| `mdpp.js` | MD++ post-process pipeline (`np2Apply`) |

Virtual host: `https://np2.preview/` → `<exe>/preview/` (WebView2).

**Manual test:** open [`doc/MD++-preview-test.md`](../doc/MD++-preview-test.md) in Notepad with Preview Mode enabled.
