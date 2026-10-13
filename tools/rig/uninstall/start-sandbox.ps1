<#
.SYNOPSIS
The uninstall check on a clean machine (T-093, T-105, ROADMAP M10 "Uninstall"): writes a Windows Sandbox
configuration and opens it; uninstall-check.ps1 runs inside at logon and writes its evidence to -Evidence.

.DESCRIPTION
The sandbox is a fresh Windows each time, with no Steam and no game. The release zip's folder (-Package) and this
script folder are mapped read-only, -Evidence writable; networking is off, so the launcher's update check cannot reach
anything. Inside, uninstall-check.ps1 checks the zip against its .sha256 and SHA256SUMS.txt, extracts it, opens the
launcher, runs --dry-run, plants and lets the launcher remove a stale HKCU layer registration, follows INSTALL.md's
uninstall steps, and diffs a snapshot after each step against the one taken before the install
(uninstall-snapshot.ps1). It is finished when <Evidence>\DONE exists; close the sandbox window then (nothing in it is
kept).

Needs the Windows optional feature Containers-DisposableClientVM (Windows 10/11 Pro or Enterprise, virtualization on);
enabling it needs an elevated shell and a restart:
  Enable-WindowsOptionalFeature -Online -FeatureName Containers-DisposableClientVM -All -NoRestart

  powershell -NoProfile -ExecutionPolicy Bypass -File tools\rig\uninstall\start-sandbox.ps1 -Package <dir with the zip> -Evidence <dir>

.PARAMETER Package
The folder holding EternalVR-alpha-<version>-<commit>.zip and its .sha256 (the newest such zip is used).

.PARAMETER Evidence
Where the sandbox writes its run.log, snapshots and diffs (created; must be empty or absent).

.PARAMETER WaitMinutes
When above 0, wait up to that long for <Evidence>\DONE and print run.log; exit 0 when it appeared, 1 otherwise.
#>
param(
    [Parameter(Mandatory = $true)][string]$Package,
    [Parameter(Mandatory = $true)][string]$Evidence,
    [int]$WaitMinutes = 0
)
$ErrorActionPreference = 'Stop'
$sandbox = Join-Path $env:SystemRoot 'System32\WindowsSandbox.exe'
if (-not (Test-Path -LiteralPath $sandbox)) {
    throw "Windows Sandbox is not available ($sandbox missing): enable Containers-DisposableClientVM (elevated) and restart"
}
$Package = (Resolve-Path -LiteralPath $Package).Path
if (-not (Get-ChildItem -LiteralPath $Package -Filter 'EternalVR-alpha-*.zip' | Where-Object { $_.Name -notlike '*-symbols*' })) {
    throw "no EternalVR-alpha-*.zip in $Package"
}
New-Item -ItemType Directory -Force $Evidence | Out-Null
$Evidence = (Resolve-Path -LiteralPath $Evidence).Path
if (Get-ChildItem -LiteralPath $Evidence -Force) { throw "$Evidence is not empty: use a new folder for each run" }

$esc = [Security.SecurityElement]
$wsb = @"
<Configuration>
  <Networking>Disable</Networking>
  <MappedFolders>
    <MappedFolder><HostFolder>$($esc::Escape($Package))</HostFolder><SandboxFolder>C:\pkg</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$($esc::Escape($PSScriptRoot))</HostFolder><SandboxFolder>C:\kit</SandboxFolder><ReadOnly>true</ReadOnly></MappedFolder>
    <MappedFolder><HostFolder>$($esc::Escape($Evidence))</HostFolder><SandboxFolder>C:\evidence</SandboxFolder><ReadOnly>false</ReadOnly></MappedFolder>
  </MappedFolders>
  <LogonCommand>
    <Command>powershell.exe -NoProfile -ExecutionPolicy Bypass -File C:\kit\uninstall-check.ps1 -Pkg C:\pkg -Evidence C:\evidence</Command>
  </LogonCommand>
</Configuration>
"@
# Outside the evidence folder, which the check expects empty; next to it.
$config = "$Evidence.wsb"
Set-Content -LiteralPath $config -Value $wsb -Encoding utf8
Write-Host "sandbox configuration $config; evidence $Evidence"
Start-Process -FilePath $sandbox -ArgumentList "`"$config`""
if ($WaitMinutes -le 0) { exit 0 }
$end = (Get-Date).AddMinutes($WaitMinutes)
while ((Get-Date) -lt $end -and -not (Test-Path -LiteralPath (Join-Path $Evidence 'DONE'))) { Start-Sleep -Seconds 10 }
$done = Test-Path -LiteralPath (Join-Path $Evidence 'DONE')
Get-Content -LiteralPath (Join-Path $Evidence 'run.log') -ErrorAction SilentlyContinue
Write-Host $(if ($done) { 'uninstall check finished' } else { "no DONE in $Evidence after $WaitMinutes min" })
exit $(if ($done) { 0 } else { 1 })
