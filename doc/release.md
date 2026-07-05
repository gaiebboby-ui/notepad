> [!Note]
> [v25.07r5738](https://github.com/zufuliu/notepad4/releases/tag/v25.07r5738) and later no longer supports Windows XP, Windows Server 2003 (see issue #974) and Windows 8/8.1 on ARM32 (Windows RT) (see issue #1023), use [v25.06r5696](https://github.com/zufuliu/notepad4/releases/tag/v25.06r5696) instead.

## v26.06r6196 (fork)

**Release:** https://github.com/gaiebboby-ui/notepad/releases/tag/v26.06r6196  
**Asset:** `Notepad_i18n_x64_v26.06r6196.zip` — MSVC x64, Windows 10/11, all locales.

See [doc/FORK-CHANGES.md](FORK-CHANGES.md) for differences from upstream Notepad4.

### Added
* **Preview Mode** — Markdown (md4c GFM), HTML, XML, CSV; split view, splitter, maximize; Ctrl+wheel zoom; context menu; `NotepadPreview.log`
* **Dark theme** — `Notepad DarkTheme.ini`, `DarkMode` shell integration, dark preview scrollbars
* **Toolbar** — Preview Mode button (custom icon, before Close); default scale 173%

### Product rename (vs Notepad4)
* `Notepad.exe`, `Notepad.ini`, `Notepad DarkTheme.ini`, `Notepad_*.zip`

### Settings (`Notepad.ini`)
* `PreviewMode`, `PreviewAuto`, `PreviewHeightPercent`, `PreviewMaximized`, `PreviewZoomPercent`

---

## Unreleased

* Scintilla updated to 5, see https://www.scintilla.org/ScintillaHistory.html for the changes.
* Lexer and API updates: .
* Other bug fixes and improvements.

## Behavior Changes

## Breaking Changes

## File List
| Localization | Language | Architecture | Minimum System |
|--|--|--|--|
| de | Deutsch | ARM64 | 🟢Windows 10 on ARM |
| en | English | AVX512🧪 | 🟢64-bit Windows 10, Server 2019 |
| fr | French | AVX2 | 🟢64-bit Windows 7, Server 2008 R2<sup>[1]</sup> |
| it | Italiano | x64 | 🟡legacy 64-bit Windows Vista, Server 2008 |
| ja | 日本語 | Win32 | 🟠legacy Windows Vista, Server 2008 |
| ko | 한국어 |
| pl | Polski | HD | HiDPI, High Definition |
| ru | Русский |
| zh-Hans | 中文 (简体) | winget | `winget install zufuliu.notepad4.AVX2` |
| zh-Hant | 中文 (繁體) | winget | `winget install zufuliu.notepad4` |
| i18n | all above languages | winget | `winget install -e "Notepad"` |
| 한국어 | https://teus.me/Notepad | Chocolatey | `choco install notepad4` |

1. [Platform Update for Windows 7](https://www.microsoft.com/en-us/download/details.aspx?id=36805) is required for AVX2 version to use Direct2D on Windows 7 and Server 2008 R2.

Latest development builds (artifacts in Release configuration for each compiler and platform) are available at https://github.com/zufuliu/notepad4/actions and https://ci.appveyor.com/project/zufuliu/notepad4.

[FindInFiles](https://github.com/zufuliu/FindInFiles) (see issue #259) is a simple GUI for [ripgrep](https://github.com/BurntSushi/ripgrep/releases), latest builds are available at https://github.com/zufuliu/FindInFiles/actions.
| File | System Requirement |
| - | - |
| FindInFiles-net4.8 | [.NET Framework 4.8](https://dotnet.microsoft.com/en-us/download/dotnet-framework) prior Windows 10 |
| FindInFiles-net10.0 | [.NET Desktop Runtime 10.0](https://dotnet.microsoft.com/en-us/download/dotnet/10.0) |
| FindInFiles-net8.0 | [.NET Desktop Runtime 8.0](https://dotnet.microsoft.com/en-us/download/dotnet/8.0) |
