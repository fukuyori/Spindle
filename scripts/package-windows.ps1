<#
.SYNOPSIS
  Package an already-built Spindle as a portable ZIP and, if NSIS (makensis)
  is available, an installer .exe.
.DESCRIPTION
  Packaging only — it does not build or sign anything. Run scripts/build.ps1
  first (and code-sign build\spindle.exe yourself, if you sign releases);
  this script only stages that existing build output into dist\Spindle and
  archives it, so it never triggers a rebuild that would invalidate a prior
  signature.
.EXAMPLE
  pwsh scripts/build.ps1
  pwsh scripts/package-windows.ps1
.OUTPUTS
  dist\Spindle-<version>-windows-x64.zip  (+ ...-setup.exe with NSIS)
#>
param(
  [string]$BuildDir = ""
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $root "build" }
$distDir = Join-Path $root "dist"

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

# Version from CMakeLists.txt
$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") `
  -Pattern 'project\(Spindle VERSION ([0-9.]+)').Matches[0].Groups[1].Value
if (-not $version) { $version = "0.0.0" }

# Locate the already-built, already-deployed exe (single- or multi-config
# layout). This script never builds, so a prior code signature on it is never
# at risk of being clobbered by a rebuild.
$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "spindle.exe" -ErrorAction SilentlyContinue |
  Select-Object -First 1
if (-not $exe) {
  throw "spindle.exe not found under $BuildDir. Run scripts/build.ps1 first."
}
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
