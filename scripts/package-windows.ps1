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
  [string]$BuildType = "Release",
  [string]$BuildDir = ""
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $root "build" }
$distDir = Join-Path $root "dist"
if (-not $QtPrefix) { $QtPrefix = $env:CMAKE_PREFIX_PATH }

function Resolve-QtPrefix {
  param([string]$Requested)

  if ($Requested) { return $Requested }
  if (-not (Test-Path -LiteralPath "C:\Qt" -PathType Container)) { return "" }

  $versions = Get-ChildItem -LiteralPath "C:\Qt" -Directory -ErrorAction SilentlyContinue |
    Sort-Object -Property Name -Descending
  foreach ($versionDir in $versions) {
    foreach ($kit in @("msvc2022_64", "msvc2019_64", "msvc_64", "mingw_64")) {
      $candidate = Join-Path $versionDir.FullName $kit
      $qtConfig = Join-Path $candidate "lib\cmake\Qt6\Qt6Config.cmake"
      $windeployqt = Join-Path $candidate "bin\windeployqt.exe"
      if ((Test-Path -LiteralPath $qtConfig -PathType Leaf) -and
          (Test-Path -LiteralPath $windeployqt -PathType Leaf)) {
        return $candidate
      }
    }
  }

  return ""
}

function Copy-DeployedBuildOutput {
  param(
    [string]$SourceDir,
    [string]$DestinationDir
  )

  $excludedNames = @(
    ".cmake",
    ".qt",
    "CMakeCache.txt",
    "CMakeFiles",
    "cmake_install.cmake",
    "CPackConfig.cmake",
    "CPackSourceConfig.cmake",
    "install_manifest.txt",
    "Makefile",
    "miniz_autogen",
    "spindle_autogen"
  )
  $excludedExtensions = @(".cmake", ".ilk", ".lib", ".obj", ".pdb")

  Get-ChildItem -LiteralPath $SourceDir -Force | ForEach-Object {
    $isExcluded = ($excludedNames -contains $_.Name) -or
      (-not $_.PSIsContainer -and ($excludedExtensions -contains $_.Extension))
    if (-not $isExcluded) {
      Copy-Item -LiteralPath $_.FullName -Destination $DestinationDir -Recurse -Force
    }
  }
}

$QtPrefix = Resolve-QtPrefix $QtPrefix

# Version from CMakeLists.txt
$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") `
  -Pattern 'project\(Spindle VERSION ([0-9.]+)').Matches[0].Groups[1].Value
if (-not $version) { $version = "0.0.0" }

# Build and deploy Qt runtime into the build output. Packaging always uses this
# build directory as its source of truth.
& (Join-Path $PSScriptRoot "build.ps1") -QtPrefix $QtPrefix -BuildType $BuildType -BuildDir $BuildDir

# Locate exe (single- or multi-config layout).
$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "spindle.exe" | Select-Object -First 1
if (-not $exe) { throw "spindle.exe not found under $BuildDir" }
$buildOutputDir = $exe.DirectoryName

# Stage the deployed build output into dist\Spindle.
$stage = Join-Path $distDir "Spindle"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Write-Host "==> Staging deployed build output from $buildOutputDir"
Copy-DeployedBuildOutput $buildOutputDir $stage

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
