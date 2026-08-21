<#
.SYNOPSIS
  Package an already-built Spindle as a portable ZIP.
.DESCRIPTION
  Packaging only — it does not build. Run scripts/build.ps1 first; this script
  only stages that existing build output into dist\Spindle and archives it, so
  it never triggers a rebuild that would invalidate a prior signature.

  With -Sign it Authenticode-signs the staged executables. The identity comes
  from the CODESIGN_CERT environment variable — see
  scripts/codesign-windows.ps1. For an installer, use
  scripts/package-windows-inno.ps1.
.EXAMPLE
  pwsh scripts/build.ps1
  pwsh scripts/package-windows.ps1
.EXAMPLE
  $env:CODESIGN_CERT = "My Publisher Name"
  pwsh scripts/package-windows.ps1 -Sign
.OUTPUTS
  dist\Spindle-<version>-windows-<arch>.zip
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

Write-Host "==> Done. Packages in: $distDir"
