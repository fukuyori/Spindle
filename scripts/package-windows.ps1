<#
.SYNOPSIS
  Package an already-built Spindle as a portable ZIP and, if NSIS (makensis)
  is available, an installer .exe.
.DESCRIPTION
  Packaging only — it does not build. Run scripts/build.ps1 first; this script
  only stages that existing build output into dist\Spindle and archives it, so
  it never triggers a rebuild that would invalidate a prior signature.

  With -Sign it Authenticode-signs the staged executables, the installer and
  the uninstaller (NSIS !finalize / !uninstfinalize). The identity comes from
  the CODESIGN_CERT environment variable — see scripts/codesign-windows.ps1.
.EXAMPLE
  pwsh scripts/build.ps1
  pwsh scripts/package-windows.ps1
.EXAMPLE
  $env:CODESIGN_CERT = "My Publisher Name"
  pwsh scripts/package-windows.ps1 -Sign
.OUTPUTS
  dist\Spindle-<version>-windows-<arch>.zip  (+ ...-windows-<arch>.exe with NSIS)
#>
param(
  [string]$BuildDir = "",
  [switch]$Sign,
  [string]$SignTool = $env:SIGNTOOL_EXE
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $root "build" }
$distDir = Join-Path $root "dist"

. (Join-Path $PSScriptRoot "codesign-windows.ps1")

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

# Package names carry the architecture the binary was actually built for, which
# the Qt kit picks — read it out of the PE header rather than guessing from the
# host.
function Get-PeArchitecture {
  param([string]$Path)

  $stream = [System.IO.File]::OpenRead($Path)
  try {
    $reader = New-Object System.IO.BinaryReader($stream)
    $stream.Position = 0x3C
    $stream.Position = $reader.ReadInt32() + 4   # skip the "PE\0\0" signature
    $machine = $reader.ReadUInt16()
  } finally {
    $stream.Dispose()
  }

  switch ($machine) {
    0x8664  { "x64" }
    0xAA64  { "arm64" }
    0x014C  { "x86" }
    default { "unknown" }
  }
}

# Fail before staging anything if -Sign can't work.
if ($Sign) { $SignTool = Assert-SignTool $SignTool }

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
$arch = Get-PeArchitecture $exe.FullName

# Stage the deployed build output into dist\Spindle.
$stage = Join-Path $distDir "Spindle"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Write-Host "==> Staging deployed build output from $buildOutputDir"
Copy-DeployedBuildOutput $buildOutputDir $stage

# Sign the staged copies, never the build tree, so a later rebuild can't ship a
# stale signature.
if ($Sign) {
  Write-Host "==> Signing staged executables"
  $binaries = @(Select-SignableFile (Get-ChildItem -LiteralPath $stage -Recurse -File -Filter "*.exe" |
    Select-Object -ExpandProperty FullName))
  Invoke-CodeSign -Path $binaries -SignTool $SignTool
}

# Portable ZIP
$zip = Join-Path $distDir "Spindle-$version-windows-$arch.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Write-Host "==> Creating $zip"
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip

# Optional NSIS installer
$makensis = Get-Command makensis -ErrorAction SilentlyContinue
if ($makensis) {
  Write-Host "==> Building NSIS installer"

  # NSIS signs its own output: !finalize runs on the installer, !uninstfinalize
  # on the uninstaller stub before it is compressed into the installer.
  $signDirectives = ""
  if ($Sign) {
    $signLine = Get-CodeSignCommandLine -FileToken '"%1"' -SignTool $SignTool
    $signDirectives = @"
!finalize '$signLine'
!uninstfinalize '$signLine'
"@
  }

  $nsi = Join-Path $distDir "spindle.nsi"
@"
!include "MUI2.nsh"
Name "Spindle"
OutFile "Spindle-$version-windows-$arch.exe"
InstallDir "`$PROGRAMFILES64\Spindle"
RequestExecutionLevel admin
$signDirectives
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
  try {
    & $makensis.Source $nsi
    if ($LASTEXITCODE -ne 0) { throw "makensis exited with code $LASTEXITCODE" }
  } finally {
    Pop-Location
    Remove-Item $nsi -Force -ErrorAction SilentlyContinue
  }
} else {
  Write-Host "==> makensis not found — skipping installer (portable ZIP produced)."
  Write-Host "    Install NSIS (https://nsis.sourceforge.io) to also build a setup.exe."
}

Write-Host "==> Done. Packages in: $distDir"
