# Build & packaging scripts

Cross-platform helpers. All output packages land in `dist/`.

| Script | Platform | Produces |
|--------|----------|----------|
| `build.sh` | macOS / Linux | local build in `build/` |
| `build.ps1` | Windows | local build in `build/` |
| `package-macos.sh` | macOS | `.dmg` (Qt bundled) |
| `package-linux.sh` | Linux | `.AppImage` + `.deb` / `.tar.gz` |
| `package-windows.ps1` | Windows | portable `.zip` |
| `package-windows-inno.ps1` | Windows | Inno Setup installer `.exe` |
| `codesign-windows.ps1` | Windows | (helper) Authenticode signing, dot-sourced by the two above |

Every package is named `Spindle-<version>-<os>-<arch>.<ext>`, e.g.
`Spindle-0.7.3-windows-x64.exe`, `Spindle-0.7.3-macos-arm64.dmg`,
`Spindle-0.7.3-linux-x86_64.AppImage`. The architecture is the one the binary
was actually built for (read from the PE header / `lipo`), not the host's.

## Prerequisites

- **Qt 6** (Widgets, Network, Xml, WebEngineWidgets, WebChannel), CMake ≥ 3.21,
  a C++17 compiler.
- Optional: **Qt TextToSpeech** (Qt Speech) for read-aloud with OS voices, and
  **Qt Multimedia** for the local AI voices (VOICEVOX / Piper) and the
  audio-file export — without these modules the build simply omits those
  features.
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
# Windows — build first; the packaging scripts never rebuild
pwsh scripts/build.ps1 -QtPrefix C:\Qt\6.8.0\msvc2022_64

pwsh scripts/package-windows.ps1        # portable zip
pwsh scripts/package-windows-inno.ps1   # Inno Setup installer

# ...and with Authenticode signatures on the executables, the installer
# and the uninstaller
$env:CODESIGN_CERT = "My Publisher Name"
pwsh scripts/package-windows-inno.ps1 -Sign
```

## Windows code signing (`-Sign`)

Both Windows packaging scripts take `-Sign`. It signs the staged executables,
and `package-windows-inno.ps1` additionally signs the installer and the
uninstaller (through Inno Setup's `SignTool` / `SignedUninstaller`). Binaries
somebody else already signed — Qt's `QtWebEngineProcess.exe`, Microsoft's `vc_redist.x64.exe`
— are left untouched, because `signtool` replaces a signature rather than
appending one.

| Variable | Meaning |
|----------|---------|
| `CODESIGN_CERT` | **Required.** A `.pfx`/`.p12` path, a 40-char SHA1 thumbprint, or a certificate subject name |
| `CODESIGN_PASSWORD` | Password for a `.pfx`/`.p12` |
| `CODESIGN_TIMESTAMP_URL` | RFC 3161 timestamp server (default `http://timestamp.digicert.com`) |
| `CODESIGN_DIGEST` | Digest algorithm (default `sha256`) |
| `SIGNTOOL_EXE` | Path to `signtool.exe`, if it is neither on PATH nor in the Windows SDK |

Only the staged copies under `dist\Spindle` are signed, never the build tree, so
a later rebuild cannot ship a stale signature.

## Notes

- The deploy tools (`macdeployqt` / `windeployqt` / `linuxdeploy-plugin-qt`)
  bundle the Qt WebEngine runtime (`QtWebEngineProcess`, locales, resources)
  needed for chapter rendering.
- Windows packaging scripts first deploy the Qt runtime into the build output
  directory, then stage that deployed build output for ZIP / Inno Setup
  packaging.
- macOS `.dmg` is only **ad-hoc signed**. For distribution outside your own
  machine, `codesign` with a Developer ID identity and notarize
  `build/spindle.app` before running the packaging script (or sign the
  resulting app and re-create the dmg).
- `package-windows-inno.ps1` requires Inno Setup 6 (`ISCC.exe`) on PATH, in the
  default install location, or passed via `-InnoCompiler`.
- CPack generators are also configured in `CMakeLists.txt`, so from a build
  directory you can run `cpack -G <ZIP|DragNDrop|DEB|TGZ>` directly.
