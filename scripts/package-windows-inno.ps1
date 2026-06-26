<#
.SYNOPSIS
  Build Spindle on Windows, bundle the Qt runtime, and create an Inno Setup
  installer.
.DESCRIPTION
  Requires Qt 6 (MSVC) with windeployqt, CMake, an MSVC toolchain, and Inno
  Setup 6 (ISCC.exe). Point -QtPrefix / QT_PREFIX / CMAKE_PREFIX_PATH at the Qt
  kit when it cannot be auto-detected.
.EXAMPLE
  pwsh scripts/package-windows-inno.ps1 -QtPrefix C:\Qt\6.11.1\msvc2022_64
.OUTPUTS
  dist\Spindle-<version>-windows-x64-inno-setup.exe
#>
param(
  [string]$QtPrefix = $env:QT_PREFIX,
  [string]$BuildType = "Release",
  [string]$BuildDir = "",
  [string]$InnoCompiler = $env:ISCC_EXE
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

function Resolve-InnoCompiler {
  param([string]$Requested)

  if ($Requested -and (Test-Path -LiteralPath $Requested -PathType Leaf)) {
    return (Resolve-Path -LiteralPath $Requested).Path
  }

  $cmd = Get-Command iscc -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }

  foreach ($candidate in @(
      "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
      "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
      "${env:ProgramFiles(x86)}\Inno Setup 5\ISCC.exe",
      "$env:ProgramFiles\Inno Setup 5\ISCC.exe")) {
    if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
      return (Resolve-Path -LiteralPath $candidate).Path
    }
  }

  return ""
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
$iscc = Resolve-InnoCompiler $InnoCompiler
if (-not $iscc) {
  throw "Inno Setup compiler not found. Install Inno Setup 6 or set -InnoCompiler / ISCC_EXE."
}

$version = (Select-String -Path (Join-Path $root "CMakeLists.txt") `
  -Pattern 'project\(Spindle VERSION ([0-9.]+)').Matches[0].Groups[1].Value
if (-not $version) { $version = "0.0.0" }

# Build and deploy Qt runtime into the build output. Packaging always uses this
# build directory as its source of truth.
& (Join-Path $PSScriptRoot "build.ps1") -QtPrefix $QtPrefix -BuildType $BuildType -BuildDir $BuildDir

$exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "spindle.exe" | Select-Object -First 1
if (-not $exe) { throw "spindle.exe not found under $BuildDir" }
$buildOutputDir = $exe.DirectoryName

$stage = Join-Path $distDir "Spindle"
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Write-Host "==> Staging deployed build output from $buildOutputDir"
Copy-DeployedBuildOutput $buildOutputDir $stage

$iss = Join-Path $distDir "spindle-inno.iss"
$icon = Join-Path $root "resources\spindle.ico"
$escapedStage = $stage.Replace("\", "\\")
$escapedDist = $distDir.Replace("\", "\\")
$escapedIcon = $icon.Replace("\", "\\")

@"
#define MyAppName "Spindle"
#define MyAppVersion "$version"
#define MyAppPublisher "Spindle"
#define MyAppExeName "spindle.exe"
#define SourceDir "$escapedStage"

[Setup]
AppId={{7C6F3BD3-ED93-47DF-8C84-DB14E9F7565C}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=$escapedDist
OutputBaseFilename=Spindle-$version-windows-x64-inno-setup
SetupIconFile=$escapedIcon
UninstallDisplayIcon={app}\{#MyAppExeName}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
"@ | Set-Content -Encoding UTF8 -LiteralPath $iss

Write-Host "==> Building Inno Setup installer"
try {
  Invoke-Native $iscc @($iss)
} finally {
  if (Test-Path -LiteralPath $iss) {
    Remove-Item -LiteralPath $iss -Force
  }
}

Write-Host "==> Done. Installer: $(Join-Path $distDir "Spindle-$version-windows-x64-inno-setup.exe")"
