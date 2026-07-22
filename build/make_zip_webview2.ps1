#Requires -Version 5.1
<#
.SYNOPSIS
  Build Notepad_x64_with_WebView2 fat zip: slim zip + Fixed Version Runtime under WebView2\.

.NOTES
  Reads pin from build/webview2-fixed-version.json.
  Cab source order:
    1) -CabPath / $env:WEBVIEW2_FIXED_CAB (local file previously acquired from Microsoft)
    2) $env:WEBVIEW2_FIXED_CAB_URL
    3) pin.url (Microsoft CDN)
#>
[CmdletBinding()]
param(
  [string]$SlimZip = '',
  [string]$CabPath = '',
  [string]$OutDir = '',
  [string]$PinPath = ''
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$buildDir = $PSScriptRoot
$repoRoot = Split-Path -Parent $buildDir
if (-not $PinPath) { $PinPath = Join-Path $buildDir 'webview2-fixed-version.json' }
if (-not $OutDir) { $OutDir = $buildDir }

function Find-SevenZip {
  $cmd = Get-Command 7z.exe -ErrorAction SilentlyContinue
  if ($cmd) { return $cmd.Source }
  foreach ($key in @(
      'HKLM:\SOFTWARE\7-Zip',
      'HKLM:\SOFTWARE\Wow6432Node\7-Zip',
      'HKLM:\SOFTWARE\7-Zip-Zstandard',
      'HKLM:\SOFTWARE\Wow6432Node\7-Zip-Zstandard'
    )) {
    $p = (Get-ItemProperty -Path $key -Name Path -ErrorAction SilentlyContinue).Path
    if ($p) {
      $exe = Join-Path $p '7z.exe'
      if (Test-Path $exe) { return $exe }
    }
  }
  throw '7z.exe not found'
}

function Get-Sha256Lower([string]$Path) {
  return (Get-FileHash -Path $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Resolve-SlimZip([string]$Preferred) {
  if ($Preferred -and (Test-Path $Preferred)) { return (Resolve-Path $Preferred).Path }
  $hits = @(Get-ChildItem -Path $OutDir -Filter 'Notepad_i18n_x64_*.zip' -File -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -notmatch 'with_WebView2' } |
    Sort-Object LastWriteTime -Descending)
  if ($hits.Count -eq 0) {
    throw "Slim zip Notepad_i18n_x64_*.zip not found under $OutDir. Run make_zip.bat first."
  }
  return $hits[0].FullName
}

function Find-MsEdgeWebView2([string]$Root) {
  $direct = Join-Path $Root 'msedgewebview2.exe'
  if (Test-Path $direct) { return $Root }
  $nested = Get-ChildItem -Path $Root -Directory -Filter 'Microsoft.WebView2.FixedVersionRuntime.*' -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'msedgewebview2.exe') } |
    Select-Object -First 1
  if ($nested) { return $nested.FullName }
  $any = Get-ChildItem -Path $Root -Recurse -Filter 'msedgewebview2.exe' -File -ErrorAction SilentlyContinue |
    Select-Object -First 1
  if ($any) { return $any.Directory.FullName }
  return $null
}

Write-Host "Reading pin: $PinPath"
$pin = Get-Content -Raw -Path $PinPath | ConvertFrom-Json
$version = [string]$pin.version
$cabName = [string]$pin.cab
$expectedHash = ([string]$pin.sha256).ToLowerInvariant().Trim()
$pinUrl = [string]$pin.url

if (-not $version -or -not $cabName) {
  throw 'Pin file missing version/cab'
}

$slimZipPath = Resolve-SlimZip $SlimZip
Write-Host "Slim zip: $slimZipPath"

$cacheDir = Join-Path $buildDir 'cache\webview2'
New-Item -ItemType Directory -Force -Path $cacheDir | Out-Null
$cabLocal = Join-Path $cacheDir $cabName

if (-not $CabPath) { $CabPath = $env:WEBVIEW2_FIXED_CAB }
$cabUrl = $env:WEBVIEW2_FIXED_CAB_URL
if (-not $cabUrl) { $cabUrl = $pinUrl }

