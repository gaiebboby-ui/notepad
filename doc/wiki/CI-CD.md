# CI/CD

Continuous integration builds **Notepad** for **64-bit Windows** (Windows 10 / 11) and uploads one zip artifact.

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
7. Upload artifact **`Notepad_x64`** (retention 14 days)

Upstream-style matrices (Win32, ARM64, AVX2/512, Clang, MinGW) are **disabled** in this fork to keep CI fast and produce a single useful build.

## AppVeyor

Config: [`appveyor.yml`](../../appveyor.yml) — same **MSVC x64 + locales** build as GitHub Actions.

Artifact: `build\Notepad_i18n_x64_*.zip` as **`Notepad_x64`**.

## Requirements for a green CI build

Commit these fork files (not optional for CI):

- `src/PreviewMode.cpp`, `src/PreviewMode.h`
- `src/DarkMode.cpp`, `src/DarkMode.h`
- `src/md4c/*`
- `doc/Notepad.ini`, `doc/Notepad DarkTheme.ini` (used by build scripts and `make_zip.bat`)
- Updated `res/Toolbar*.bmp` (if toolbar changed)
- `build/VisualStudio/Notepad4.vcxproj` (lists new sources)

## Common failures

| Symptom | Cause / fix |
|---------|-------------|
| `Notepad_zh-Hans_` target not found | Wrong `locale/build.bat` target — use `Notepad4_zh-Hans_` |
| `LNK1104` on Notepad.exe | Executable still running — kill process before build |
| Zip step fails on DarkTheme ini | Missing `doc/Notepad DarkTheme.ini` in repo |
| Zip step fails on Notepad.ini | Missing `doc/Notepad.ini` in repo |
| Empty artifact upload | Zip name mismatch — must be `Notepad_i18n_x64_*.zip` |

## Local CI dry-run

```bat
build\VisualStudio\build.bat Build x64 Release 1
locale\build.bat Build x64 Release 1
build\make_zip.bat MSVC x64 Release Locale 1
dir build\Notepad_i18n_x64_*.zip
```

## Version hash

CI replaces the 8-character revision in:

- `src/VersionRev.h`
- `matepath/src/VersionRev.h`

using `git` commit SHA (`%GITHUB_SHA%` / `%APPVEYOR_REPO_COMMIT%`).
