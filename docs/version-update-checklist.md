# Version update checklist

Everything the build and the packagers report is derived from one number, so a
release touches very few files. The list below is the whole of it.

## 1. `CMakeLists.txt` — the single source of truth

```cmake
project(Spindle VERSION 0.8.0 LANGUAGES C CXX)
```

`PROJECT_VERSION` flows from here into:

| Consumer | How |
|---|---|
| `SPINDLE_VERSION` compile definition | `CMakeLists.txt` |
| macOS bundle version / short version | `MACOSX_BUNDLE_*` |
| CPack package name and version | `CPACK_PACKAGE_*` |
| `scripts/package-windows.ps1`, `scripts/package-windows-inno.ps1` | read `project(... VERSION ...)` out of `CMakeLists.txt` with a regex |
| `scripts/package-macos.sh`, `scripts/package-linux.sh` | same |

Nothing else declares a version. Do not add a second copy.

## 2. `CHANGELOG.md`

Add a `## [<version>] - <YYYY-MM-DD>` section at the top, above the previous
release. Keep a Changelog headings (`Added` / `Changed` / `Fixed` / `Removed`),
newest release first.

## 3. `scripts/README.md`

The naming-convention paragraph spells out example filenames
(`Spindle-<version>-windows-x64.exe` and friends). They are illustrative, but
keep them on the current version so nobody reads a stale number as the latest
release.

## Not part of a version bump

- `README.md` / `README.ja.md` mention `v0.3.5` only as an example of the tag
  format for the release workflow. Leave them alone.
- `i18n/spindle_en.ts` carries no version.
## Releasing

Tagging is a separate, deliberate step. `.github/workflows/build.yml` builds
and packages all three platforms on every push and PR; a tag matching `v*`
additionally publishes a GitHub Release with every package attached.

```sh
git tag v0.8.0 && git push origin v0.8.0
```

The packages CI produces are unsigned. A signed Windows build has to be made on
a machine that holds the certificate:

```powershell
pwsh scripts/package-windows-inno.ps1 -Sign
```

## After editing

```powershell
pwsh scripts/build.ps1
```

and confirm the built binary reports the new version (the About dialog uses
`SPINDLE_VERSION`).
