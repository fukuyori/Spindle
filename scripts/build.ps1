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
  [string]$BuildDir = "",
  [string]$CMake = "",
  [switch]$NoDeploy
)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $root "build" }
if (-not $QtPrefix) { $QtPrefix = $env:CMAKE_PREFIX_PATH }

function Resolve-ExistingFile {
  param([string[]]$Paths)
  foreach ($path in $Paths) {
    if ($path -and (Test-Path -LiteralPath $path -PathType Leaf)) {
      return (Resolve-Path -LiteralPath $path).Path
    }
  }
  return $null
}

function Resolve-CMake {
  param([string]$Requested)

  if ($Requested) {
    if (Test-Path -LiteralPath $Requested -PathType Leaf) {
      return (Resolve-Path -LiteralPath $Requested).Path
    }
    throw "CMake was not found at: $Requested"
  }

  $cmd = Get-Command cmake -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }

  $candidates = @()
  if ($env:ProgramFiles) {
    foreach ($edition in @("Community", "Professional", "Enterprise", "BuildTools")) {
      $candidates += Join-Path $env:ProgramFiles "Microsoft Visual Studio\18\$edition\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
      $candidates += Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\$edition\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    }
  }

  $cmakePath = Resolve-ExistingFile $candidates
  if ($cmakePath) { return $cmakePath }

  throw "CMake was not found. Install CMake, add it to PATH, or pass -CMake <path-to-cmake.exe>."
}

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
      if (Test-Path -LiteralPath $qtConfig -PathType Leaf) {
        return $candidate
      }
    }
  }

  return ""
}

function Resolve-WindeployQt {
  param([string]$QtPrefix)

  if ($QtPrefix) {
    $candidate = Join-Path $QtPrefix "bin\windeployqt.exe"
    if (Test-Path -LiteralPath $candidate -PathType Leaf) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }

  $cmd = Get-Command windeployqt -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }

  return ""
}

function Import-VsDeveloperEnvironment {
  $cl = Get-Command cl -ErrorAction SilentlyContinue
  if ($cl) { return }

  $candidates = @()
  if ($env:ProgramFiles) {
    foreach ($edition in @("Community", "Professional", "Enterprise", "BuildTools")) {
      $candidates += Join-Path $env:ProgramFiles "Microsoft Visual Studio\18\$edition\VC\Auxiliary\Build\vcvars64.bat"
      $candidates += Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022\$edition\VC\Auxiliary\Build\vcvars64.bat"
    }
  }

  $vcvars = Resolve-ExistingFile $candidates
  if (-not $vcvars) { return }

  Write-Host "==> Loading Visual Studio developer environment"
  $seenVariables = @{}
  cmd /s /c "`"$vcvars`" >nul && set" | ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") {
      $name = $matches[1]
      $key = $name.ToUpperInvariant()
      if (-not $seenVariables.ContainsKey($key)) {
        Set-Item -Path "Env:$name" -Value $matches[2]
        $seenVariables[$key] = $true
      }
    }
  }
}

function Invoke-Native {
  param(
    [string]$FilePath,
    [string[]]$Arguments
  )

  & $FilePath @Arguments
  if ($LASTEXITCODE -ne 0) {
    throw "$FilePath exited with code $LASTEXITCODE"
  }
}

function Get-CachedGenerator {
  param([string]$CachePath)

  if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) {
    return ""
  }

  $line = Get-Content -LiteralPath $CachePath |
    Where-Object { $_ -like "CMAKE_GENERATOR:INTERNAL=*" } |
    Select-Object -First 1
  if (-not $line) { return "" }
  return ($line -replace "^CMAKE_GENERATOR:INTERNAL=", "")
}

function Clear-CMakeConfigureCache {
  param(
    [string]$RootDir,
    [string]$BuildDir
  )

  $rootFull = [System.IO.Path]::GetFullPath($RootDir)
  $buildFull = [System.IO.Path]::GetFullPath($BuildDir)
  if (-not $buildFull.StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing to refresh a build directory outside the source tree: $buildFull"
  }

  Remove-Item -LiteralPath (Join-Path $buildFull "CMakeCache.txt") -Force -ErrorAction SilentlyContinue
  Remove-Item -LiteralPath (Join-Path $buildFull "CMakeFiles") -Recurse -Force -ErrorAction SilentlyContinue
}

$cmakeExe = Resolve-CMake $CMake
$QtPrefix = Resolve-QtPrefix $QtPrefix
Import-VsDeveloperEnvironment

$cmakeArgs = @("-S", $root, "-B", $BuildDir, "-DCMAKE_BUILD_TYPE=$BuildType")
$cachePath = Join-Path $BuildDir "CMakeCache.txt"
$useNMake = (Get-Command nmake -ErrorAction SilentlyContinue) -and -not $env:CMAKE_GENERATOR
$usingNMake = $false
if ($useNMake) {
  $cachedGenerator = Get-CachedGenerator $cachePath
  if ($cachedGenerator -and $cachedGenerator -ne "NMake Makefiles") {
    Write-Host "==> Refreshing CMake cache (generator: $cachedGenerator -> NMake Makefiles)"
    Clear-CMakeConfigureCache $root $BuildDir
  }
  $cmakeArgs += @("-G", "NMake Makefiles")
  $usingNMake = $true
}
if ($QtPrefix) { $cmakeArgs += "-DCMAKE_PREFIX_PATH=$QtPrefix" }

$prefixLabel = if ($QtPrefix) { $QtPrefix } else { "<system>" }
Write-Host "==> Configuring (type=$BuildType, prefix=$prefixLabel)"
Invoke-Native $cmakeExe $cmakeArgs

Write-Host "==> Building"
$buildArgs = @("--build", $BuildDir, "--config", $BuildType)
if (-not $usingNMake) { $buildArgs += "--parallel" }
Invoke-Native $cmakeExe $buildArgs

if ($IsWindows -and -not $NoDeploy) {
  $exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "spindle.exe" |
    Select-Object -First 1
  if (-not $exe) {
    throw "spindle.exe not found under $BuildDir"
  }

  $windeployqt = Resolve-WindeployQt $QtPrefix
  if (-not $windeployqt) {
    throw "windeployqt was not found. Set -QtPrefix to a Qt kit that contains bin\windeployqt.exe, or pass -NoDeploy."
  }

  $deployMode = if ($BuildType -eq "Debug") { "--debug" } else { "--release" }
  Remove-Item -LiteralPath (Join-Path $exe.DirectoryName "position\qtposition_nmea.dll") -Force -ErrorAction SilentlyContinue
  Write-Host "==> Deploying Qt runtime"
  Invoke-Native $windeployqt @($deployMode, "--compiler-runtime", "--no-translations", "--exclude-plugins", "qtposition_nmea", $exe.FullName)
}

Write-Host "==> Done. Artifacts in: $BuildDir"
