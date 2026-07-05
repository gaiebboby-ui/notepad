# Build Notepad (this fork)

Product output: **`Notepad.exe`**, **`matepath.exe`**, settings **`Notepad.ini`**.

Prerequisites: Visual Studio 2019 / 2022 / 2026 (or Build Tools) with Desktop C++, Windows SDK, MSBuild.

## MSVC (recommended)

```bat
cd build\VisualStudio
build.bat Build x64 Release
```

Output:

```
build\bin\Release\x64\Notepad.exe
build\bin\Release\x64\matepath.exe
build\bin\Release\x64\locale\ru\   (Russian DLLs built automatically for x64 Release)
```

Other platforms:

```bat
build.bat Build Win32 Release
build.bat Build ARM64 Release
build.bat Build AVX2 Release
build.bat Build AVX512 Release
build.bat Rebuild x64 Release
```

Close a running `Notepad.exe` before linking, or the linker may fail with `LNK1104`.

### Docs copied on x64 Release

- `doc\Notepad.ini` → `Notepad.ini-default`
- `doc\Notepad DarkTheme.ini` → output folder (required for `make_zip.bat`)

## All locale DLLs

```bat
cd locale
build.bat Build x64 Release
```

Builds every language in `Locale.sln`. MSBuild entry target:

```
/target:Notepad4_zh-Hans_;Build
```

> **Important:** the target name is `Notepad4_zh-Hans_` (project names in the solution are still `Notepad4(xx)`). Renaming it to `Notepad_zh-Hans_` breaks the build.

For AVX2 / AVX512, `locale\build.bat` builds x64 locales and copies them into the AVX folders.

## Release zip

```bat
cd build
make_zip.bat MSVC x64 Release Locale
```

Creates `build\Notepad_i18n_x64_v<version>.zip` with executables, default ini files, `Notepad DarkTheme.ini`, and `locale\`.

Requires **7-Zip** on PATH or in the registry.

## Clang (LLVM + MSVC toolset)

```bat
build\install_llvm.bat latest
build\VisualStudio\build.bat Build x64 LLVMRelease
build\make_zip.bat LLVM x64 Release
```

## MinGW (MSYS2 / llvm-mingw)

Used in CI; locally:

```bat
build\install_mingw.bat x86_64 GCC
cd build\mingw
build.bat x86_64 x64 GCC
```

Preview mode requires **md4c** C sources — listed in `build\mingw\notepad4.mk`.

## Toolbar bitmaps

Icons are assembled from PNG strips:

```bat
cd tools
python build_toolbar.py
```

Requires Python 3, `Pillow`, `cairosvg`. Updates `res\Toolbar16.bmp` … `Toolbar48.bmp`.  
Source SVG for preview button: `tools\images\Preview.svg`.  
See `tools\ImageTool.py` for the full icon list.

## Project layout (new / fork files)

| Path | Role |
|------|------|
| `src/PreviewMode.cpp`, `.h` | Preview split view |
| `src/DarkMode.cpp`, `.h` | Dark shell theme |
| `src/md4c/` | Markdown HTML renderer |
| `res/Toolbar*.bmp` | Toolbar images |
| `doc/Notepad DarkTheme.ini` | Bundled dark scheme |

## CodeLite / other

See `build\CodeLite\` and upstream [Build Notepad4](https://github.com/zufuliu/notepad4/wiki/Build-Notepad4) for additional generators; this fork’s MSVC path above is the primary one.
