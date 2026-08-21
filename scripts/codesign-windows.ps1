<#
.SYNOPSIS
  Authenticode signing helpers shared by the Windows packaging scripts.
.DESCRIPTION
  Dot-source this file, then call Invoke-CodeSign (to sign files directly) or
  Get-CodeSignCommandLine (to hand a sign command to NSIS / Inno Setup, which
  sign the installer and uninstaller themselves).

  The signing identity comes from the CODESIGN_CERT environment variable:
    - a .pfx / .p12 file path  -> signtool /f (password from CODESIGN_PASSWORD)
    - a 40-char SHA1 thumbprint -> signtool /sha1 (certificate store lookup)
    - anything else             -> certificate subject name, signtool /n

  Optional overrides:
    CODESIGN_TIMESTAMP_URL  RFC 3161 timestamp server
                            (default: http://timestamp.digicert.com)
    CODESIGN_DIGEST         file/timestamp digest algorithm (default: sha256)
    SIGNTOOL_EXE            path to signtool.exe
#>

function Resolve-SignTool {
  param([string]$Requested = $env:SIGNTOOL_EXE)

  if ($Requested -and (Test-Path -LiteralPath $Requested -PathType Leaf)) {
    return (Resolve-Path -LiteralPath $Requested).Path
  }

  $cmd = Get-Command signtool.exe -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }

  $hostArch = switch ($env:PROCESSOR_ARCHITECTURE) {
    "ARM64" { "arm64" }
    "x86"   { "x86" }
    default { "x64" }
  }

  foreach ($kitBin in @("${env:ProgramFiles(x86)}\Windows Kits\10\bin",
                        "$env:ProgramFiles\Windows Kits\10\bin")) {
    if (-not (Test-Path -LiteralPath $kitBin -PathType Container)) { continue }

    # Newest SDK build first; fall back to the pre-versioned SDK layout.
    $found = Get-ChildItem -LiteralPath $kitBin -Directory -ErrorAction SilentlyContinue |
      Where-Object { $_.Name -match '^10\.' } |
      Sort-Object { [version]$_.Name } -Descending |
      ForEach-Object { Join-Path $_.FullName "$hostArch\signtool.exe" } |
      Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
      Select-Object -First 1
    if ($found) { return $found }

    $legacy = Join-Path $kitBin "$hostArch\signtool.exe"
    if (Test-Path -LiteralPath $legacy -PathType Leaf) { return $legacy }
  }

  return ""
}

function Get-SignToolIdentityArgs {
  param([string]$Certificate = $env:CODESIGN_CERT)

  if (-not $Certificate) {
    throw "Signing requires CODESIGN_CERT (a .pfx path, a SHA1 thumbprint, or a certificate subject name)."
  }

  if (Test-Path -LiteralPath $Certificate -PathType Leaf) {
    $identity = @("/f", $Certificate)
    if ($env:CODESIGN_PASSWORD) { $identity += @("/p", $env:CODESIGN_PASSWORD) }
    return $identity
  }

  if ($Certificate -match '^[0-9a-fA-F]{40}$') { return @("/sha1", $Certificate) }

  return @("/n", $Certificate)
}

function Get-SignToolCommonArgs {
  $timestamp = if ($env:CODESIGN_TIMESTAMP_URL) { $env:CODESIGN_TIMESTAMP_URL }
               else { "http://timestamp.digicert.com" }
  $digest = if ($env:CODESIGN_DIGEST) { $env:CODESIGN_DIGEST } else { "sha256" }
  return @("/fd", $digest, "/tr", $timestamp, "/td", $digest)
}

function Assert-SignTool {
  param([string]$SignTool)

  if (-not $SignTool) { $SignTool = Resolve-SignTool }
  if (-not $SignTool) {
    throw "signtool.exe not found. Install the Windows SDK (Signing Tools) or set SIGNTOOL_EXE."
  }
  # Fail fast on a missing/ambiguous certificate rather than mid-package.
  Get-SignToolIdentityArgs | Out-Null
  return $SignTool
}

<#
.SYNOPSIS
  Best-effort lookup of the certificate CODESIGN_CERT selects, used only to tell
  our own signature apart from a third party's. Returns $null when it cannot be
  resolved.
