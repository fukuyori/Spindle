<#
.SYNOPSIS
  Configure and build Spindle on Windows.
.DESCRIPTION
  Requires Qt 6 (MSVC) and CMake. Point -QtPrefix (or the QT_PREFIX env var,
  or CMAKE_PREFIX_PATH) at your Qt kit, e.g. C:\Qt\6.8.0\msvc2022_64.
.EXAMPLE
  pwsh scripts/build.ps1 -QtPrefix C:\Qt\6.8.0\msvc2022_64
#>
param(
  [string]$QtPrefix = $env:QT_PREFIX,
  [string]$BuildType = "Release",
  [string]$BuildDir = ""
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $root "build" }
if (-not $QtPrefix) { $QtPrefix = $env:CMAKE_PREFIX_PATH }

$cmakeArgs = @("-S", $root, "-B", $BuildDir, "-DCMAKE_BUILD_TYPE=$BuildType")
if ($QtPrefix) { $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtPrefix" }

$prefixLabel = if ($QtPrefix) { $QtPrefix } else { "<system>" }
Write-Host "==> Configuring (type=$BuildType, prefix=$prefixLabel)"
cmake @cmakeArgs

Write-Host "==> Building"
cmake --build $BuildDir --config $BuildType --parallel

Write-Host "==> Done. Artifacts in: $BuildDir"
