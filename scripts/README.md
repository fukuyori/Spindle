# Build & packaging scripts

Cross-platform helpers. All output packages land in `dist/`.

| Script | Platform | Produces |
|--------|----------|----------|
| `build.sh` | macOS / Linux | local build in `build/` |
| `build.ps1` | Windows | local build in `build/` |
| `package-macos.sh` | macOS | `Spindle-<ver>-macOS.dmg` (Qt bundled) |
| `package-linux.sh` | Linux | `Spindle-<ver>-x86_64.AppImage` + `.deb` / `.tar.gz` |
| `package-windows.ps1` | Windows | portable `.zip` (+ NSIS `setup.exe`) |

## Prerequisites

- **Qt 6** (Widgets, Network, Xml, WebEngineWidgets, WebChannel), CMake ≥ 3.21,
  a C++17 compiler.
- Point the build at your Qt kit when it isn't auto-detected:
  - macOS (Homebrew): auto-detected via `brew --prefix qt`.
  - Linux: system Qt is found automatically, or set `CMAKE_PREFIX_PATH`.
  - Windows: pass `-QtPrefix C:\Qt\6.x\msvc2022_64` (or set `QT_PREFIX`).

## Examples

```sh
# macOS — build a .dmg
./scripts/package-macos.sh

# Linux — AppImage + .deb (downloads linuxdeploy on first run)
./scripts/package-linux.sh
SKIP_APPIMAGE=1 ./scripts/package-linux.sh    # only CPack deb/tgz
```

```powershell
# Windows — portable zip (+ installer if NSIS is installed)
pwsh scripts/package-windows.ps1 -QtPrefix C:\Qt\6.8.0\msvc2022_64
```

## Notes

- The deploy tools (`macdeployqt` / `windeployqt` / `linuxdeploy-plugin-qt`)
  bundle the Qt WebEngine runtime (`QtWebEngineProcess`, locales, resources)
  needed for chapter rendering.
- macOS `.dmg` is **unsigned**. For distribution outside your own machine,
  `codesign` and notarize `build/spindle.app` before running the packaging
  script (or sign the resulting app and re-create the dmg).
- CPack generators are also configured in `CMakeLists.txt`, so from a build
  directory you can run `cpack -G <ZIP|DragNDrop|DEB|TGZ>` directly.