#>
function Resolve-SigningCertificate {
  param([string]$Certificate = $env:CODESIGN_CERT)

  if (-not $Certificate) { return $null }

  if (Test-Path -LiteralPath $Certificate -PathType Leaf) {
    try {
      $pfx = (Resolve-Path -LiteralPath $Certificate).Path
      $password = if ($env:CODESIGN_PASSWORD) { $env:CODESIGN_PASSWORD } else { "" }
      return [System.Security.Cryptography.X509Certificates.X509Certificate2]::new($pfx, $password)
    } catch {
      return $null
    }
  }

  $store = Get-ChildItem Cert:\CurrentUser\My, Cert:\LocalMachine\My -CodeSigningCert -ErrorAction SilentlyContinue
  if ($Certificate -match '^[0-9a-fA-F]{40}$') {
    return ($store | Where-Object { $_.Thumbprint -eq $Certificate } | Select-Object -First 1)
  }
  return ($store | Where-Object { $_.Subject -like "*$Certificate*" } | Select-Object -First 1)
}

<#
.SYNOPSIS
  Drop files that somebody else already signed.
.DESCRIPTION
  A staged tree carries third-party binaries the deploy tools copied in —
  QtWebEngineProcess.exe is signed by The Qt Company, vc_redist.exe by
  Microsoft. signtool replaces a signature rather than appending one, so
  re-signing those would strip their vendor's. Files carrying our own signature
  are kept in the list so a re-run refreshes them.
#>
function Select-SignableFile {
  param(
    [string[]]$Path,
    [string]$Certificate = $env:CODESIGN_CERT
  )

  $ours = Resolve-SigningCertificate $Certificate
  if (-not $ours) {
    Write-Warning ("Could not resolve the certificate CODESIGN_CERT selects; " +
      "every already-signed file will be left untouched.")
  }

  $signable = @()
  foreach ($file in $Path) {
    if (-not $file -or -not (Test-Path -LiteralPath $file -PathType Leaf)) { continue }
    $signer = (Get-AuthenticodeSignature -LiteralPath $file).SignerCertificate
    if ($signer -and (-not $ours -or $signer.Thumbprint -ne $ours.Thumbprint)) {
      $who = ($signer.Subject -split ',')[0]
      Write-Host "    already signed by $who — leaving untouched: $(Split-Path -Leaf $file)"
      continue
    }
    $signable += $file
  }
  return ,$signable
}

function Invoke-CodeSign {
  param(
    [AllowEmptyCollection()][string[]]$Path = @(),
    [string]$SignTool = ""
  )

  $files = @($Path | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) })
  if (-not $files) {
    Write-Host "    nothing left to sign"
    return
  }

  $SignTool = Assert-SignTool $SignTool
  $signArgs = @("sign") + (Get-SignToolIdentityArgs) + (Get-SignToolCommonArgs) + $files
  & $SignTool @signArgs
  if ($LASTEXITCODE -ne 0) { throw "signtool exited with code $LASTEXITCODE" }
}

<#
.SYNOPSIS
  Render the signtool invocation as a single command line for an installer
  builder that signs its own output.
.PARAMETER FileToken
  The builder's placeholder for the file being signed — '"%1"' for NSIS
  !finalize / !uninstfinalize, '$f' for an Inno Setup SignTool command (Inno
  quotes $f itself).
.PARAMETER QuoteToken
  How to quote an argument. Inno Setup wants its own '$q' escape rather than a
  literal double quote.
#>
function Get-CodeSignCommandLine {
  param(
    [Parameter(Mandatory = $true)][string]$FileToken,
    [string]$QuoteToken = '"',
    [string]$SignTool = ""
  )

  $SignTool = Assert-SignTool $SignTool
  $q = $QuoteToken

  $parts = @("$q$SignTool$q", "sign")
  foreach ($arg in ((Get-SignToolIdentityArgs) + (Get-SignToolCommonArgs))) {
    if ($arg -like "/*") { $parts += $arg } else { $parts += "$q$arg$q" }
  }
  $parts += $FileToken

  return ($parts -join " ")
}