if ($CabPath) {
  if (-not (Test-Path $CabPath)) { throw "WEBVIEW2_FIXED_CAB / -CabPath not found: $CabPath" }
  Write-Host "Using local cab: $CabPath"
  $srcFull = (Resolve-Path $CabPath).Path
  $dstFull = [System.IO.Path]::GetFullPath($cabLocal)
  if ($srcFull -ne $dstFull) {
    Copy-Item -Force $CabPath $cabLocal
  }
} elseif ($cabUrl) {
  Write-Host "Downloading cab from: $cabUrl"
  Invoke-WebRequest -Uri $cabUrl -OutFile $cabLocal -UseBasicParsing
} elseif (Test-Path $cabLocal) {
  Write-Host "Using cached cab: $cabLocal"
} else {
  throw @"
Fixed Version cab not available.
Acquire Microsoft.WebView2.FixedVersionRuntime.$version.x64.cab directly from Microsoft,
then either:
  - set pin.url to the Microsoft CDN URL and fill sha256, or
  - set WEBVIEW2_FIXED_CAB to the local cab path, or
  - set WEBVIEW2_FIXED_CAB_URL to a Microsoft download URL
"@
}

$actualHash = Get-Sha256Lower $cabLocal
Write-Host "Cab SHA256: $actualHash"
if ($expectedHash) {
  if ($actualHash -ne $expectedHash) {
    throw "SHA256 mismatch for $cabName. Expected $expectedHash, got $actualHash"
  }
} else {
  Write-Host "WARNING: pin.sha256 is empty; write this hash into webview2-fixed-version.json: $actualHash"
}

$workRoot = Join-Path $buildDir ('temp_webview2_' + [guid]::NewGuid().ToString('N'))
$expandRoot = Join-Path $workRoot 'expand'
$stageRoot = Join-Path $workRoot 'stage'
New-Item -ItemType Directory -Force -Path $expandRoot, $stageRoot | Out-Null

try {
  Write-Host 'Expanding cab (preserve LICENSE / ThirdPartyNotices)...'
  & expand.exe $cabLocal "-F:*" $expandRoot | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "expand.exe failed with exit $LASTEXITCODE" }

  $runtimeSrc = Find-MsEdgeWebView2 $expandRoot
  if (-not $runtimeSrc) {
    throw 'msedgewebview2.exe not found after expand'
  }
  Write-Host "Runtime folder: $runtimeSrc"

  $sevenZip = Find-SevenZip
  Write-Host "Extracting slim zip with $sevenZip"
  & $sevenZip x -y "-o$stageRoot" $slimZipPath | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "7z extract failed: $LASTEXITCODE" }

  $webViewDest = Join-Path $stageRoot 'WebView2'
  if (Test-Path $webViewDest) { Remove-Item -Recurse -Force $webViewDest }
  New-Item -ItemType Directory -Force -Path $webViewDest | Out-Null
  Copy-Item -Path (Join-Path $runtimeSrc '*') -Destination $webViewDest -Recurse -Force

  if (-not (Test-Path (Join-Path $webViewDest 'msedgewebview2.exe'))) {
    throw 'WebView2 staging missing msedgewebview2.exe'
  }

  $readme = @"
WebView2 Fixed Version Runtime (bundled)
========================================

Version: $version (x64)
Folder:  WebView2\

This portable build includes the Microsoft Edge WebView2 Fixed Version Runtime
so Preview Mode works without a system Evergreen install.

Target compatibility: Windows 10 build 19041 / 19044 and nearby, matching the
vendored WebView2 SDK 1.0.2903.40 used by this project.

Licenses and third-party notices from Microsoft ship inside this WebView2 folder
(LICENSE / ThirdPartyNotices / show_third_party_software_licenses.bat when present).
See also License.txt in the application root.

Do not redistribute the WebView2 folder as a stand-alone runtime package.
"@
  Set-Content -Path (Join-Path $stageRoot 'NOTICE-WebView2.txt') -Value $readme -Encoding UTF8

  $baseName = [System.IO.Path]::GetFileNameWithoutExtension($slimZipPath)
  # Notepad_i18n_x64_vX -> Notepad_i18n_x64_with_WebView2_vX
  $fatName = $baseName -replace '^(Notepad(?:_i18n)?_x64)_', '${1}_with_WebView2_'
  if ($fatName -eq $baseName) {
    $fatName = $baseName + '_with_WebView2'
  }
  $fatZip = Join-Path $OutDir ($fatName + '.zip')
  if (Test-Path $fatZip) { Remove-Item -Force $fatZip }

  Write-Host "Creating $fatZip"
  Push-Location $stageRoot
  try {
    & $sevenZip a -tzip -mx=9 $fatZip '*' | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "7z pack failed: $LASTEXITCODE" }
  } finally {
    Pop-Location
  }

  Write-Host "OK: $fatZip"
  Write-Output $fatZip
} finally {
  if (Test-Path $workRoot) {
    Remove-Item -Recurse -Force $workRoot -ErrorAction SilentlyContinue
  }
}
