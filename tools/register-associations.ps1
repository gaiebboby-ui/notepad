#Requires -Version 5.1
<#
.SYNOPSIS
  Registers file associations and a context-menu entry for Notepad (fork).

.DESCRIPTION
  For the current user (HKCU, no admin required):
    - ProgId Notepad.document with open command and icon
    - Default handler for safe text types; context menu for scripts and HTML (edit in Notepad)
  - No default association: executables, auto-run scripts (.bat, .ps1, .ahk, …), HTML (.html, .htm, …)
  - No context menu: binaries only (.exe, .dll, .lnk, …)

  Mirrors the in-app System Integration logic (see src/Dialogs.cpp, src/config.h).
.PARAMETER ExePath
  Full path to Notepad.exe

.PARAMETER Extended
  Use all single-token extensions parsed from doc/FileExt.txt (in addition to the core set)

.PARAMETER Unregister
  Remove registry entries created by this script

.PARAMETER RegisterDefaultApps
  Also register in HKLM "Default apps" (requires Administrator). Optional.

.EXAMPLE
  .\register-associations.ps1 -ExePath "C:\Apps\Notepad\Notepad.exe" -Extended
#>
[CmdletBinding()]
param(
	[Parameter()]
	[string]$ExePath,

	[Parameter()]
	[string]$ContextMenuLabel,

	[Parameter()]
	[string]$ProgId = 'Notepad.document',

	[Parameter()]
	[string]$FriendlyName = 'Notepad document',

	[Parameter()]
	[switch]$Extended,

	[Parameter()]
	[switch]$Unregister,

	[Parameter()]
	[switch]$RegisterDefaultApps
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Script:CoreExtensions = @(
	'txt', 'log', 'ini', 'md', 'json', 'xml', 'csv',
	'yaml', 'yml', 'cfg', 'conf', 'ts', 'css'
)

# Context menu only (no double-click default) — still opened via "Open in Notepad".
$Script:ContextMenuOnlyExtensions = @(
	'html', 'htm', 'shtml', 'xhtml', 'mht', 'mhtml',
	'asp', 'aspx', 'jsp', 'vue', 'hbs', 'svelte', 'cfm', 'tpl', 'jd', 'htc'
)

# No default handler (scripts / side-effect types) — context menu remains.
$Script:NoDefaultExtensions = [System.Collections.Generic.HashSet[string]]::new(
	[StringComparer]::OrdinalIgnoreCase
)
@(
	$Script:ContextMenuOnlyExtensions
	# CMD & batch
	'bat', 'cmd',
	# Windows Script Host
	'vbs', 'vbe', 'vb', 'js', 'jse', 'ws', 'wsc', 'wsf', 'wsh', 'msc', 'hta', 'sct', 'shb', 'shs', 'scf',
	# PowerShell
	'ps1', 'psm1', 'psc1', 'psd1', 'ps1xml', 'cdxml', 'ps2', 'ps2xml', 'workflow',
	'msh', 'msh1', 'msh2', 'mshxml', 'msh1xml', 'msh2xml',
	# AutoHotkey / AutoIt
	'ahk', 'ia', 'au3',
	# Python / Ruby / Perl
	'py', 'pyw', 'pyc', 'pyo', 'pyz', 'pyzw', 'rb', 'rbw', 'pl', 'plx', 'pm',
	# Shell / Node
	'sh', 'bash', 'zsh', 'fish', 'ksh', 'csh', 'mjs', 'cjs',
	# Registry / driver setup
	'reg', 'inf',
	# PHP
	'php', 'phps', 'phtml', 'phpt'
) | ForEach-Object { [void]$Script:NoDefaultExtensions.Add($_) }

# No association at all (no default, no context menu).
$Script:NoMenuExtensions = [System.Collections.Generic.HashSet[string]]::new(
	[StringComparer]::OrdinalIgnoreCase
)
@(
	'exe', 'com', 'scr', 'pif', 'msi', 'msp', 'msu', 'dll', 'ocx', 'sys', 'drv', 'cpl',
	'application', 'gadget', 'diagcab', 'diagpkg', 'appx', 'msix', 'appxbundle', 'msixbundle', 'appref-ms',
	'lnk', 'url', 'website', 'webloc',
	'jar', 'class', 'jnlp', 'war', 'ear',
	'app', 'action', 'command', 'osx', 'ipa', 'apk',
	'deb', 'rpm', 'appimage',
	'bin', 'elf', 'o', 'obj', 'lib', 'so', 'dylib', 'wasm',
	'ade', 'adp', 'asa', 'asax', 'ashx', 'asm', 'asmx', 'bas', 'cer', 'chm',
	'crt', 'der', 'fxp', 'grp', 'hlp', 'hvx', 'its', 'mdb', 'mde', 'mmc',
	'mof', 'ops', 'osd', 'pcd', 'plg', 'prc', 'prg', 'printerexport', 'pst', 'vbp',
	'vhd', 'vhdx', 'vsmacros', 'vsw', 'xll', 'xnk'
) | ForEach-Object { [void]$Script:NoMenuExtensions.Add($_) }

function Test-SkipDefaultAssociation {
	param([string]$Extension)
	$ext = $Extension.TrimStart('.')
	return $Script:NoDefaultExtensions.Contains($ext) -or $Script:NoMenuExtensions.Contains($ext)
}

function Test-SkipContextMenu {
	param([string]$Extension)
	return $Script:NoMenuExtensions.Contains($Extension.TrimStart('.'))
}

function Resolve-NotepadExe {
	param([string]$Path)
	if (-not [string]::IsNullOrWhiteSpace($Path)) {
		if (-not (Test-Path -LiteralPath $Path)) {
			throw "Notepad.exe not found: $Path"
		}
		return (Resolve-Path -LiteralPath $Path).Path
	}
	$sameDir = Join-Path $PSScriptRoot 'Notepad.exe'
	if (Test-Path -LiteralPath $sameDir) {
		return (Resolve-Path -LiteralPath $sameDir).Path
	}
	if ($env:NOTEPAD_EXE -and (Test-Path -LiteralPath $env:NOTEPAD_EXE)) {
		return (Resolve-Path -LiteralPath $env:NOTEPAD_EXE).Path
	}
	throw "Notepad.exe not found next to this script. Expected: $sameDir"
}

function Test-Administrator {
	$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
	$principal = [Security.Principal.WindowsPrincipal]$identity
	return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-ExtensionsFromFileExtTxt {
	param([string]$FilePath)

	if (-not (Test-Path -LiteralPath $FilePath)) {
		Write-Warning "File not found: $FilePath"
		return @()
	}

	$set = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
	foreach ($line in [System.IO.File]::ReadLines($FilePath)) {
		if ($line -notmatch '^\t+') { continue }
		if ($line -match '[\*\[\]]') { continue }
		if ($line -notmatch '^\t+\s*([A-Za-z0-9][\w\-]*)') { continue }
		$ext = $Matches[1]
		if ($ext.Length -lt 1) { continue }
		[void]$set.Add($ext.TrimStart('.'))
	}
	return @($set) | Sort-Object
}

function Get-AllCandidateExtensions {
	$list = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
	foreach ($ext in $Script:CoreExtensions) { [void]$list.Add($ext) }
	foreach ($ext in $Script:ContextMenuOnlyExtensions) { [void]$list.Add($ext) }
	$fileExt = Join-Path $PSScriptRoot 'FileExt.txt'
	if (-not (Test-Path -LiteralPath $fileExt)) {
		$fileExt = Join-Path $PSScriptRoot '..\doc\FileExt.txt'
	}
	if ($Extended -and (Test-Path -LiteralPath $fileExt)) {
		foreach ($ext in (Get-ExtensionsFromFileExtTxt -FilePath $fileExt)) {
			[void]$list.Add($ext)
		}
	}
	return @($list) | Sort-Object
}

function Get-CleanupExtensions {
	$list = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
	foreach ($ext in (Get-AllCandidateExtensions)) { [void]$list.Add($ext) }
	foreach ($ext in $Script:NoDefaultExtensions) { [void]$list.Add($ext) }
	foreach ($ext in $Script:NoMenuExtensions) { [void]$list.Add($ext) }
	$fileExt = Join-Path $PSScriptRoot 'FileExt.txt'
	if (-not (Test-Path -LiteralPath $fileExt)) {
		$fileExt = Join-Path $PSScriptRoot '..\doc\FileExt.txt'
	}
	if (Test-Path -LiteralPath $fileExt) {
		foreach ($ext in (Get-ExtensionsFromFileExtTxt -FilePath $fileExt)) {
			[void]$list.Add($ext)
		}
	}
	return @($list) | Sort-Object
}

function Get-TargetExtensions {
	$skippedMenu = [System.Collections.Generic.List[string]]::new()
	$defaultOnly = [System.Collections.Generic.List[string]]::new()
	$withMenu = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
	foreach ($ext in (Get-AllCandidateExtensions)) {
		if (Test-SkipContextMenu -Extension $ext) {
			$skippedMenu.Add($ext) | Out-Null
			continue
		}
		[void]$withMenu.Add($ext)
		if (Test-SkipDefaultAssociation -Extension $ext) {
			$defaultOnly.Add($ext) | Out-Null
		}
	}
	$script:LastSkippedMenuExtensions = $skippedMenu
	$script:LastMenuOnlyExtensions = $defaultOnly
	return @($withMenu) | Sort-Object
}

function Get-OpenCommand {
	param([string]$Path)
	return "`"$Path`" `"%1`""
}

function Set-RegistryDefaultString {
	param(
		[Microsoft.Win32.RegistryKey]$Key,
		[string]$Value
	)
	if ($null -eq $Value) {
		$Key.DeleteValue('', $false)
	} else {
		$Key.SetValue('', $Value, [Microsoft.Win32.RegistryValueKind]::String)
	}
}

function Remove-RegistryTree {
	param(
		[Microsoft.Win32.RegistryHive]$Hive,
		[string]$SubKey
	)
	$root = [Microsoft.Win32.Registry]::$Hive
	try {
		$root.DeleteSubKeyTree($SubKey, $false)
	} catch {
		# ignore missing keys
	}
}

function Invoke-AssociationRefresh {
	Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class ShellNotify {
	[DllImport("shell32.dll")]
	public static extern void SHChangeNotify(int eventId, uint flags, IntPtr item1, IntPtr item2);
	public static void NotifyAssociationChanged() {
		SHChangeNotify(0x08000000, 0x0000, IntPtr.Zero, IntPtr.Zero);
	}
}
"@ -ErrorAction SilentlyContinue | Out-Null
	[ShellNotify]::NotifyAssociationChanged()
}

function Register-ProgId {
	param(
		[string]$Path,
		[string]$IconPath,
		[string]$OpenCommand
	)
	$progKey = "Software\Classes\$ProgId"
	$k = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($progKey)
	Set-RegistryDefaultString -Key $k -Value $FriendlyName
	$k.SetValue('FriendlyTypeName', $FriendlyName, [Microsoft.Win32.RegistryValueKind]::String)
	$k.SetValue('DefaultIcon', $IconPath, [Microsoft.Win32.RegistryValueKind]::String)
	$k.Close()

	$cmdKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("$progKey\shell\open\command")
	Set-RegistryDefaultString -Key $cmdKey -Value $OpenCommand
	$cmdKey.Close()
}

function Set-ExtensionDefault {
	param([string]$Extension)
	$extKey = "Software\Classes\.$Extension"
	$k = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($extKey)
	Set-RegistryDefaultString -Key $k -Value $ProgId
	$k.Close()
}

function Add-ExtensionContextMenu {
	param(
		[string]$Extension,
		[string]$Label,
		[string]$IconPath,
		[string]$OpenCommand
	)
	$shellKey = "Software\Classes\SystemFileAssociations\.$Extension\shell\OpenInNotepad"
	$k = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($shellKey)
	Set-RegistryDefaultString -Key $k -Value $Label
	$k.SetValue('Icon', $IconPath, [Microsoft.Win32.RegistryValueKind]::String)
	$k.Close()

	$cmdKey = [Microsoft.Win32.Registry]::CurrentUser.CreateSubKey("$shellKey\command")
	Set-RegistryDefaultString -Key $cmdKey -Value $OpenCommand
	$cmdKey.Close()
}

function Register-DefaultPrograms {
	param(
		[string]$Path,
		[string]$Associations
	)
	if (-not (Test-Administrator)) {
		throw 'RegisterDefaultApps requires Administrator. Re-run elevated or omit -RegisterDefaultApps.'
	}

	$capabilities = 'Software\Notepad\Capabilities'
	$registered = 'Software\RegisteredApplications'

	$rk = [Microsoft.Win32.Registry]::LocalMachine.CreateSubKey($registered, [Microsoft.Win32.RegistryKeyPermissionCheck]::ReadWriteSubTree, [System.Security.AccessControl.RegistryRights]::FullControl)
	$rk.SetValue('Notepad.exe', $capabilities, [Microsoft.Win32.RegistryValueKind]::String)
	$rk.Close()

	$ck = [Microsoft.Win32.Registry]::LocalMachine.CreateSubKey($capabilities)
	$ck.SetValue('ApplicationName', $FriendlyName, [Microsoft.Win32.RegistryValueKind]::String)
	$ck.SetValue('ApplicationDescription', $FriendlyName, [Microsoft.Win32.RegistryValueKind]::String)
	$ck.SetValue('FileAssociations', $Associations, [Microsoft.Win32.RegistryValueKind]::String)
	$ck.Close()

	# ProgId under HKCR (64-bit view)
	$openCommand = Get-OpenCommand -Path $Path
	$progCr = "Notepad.document"
	$pk = [Microsoft.Win32.Registry]::ClassesRoot.CreateSubKey($progCr)
	Set-RegistryDefaultString -Key $pk -Value $FriendlyName
	$pk.SetValue('DefaultIcon', $Path, [Microsoft.Win32.RegistryValueKind]::String)
	$pk.Close()
	$cmdk = [Microsoft.Win32.Registry]::ClassesRoot.CreateSubKey("$progCr\shell\open\command")
	Set-RegistryDefaultString -Key $cmdk -Value $openCommand
	$cmdk.Close()
}

function Remove-StaleDefaultAssociation {
	param([string]$Extension)
	$extKey = "Software\Classes\.$Extension"
	try {
		$k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($extKey, $true)
		if ($null -ne $k) {
			$current = $k.GetValue('')
			if ($current -eq $ProgId) {
				$k.DeleteValue('')
			}
			$k.Close()
		}
	} catch { }
}

function Remove-StaleContextMenu {
	param([string]$Extension)
	Remove-RegistryTree -Hive 'CurrentUser' -SubKey "Software\Classes\SystemFileAssociations\.$Extension\shell\OpenInNotepad"
}

function Remove-StaleExtensionRegistration {
	param([string]$Extension)
	Remove-StaleDefaultAssociation -Extension $Extension
	Remove-StaleContextMenu -Extension $Extension
}

function Unregister-Associations {
	param([string[]]$Extensions)

	Remove-RegistryTree -Hive 'CurrentUser' -SubKey "Software\Classes\$ProgId"
	Remove-RegistryTree -Hive 'CurrentUser' -SubKey 'Software\Classes\*\shell\OpenInNotepad'

	foreach ($ext in $Extensions) {
		$extKey = "Software\Classes\.$ext"
		try {
			$k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($extKey, $true)
			if ($null -ne $k) {
				$current = $k.GetValue('')
				if ($current -eq $ProgId) {
					$k.DeleteValue('')
				}
				$k.Close()
			}
		} catch { }
		Remove-RegistryTree -Hive 'CurrentUser' -SubKey "Software\Classes\SystemFileAssociations\.$ext\shell\OpenInNotepad"
	}

	if (Test-Administrator) {
		try {
			$rk = [Microsoft.Win32.Registry]::LocalMachine.OpenSubKey('Software\RegisteredApplications', $true)
			if ($null -ne $rk) {
				$rk.DeleteValue('Notepad.exe', $false)
				$rk.Close()
			}
		} catch { }
		Remove-RegistryTree -Hive 'LocalMachine' -SubKey 'Software\Notepad\Capabilities'
		Remove-RegistryTree -Hive 'ClassesRoot' -SubKey 'Notepad.document'
	}

	Invoke-AssociationRefresh
	Write-Host 'Associations removed.' -ForegroundColor Yellow
}

# --- main ---
if ([string]::IsNullOrWhiteSpace($ContextMenuLabel)) {
	$ContextMenuLabel = (-join [char[]](0x041E, 0x0442, 0x043A, 0x0440, 0x044B, 0x0442, 0x044C, 0x0020, 0x0432, 0x0020)) + 'Notepad'
}

$exe = Resolve-NotepadExe -Path $ExePath
$icon = "`"$exe`",0"
$openCommand = Get-OpenCommand -Path $exe
$extensions = Get-TargetExtensions

if ($Unregister) {
	Unregister-Associations -Extensions (Get-CleanupExtensions)
	return
}

$skippedCount = 0
$menuOnlyCount = 0
if ($null -ne $script:LastSkippedMenuExtensions) {
	$skippedCount = $script:LastSkippedMenuExtensions.Count
}
if ($null -ne $script:LastMenuOnlyExtensions) {
	$menuOnlyCount = $script:LastMenuOnlyExtensions.Count
}

Write-Host "Notepad: $exe"
Write-Host "Extensions: $($extensions.Count) ($(if ($Extended) { 'extended' } else { 'core' })), menu-only $menuOnlyCount, skipped $skippedCount binary"
Write-Host "ProgId: $ProgId"
Write-Host ''

foreach ($ext in $Script:NoMenuExtensions) {
	Remove-StaleExtensionRegistration -Extension $ext
}
foreach ($ext in $Script:NoDefaultExtensions) {
	Remove-StaleDefaultAssociation -Extension $ext
}

Register-ProgId -Path $exe -IconPath $icon -OpenCommand $openCommand

$i = 0
$defaultCount = 0
foreach ($ext in $extensions) {
	Add-ExtensionContextMenu -Extension $ext -Label $ContextMenuLabel -IconPath $icon -OpenCommand $openCommand
	if (-not (Test-SkipDefaultAssociation -Extension $ext)) {
		Set-ExtensionDefault -Extension $ext
		$defaultCount++
	} else {
		Remove-StaleDefaultAssociation -Extension $ext
	}
	$i++
	if ($i % 50 -eq 0) {
		Write-Host "  ... $i / $($extensions.Count)"
	}
}

if ($RegisterDefaultApps) {
	$assoc = ($extensions | Where-Object { -not (Test-SkipDefaultAssociation -Extension $_) } | ForEach-Object { ".$_" }) -join ';'
	Register-DefaultPrograms -Path $exe -Associations $assoc
}

Invoke-AssociationRefresh

Write-Host ''
Write-Host "Done. Context menu: $i extensions; default handler: $defaultCount." -ForegroundColor Green
Write-Host "Scripts/HTML: context menu only (no double-click default)."
Write-Host ''
Write-Host 'Tip: run with -Extended to use FileExt.txt in this folder.'
Write-Host 'Tip: Settings -> Apps -> Default apps -> Choose defaults by file type, if Windows still uses another app.'
