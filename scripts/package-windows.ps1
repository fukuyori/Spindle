<#
.SYNOPSIS
  Build Spindle on Windows, bundle the Qt runtime (incl. WebEngine) and package
  it as a portable ZIP and, if NSIS (makensis) is available, an installer .exe.
.DESCRIPTION
  Requires Qt 6 (MSVC) with windeployqt, CMake, and an MSVC toolchain.
  Point -QtPrefix / QT_PREFIX / CMAKE_PREFIX_PATH at the Qt kit.
.EXAMPLE
  pwsh scripts/package-windows.ps1 -QtPrefix C:\Qt\6.8.0\msvc2022_64
.OUTPUTS
  dist\Spindle-<version>-windows-x64.zip  (+ ...-setup.exe with NSIS)
#>
param(
  [string]$QtPrefix = $env:QT_PREFIX,
  [string]$BuildType = "Release"
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root "build"
$distDir = Join-Path $root "dist"
if (-not $QtPrefix) { $QtPrefix = $env:CMAKE_PREFIX_PATH }

# Version from CMakeLists.txt
$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") `
  -Pattern 'project\(Spindle VERSION ([0-9.]+)').Matches[0].Groups[1].Value
if (-not $version) { $version = "0.0.0" }

# Build
& (Join-Path $PSScriptRoot "build.ps1") -QtPrefix $QtPrefix -BuildType $BuildType -BuildDir $buildDir

# Locate exe (single- or multi-config layout) and windeployqt.
$exe = Get-ChildItem -Path $buildDir -Recurse -Filter "spindle.exe" | Select-Object -First 1
if (-not $exe) { throw "spindle.exe not found under $buildDir" }
$windeployqt = if ($QtPrefix) { Join-Path $QtPrefix "bin\windeployqt.exe" } else { "windeployqt.exe" }
if (-not (Get-Command $windeployqt -ErrorAction SilentlyContinue) -and -not (Test-Path $windeployqt)) {
  throw "windeployqt not found (set -QtPrefix)"
}

# Stage into dist\Spindle and deploy Qt dependencies there.
$stage = Join-Path $distDir "Spindle"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item $exe.FullName (Join-Path $stage "spindle.exe")

Write-Host "==> Deploying Qt runtime with windeployqt"
& $windeployqt --release --compiler-runtime --no-translations (Join-Path $stage "spindle.exe")

# Portable ZIP
$zip = Join-Path $distDir "Spindle-$version-windows-x64.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Write-Host "==> Creating $zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip

# Optional NSIS installer
$makensis = Get-Command makensis -ErrorAction SilentlyContinue
if ($makensis) {
  Write-Host "==> Building NSIS installer"
  $nsi = Join-Path $distDir "spindle.nsi"
@"
!include "MUI2.nsh"
Name "Spindle"
OutFile "Spindle-$version-windows-x64-setup.exe"
InstallDir "`$PROGRAMFILES64\Spindle"
RequestExecutionLevel admin
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"
Section "Install"
  SetOutPath "`$INSTDIR"
  File /r "Spindle\*.*"
  CreateShortcut "`$SMPROGRAMS\Spindle.lnk" "`$INSTDIR\spindle.exe"
  WriteUninstaller "`$INSTDIR\uninstall.exe"
SectionEnd
Section "Uninstall"
  Delete "`$SMPROGRAMS\Spindle.lnk"
  RMDir /r "`$INSTDIR"
SectionEnd
"@ | Set-Content -Encoding UTF8 $nsi
  Push-Location $distDir
  & $makensis.Source $nsi
  Pop-Location
  Remove-Item $nsi
} else {
  Write-Host "==> makensis not found — skipping installer (portable ZIP produced)."
  Write-Host "    Install NSIS (https://nsis.sourceforge.io) to also build a setup.exe."
}

Write-Host "==> Done. Packages in: $distDir"
