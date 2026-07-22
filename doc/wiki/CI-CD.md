# CI/CD

Continuous integration builds **Notepad** for **64-bit Windows** (Windows 10 / 11) and uploads zip artifacts.

## GitHub Actions

Workflow: [`.github/workflows/main.yml`](../../.github/workflows/main.yml)

Triggers: `push`, `pull_request`, `workflow_dispatch` (manual).

### Job: `build-x64`

| Setting | Value |
|---------|--------|
| Runner | `windows-2022` |
| Toolchain | MSVC Release |
| Platform | `x64` only |
| Locales | yes (all packaged locales) |

Steps:

1. Checkout
2. Patch `VersionRev.h` with short git hash
3. `build\VisualStudio\build.bat Build x64 Release 1`
4. `locale\build.bat Build x64 Release 1`
5. `build\make_zip.bat MSVC x64 Release Locale 1`
6. Verify `Notepad.exe`, `matepath.exe`, and `Notepad_i18n_x64_*.zip`
7. Upload artifact **`Notepad_x64`** (slim / Evergreen)
8. Build fat zip via `build\make_zip_webview2.ps1` (Fixed Version **131.0.2903.146**) when a Microsoft-acquired cab is available
9. Upload artifact **`Notepad_x64_with_WebView2`** when the fat zip was produced

Upstream-style matrices (Win32, ARM64, AVX2/512, Clang, MinGW) are **disabled** in this fork to keep CI fast and produce useful builds.

### Fat / Fixed Version cab

Pin: [`build/webview2-fixed-version.json`](../../build/webview2-fixed-version.json) (SDK-aligned **131.0.2903.146**, Win10 ~19041/19044).

Acquire the `.cab` **directly from Microsoft**, then either:

- set pin `url` to the Microsoft CDN URL, or
- set repository variable / secret **`WEBVIEW2_FIXED_CAB_URL`**, or
- set **`WEBVIEW2_FIXED_CAB`** to a local path (local builds)

CI caches the cab by SHA256. If no URL/cab is available, the slim artifact still uploads; the fat artifact step is skipped with a warning.

## AppVeyor

Config: [`appveyor.yml`](../../appveyor.yml) — MSVC x64 + locales slim zip (`Notepad_x64`). Fat zip is GitHub Actions–only unless you mirror the PowerShell step locally.

## Requirements for a green CI build

Commit these fork files (not optional for CI):

- `src/PreviewMode.cpp`, `src/PreviewMode.h`
- `src/DarkMode.cpp`, `src/DarkMode.h`
- `src/md4c/*`
- `third_party/webview2/*` (SDK headers + `WebView2LoaderStatic.lib`)
- `preview/mermaid.min.js`
- `doc/Notepad.ini`, `doc/Notepad DarkTheme.ini` (used by build scripts and `make_zip.bat`)
- Updated `res/Toolbar*.bmp` (if toolbar changed)
- `build/VisualStudio/Notepad4.vcxproj` (lists new sources / WebView2 link paths)
- `build/webview2-fixed-version.json`, `build/make_zip_webview2.ps1` (fat packaging)

## Common failures

| Symptom | Cause / fix |
|---------|-------------|
| `Notepad_zh-Hans_` target not found | Wrong `locale/build.bat` target — use `Notepad4_zh-Hans_` |
| `LNK1104` on Notepad.exe | Executable still running — kill process before build |
| Zip step fails on DarkTheme ini | Missing `doc/Notepad DarkTheme.ini` in repo |
| Zip step fails on Notepad.ini | Missing `doc/Notepad.ini` in repo |
| Empty artifact upload | Zip name mismatch — must be `Notepad_i18n_x64_*.zip` |
| Fat zip skipped | No Microsoft cab URL/path — set `WEBVIEW2_FIXED_CAB_URL` or pin `url` |
| Fat SHA256 mismatch | Wrong cab file; pin expects `131.0.2903.146` x64 |

## Local CI dry-run

```bat
build\VisualStudio\build.bat Build x64 Release 1
locale\build.bat Build x64 Release 1
build\make_zip.bat MSVC x64 Release Locale 1
dir build\Notepad_i18n_x64_*.zip

set WEBVIEW2_FIXED_CAB=C:\path\to\Microsoft.WebView2.FixedVersionRuntime.131.0.2903.146.x64.cab
powershell -File build\make_zip_webview2.ps1
dir build\Notepad_i18n_x64_with_WebView2_*.zip
```

## Version hash

CI replaces the 8-character revision in:

- `src/VersionRev.h`
- `matepath/src/VersionRev.h`

using `git` commit SHA (`%GITHUB_SHA%` / `%APPVEYOR_REPO_COMMIT%`).
