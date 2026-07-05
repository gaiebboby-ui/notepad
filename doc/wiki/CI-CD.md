# CI/CD

Continuous integration builds **Notepad** (this fork) on Windows and uploads zip artifacts.

## GitHub Actions

Workflow: [`.github/workflows/main.yml`](../../.github/workflows/main.yml)

Triggers: `push`, `pull_request`, `workflow_dispatch` (manual).

### Jobs

| Job | Runner | What it builds |
|-----|--------|----------------|
| **msvc** | `windows-2022`, `windows-2025-vs2026` | MSVC Release, all platforms, **with locales** |
| **llvm_msvc** | same | Clang + LLVM toolset, LLVMRelease, English only |
| **llvm_mingw** | `windows-latest` | llvm-mingw Clang |
| **mingw** | `windows-latest` | MSYS2 GCC/Clang (`continue-on-error: true`) |

Matrix platforms: `Win32`, `x64`, `ARM64`, `AVX2`, `AVX512`.

### MSVC job steps (typical)

1. Checkout
2. Patch `VersionRev.h` with short git hash
3. `build\VisualStudio\build.bat Build <platform> Release 1`
4. `locale\build.bat Build <platform> Release 1`
5. `build\make_zip.bat MSVC <platform> Release Locale 1`
6. Verify `Notepad.exe` exists
7. Upload artifact: `Notepad_MSVC<version>_i18n_<platform>`

### Artifact names (this fork)

| Pattern | Contents |
|---------|----------|
| `Notepad_i18n_<platform>_v*.zip` | MSVC + all locales |
| `Notepad_LLVM_<platform>_v*.zip` | Clang/LLVM MSVC |
| `Notepad_Clang_<platform>_v*.zip` | llvm-mingw |
| `Notepad_GCC_<platform>_v*.zip` | MSYS2 GCC |
| `Notepad_Clang_<platform>_v*.zip` | MSYS2 Clang |

Upstream used `Notepad4_*` prefixes; this fork uses **`Notepad_*`**.

## AppVeyor

Config: [`appveyor.yml`](../../appveyor.yml)

- **MSVC:** Win32, x64, AVX2, AVX512, ARM64 — each with locales and zip
- **Clang:** LLVMRelease for all platforms
- Artifacts: `build\Notepad*.zip`

## Requirements for a green CI build

Commit these fork files (not optional for CI):

- `src/PreviewMode.cpp`, `src/PreviewMode.h`
- `src/DarkMode.cpp`, `src/DarkMode.h`
- `src/md4c/*`
- `doc/Notepad DarkTheme.ini` (used by `make_zip.bat`)
- Updated `res/Toolbar*.bmp` (if toolbar changed)
- `build/VisualStudio/Notepad4.vcxproj` (lists new sources)

## Common failures

| Symptom | Cause / fix |
|---------|-------------|
| `Notepad_zh-Hans_` target not found | Wrong `locale/build.bat` target — use `Notepad4_zh-Hans_` |
| `LNK1104` on Notepad.exe | Executable still running — kill process before build |
| Zip step fails on DarkTheme ini | Missing `doc/Notepad DarkTheme.ini` in repo |
| MinGW undefined md4c symbols | Ensure `notepad4.mk` includes md4c objects |
| Empty artifact upload | Zip name mismatch — must be `Notepad_*.zip` |

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
