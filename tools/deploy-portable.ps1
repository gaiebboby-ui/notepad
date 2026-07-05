#Requires -Version 5.1
<#
.SYNOPSIS
  Copy portable Notepad files and/or association scripts to a target folder.

.DESCRIPTION
  Scripts (.bat, .ps1) are copied as separate files for direct execution by Windows.
  Paths inside register-associations.ps1 assume all files live in the same directory as Notepad.exe.

.PARAMETER TargetDir
  Destination folder (portable install root).

.PARAMETER SourceDir
  Repo root. Defaults to parent of tools\.

.PARAMETER ScriptsOnly
  Only refresh License.txt, FileExt.txt, and association scripts (no exe/locale).
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory)]
	[string]$TargetDir,

	[Parameter()]
	[string]$SourceDir,

	[Parameter()]
	[switch]$ScriptsOnly
)

$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
	$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
}

function Copy-PortableScripts {
	param([string]$Dest, [string]$ToolsDir, [string]$Root)
	New-Item -ItemType Directory -Path $Dest -Force | Out-Null
	Copy-Item -Path (Join-Path $Root 'License.txt') -Destination $Dest -Force
	Copy-Item -Path (Join-Path $Root 'doc\FileExt.txt') -Destination $Dest -Force
	foreach ($name in @(
		'register-associations.ps1',
		'register-associations.bat',
		'unregister-associations.bat',
		'deploy-portable.ps1'
	)) {
		Copy-Item -Path (Join-Path $ToolsDir $name) -Destination $Dest -Force
	}
}

New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null

if ($ScriptsOnly) {
	Copy-PortableScripts -Dest $TargetDir -ToolsDir $PSScriptRoot -Root $SourceDir
	Write-Host "Scripts refreshed: $TargetDir"
	return
}

$buildX64 = Join-Path $SourceDir 'build\bin\Release\x64'
$zip = Get-ChildItem -Path (Join-Path $SourceDir 'build') -Filter 'Notepad_i18n_x64_*.zip' -ErrorAction SilentlyContinue |
	Sort-Object LastWriteTime -Descending | Select-Object -First 1

if (-not (Test-Path -LiteralPath $buildX64) -and $null -eq $zip) {
	$release = Join-Path $SourceDir 'release-temp\Notepad_i18n_x64_*.zip'
	$zip = Get-ChildItem -Path $release -ErrorAction SilentlyContinue | Select-Object -First 1
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("notepad-portable-" + [guid]::NewGuid().ToString('n'))

try {
	if (Test-Path -LiteralPath (Join-Path $buildX64 'Notepad.exe')) {
		Write-Host "Copy from build: $buildX64"
		Copy-Item -Path (Join-Path $buildX64 'Notepad.exe') -Destination $TargetDir -Force
		Copy-Item -Path (Join-Path $buildX64 'matepath.exe') -Destination $TargetDir -Force
		$locale = Join-Path $buildX64 'locale'
		if (Test-Path -LiteralPath $locale) {
			Copy-Item -Path $locale -Destination $TargetDir -Recurse -Force
		}
		Copy-Item -Path (Join-Path $SourceDir 'doc\Notepad.ini') -Destination (Join-Path $TargetDir 'Notepad.ini-default') -Force
		Copy-Item -Path (Join-Path $SourceDir 'matepath\doc\matepath.ini') -Destination (Join-Path $TargetDir 'matepath.ini-default') -Force
		Copy-Item -Path (Join-Path $SourceDir 'doc\Notepad DarkTheme.ini') -Destination $TargetDir -Force
		Copy-Item -Path (Join-Path $SourceDir 'doc\Notepad DarkTheme.ini') -Destination (Join-Path $TargetDir 'Notepad DarkTheme.ini-default') -Force
	} elseif ($null -ne $zip) {
		Write-Host "Extract from zip: $($zip.FullName)"
		Expand-Archive -LiteralPath $zip.FullName -DestinationPath $temp -Force
		Get-ChildItem -LiteralPath $temp | Copy-Item -Destination $TargetDir -Recurse -Force
	} else {
		throw "No build output or Notepad_i18n_x64 zip found under $SourceDir. Use -ScriptsOnly to refresh scripts only."
	}

	Copy-PortableScripts -Dest $TargetDir -ToolsDir $PSScriptRoot -Root $SourceDir
	Write-Host "Portable install ready: $TargetDir"
} finally {
	if (Test-Path -LiteralPath $temp) {
		Remove-Item -LiteralPath $temp -Recurse -Force -ErrorAction SilentlyContinue
	}
}
