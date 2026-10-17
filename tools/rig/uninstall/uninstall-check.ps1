# Uninstall check on a clean machine (T-093, T-105, ROADMAP M10 "Uninstall"), run inside Windows Sandbox by the
# .wsb file start-sandbox.ps1 writes (its logon command). DOOM Eternal and Steam are not in the sandbox, and networking is off.
# What runs, with a snapshot (uninstall-snapshot.ps1) after each step into C:\evidence\<nn-step>\:
#   00-pre             the clean machine
#   01  install        the release zip checked against its .sha256 and the files against SHA256SUMS.txt, then
#                      extracted to Downloads (INSTALL.md: unzip anywhere outside Program Files)
#   02  first-run      the launcher window opened (first run: no game, no Steam), screenshot, closed
#   03  dry-run        EternalVR.Launcher.exe --dry-run (preflight, settings snapshot, save backup, launch plan)
#   04  stale-hkcu     the launcher's own HKCU registration (its HkcuLayerRegistration.Register, called through
#                      reflection as a --register-hkcu session calls it: REGISTRATION_PENDING, any missing Khronos
#                      keys, the value) left as a session killed mid-way leaves it, then --dry-run: the start-up
#                      clean-up must remove the value, the marker and (launchers after eee2bb2) the keys it created
#   05  uninstall      INSTALL.md "Uninstalling": open the launcher once and close it, delete the unzipped folder,
#                      delete %LOCALAPPDATA%\EternalVR
#   diff-*.txt         every step against 00-pre; post-uninstall against 00-pre is the criterion
# -HostTest: a mechanics check on a non-clean PC: no window, no broad snapshots, a scratch --data-root, no file deleted
# outside the scratch folder. It does write the host's HKCU: step 04 registers a value (naming a manifest path that does
# not exist) under HKCU\SOFTWARE\Khronos\Vulkan\ImplicitLayers, creating any of the three Khronos keys that are
# missing; a finally block removes that exact value and those keys again (only the ones that were missing before and
# are empty), whatever the launcher's clean-up did.
# Without -HostTest it runs only as the sandbox's user (WDAGUtilityAccount): it exports whole hives, unzips into and
# deletes from Downloads, and deletes %LOCALAPPDATA%\EternalVR, none of which may happen on a real PC.
param([string]$Pkg = 'C:\pkg', [string]$Evidence = 'C:\evidence', [switch]$HostTest)
$ErrorActionPreference = 'Continue'
if (-not $HostTest -and $env:USERNAME -ne 'WDAGUtilityAccount') {
    Write-Error "refused: run inside Windows Sandbox (start-sandbox.ps1), or with -HostTest on any other PC (user '$env:USERNAME')"
    exit 2
}
. (Join-Path $PSScriptRoot 'uninstall-snapshot.ps1')
New-Item -ItemType Directory -Force $Evidence | Out-Null
$log = Join-Path $Evidence 'run.log'
function Say([string]$m) { $l = "$((Get-Date).ToString('HH:mm:ss')) $m"; Write-Host $l; Add-Content -LiteralPath $log -Value $l -Encoding utf8 }
$broad = -not $HostTest
$step = 0
function Snap([string]$Name) { $d = Join-Path $Evidence $Name; Save-EvrSnapshot -Dir $d -Broad:$broad; Say "snapshot $Name"; return $d }

Say "uninstall check; host test: $HostTest; user $env:USERNAME; $([Environment]::OSVersion.VersionString)"
$admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
$net = (Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full' -ErrorAction SilentlyContinue).Release
Say "elevated: $admin; .NET Framework release $net; vulkan-1.dll present: $(Test-Path "$env:SystemRoot\System32\vulkan-1.dll"); steam.exe running: $([bool](Get-Process steam -ErrorAction SilentlyContinue))"
$pre = Snap '00-pre'

# 01 install
$zip = Get-ChildItem -LiteralPath $Pkg -Filter 'EternalVR-alpha-*.zip' | Where-Object { $_.Name -notlike '*-symbols*' } | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $zip) { Say "FAIL no EternalVR-alpha-*.zip in $Pkg"; exit 1 }
$want = ((Get-Content -LiteralPath "$($zip.FullName).sha256" -ErrorAction SilentlyContinue) -split '\s+')[0]
$have = (Get-FileHash -LiteralPath $zip.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
Say "zip $($zip.Name): sha256 $have; published $want; match $($want -eq $have)"
$base = if ($HostTest) { Join-Path $Evidence 'host-install' } else { Join-Path $env:USERPROFILE 'Downloads' }
$dest = Join-Path $base ([IO.Path]::GetFileNameWithoutExtension($zip.Name))
if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
Expand-Archive -LiteralPath $zip.FullName -DestinationPath $base -Force
if (-not (Test-Path -LiteralPath (Join-Path $dest 'EternalVR.Launcher.exe'))) {
    # The zip may hold its files at the top level rather than in a folder of its own name.
    $found = Get-ChildItem -LiteralPath $base -Recurse -Filter 'EternalVR.Launcher.exe' | Select-Object -First 1
    if ($found) { $dest = $found.DirectoryName }
}
$exe = Join-Path $dest 'EternalVR.Launcher.exe'
Say "extracted to $dest; launcher present: $(Test-Path -LiteralPath $exe)"
$sums = Join-Path $dest 'SHA256SUMS.txt'
$bad = 0; $n = 0
foreach ($line in @(Get-Content -LiteralPath $sums -ErrorAction SilentlyContinue)) {
    if ($line -notmatch '^([0-9a-fA-F]{64})\s+\*?(.+)$') { continue }
    $n++
    $f = Join-Path $dest ($Matches[2].Trim() -replace '/', '\')
    if (-not (Test-Path -LiteralPath $f) -or (Get-FileHash -LiteralPath $f -Algorithm SHA256).Hash -ne $Matches[1].ToUpperInvariant()) { $bad++; Say "  SHA256SUMS mismatch: $($Matches[2])" }
}
Say "SHA256SUMS.txt: $n file(s), $bad mismatch(es)"
Get-ChildItem -LiteralPath $dest -Recurse -File | ForEach-Object { $_.FullName.Substring($dest.Length) } | Set-Content (Join-Path $Evidence 'package-files.txt') -Encoding utf8
$s1 = Snap '01-install'

$dataArgs = @()
$data = Join-Path $env:LOCALAPPDATA 'EternalVR'
if ($HostTest) { $data = Join-Path $Evidence 'host-data'; $dataArgs = @('--data-root', $data) }

function Invoke-Launcher([string[]]$LauncherArgs, [string]$Out) {
    $psi = New-Object Diagnostics.ProcessStartInfo $exe, ((@($LauncherArgs) + $dataArgs | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' ')
    $psi.UseShellExecute = $false; $psi.RedirectStandardOutput = $true; $psi.RedirectStandardError = $true
    $p = [Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync(); $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit(120000)) { $p.Kill(); Say "  killed after 120 s" }
    Set-Content -LiteralPath $Out -Value ($o.Result + "`n--- stderr`n" + $e.Result + "`n--- exit $($p.ExitCode)") -Encoding utf8
    return $p.ExitCode
}

# The window: wait, screenshot, list its windows (a dialog shows there), close it as a player would.
function Invoke-Window([string]$Tag, [int]$Seconds = 20) {
    # Windows PowerShell refuses an empty -ArgumentList, which is what the sandbox run has: pass it only when set.
    $start = @{ FilePath = $exe; PassThru = $true }
    if ($dataArgs.Count) { $start.ArgumentList = $dataArgs }
    $p = Start-Process @start
    if (-not $p) { Say "FAIL $Tag window: the launcher did not start"; exit 1 }
    Start-Sleep -Seconds $Seconds
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    $b = [Windows.Forms.SystemInformation]::VirtualScreen
    $bmp = New-Object Drawing.Bitmap $b.Width, $b.Height
    $g = [Drawing.Graphics]::FromImage($bmp)
    try { $g.CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size); $bmp.Save((Join-Path $Evidence "$Tag.png"), [Drawing.Imaging.ImageFormat]::Png) } finally { $g.Dispose(); $bmp.Dispose() }
    $titles = @(Get-Process -Id $p.Id -ErrorAction SilentlyContinue | ForEach-Object { $_.MainWindowTitle })
    Say "  $Tag window: pid $($p.Id), main window '$($titles -join ', ')', running $(-not $p.HasExited)"
    if (-not $p.HasExited) { [void]$p.CloseMainWindow(); [void]$p.WaitForExit(15000) }
    if (-not $p.HasExited) { Say "  $Tag window did not close in 15 s (a dialog?); stopped"; Stop-Process -Id $p.Id -Force; Start-Sleep 2 }
    Say "  $Tag closed; exit $($p.ExitCode)"
    Get-Process EternalVR.Launcher -ErrorAction SilentlyContinue | ForEach-Object { Say "  left running: EternalVR.Launcher $($_.Id)" }
}

# 02 first run (the window)
if (-not $HostTest) { Say 'first run: the window'; Invoke-Window '02-first-run' 25 }
$s2 = Snap '02-first-run'

# 03 dry run
$code = Invoke-Launcher @('--dry-run') (Join-Path $Evidence '03-dry-run.txt')
Say "--dry-run exit $code"
$s3 = Snap '03-dry-run'

# 04 a stale HKCU registration (what a --register-hkcu session killed mid-way leaves), removed by the next start
$manifest = if ($HostTest) { Join-Path $Evidence 'no-such-folder\VK_LAYER_ETERNALVR.json' } else { Join-Path $dest 'layer\VK_LAYER_ETERNALVR.json' }
New-Item -ItemType Directory -Force $data | Out-Null
$khronosKeys = @('SOFTWARE\Khronos', 'SOFTWARE\Khronos\Vulkan', 'SOFTWARE\Khronos\Vulkan\ImplicitLayers')
function Test-HkcuKey([string]$Key) { $k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($Key); if ($k) { $k.Close(); return $true }; return $false }
$missingBefore = @($khronosKeys | Where-Object { -not (Test-HkcuKey $_) })
Say "Khronos keys missing before the registration: $(if ($missingBefore.Count) { $missingBefore -join ', ' } else { 'none' })"
$markerLines = @()
# In a process of its own: loading the launcher's assembly locks its files until the process ends (step 05 deletes them).
$regScript = @'
param($Exe, $Marker, $Manifest)
try {
    $type = [Reflection.Assembly]::LoadFrom($Exe).GetType('EternalVR.Launcher.Platform.HkcuLayerRegistration', $true)
    $logged = New-Object System.Collections.Generic.List[string]
    # (marker, log) and, after eee2bb2, an optional layers key: the defaults filled in, as a C# call gets them.
    $ctor = $type.GetConstructors()[0]
    $ctorArgs = @($Marker, [Action[string]] { param($m) $logged.Add($m) }) + @($ctor.GetParameters() | Select-Object -Skip 2 | ForEach-Object { $_.DefaultValue })
    $reg = $ctor.Invoke([object[]]$ctorArgs)
    [void]$type.GetMethod('Register').Invoke($reg, @($Manifest))
    "registered through the launcher's HkcuLayerRegistration: $($logged -join '; ')"
} catch { "FAIL the launcher's registration could not be called: $($_.Exception.InnerException.Message) $($_.Exception.Message)" }
'@
$regFile = Join-Path $Evidence 'register-hkcu.ps1'
Set-Content -LiteralPath $regFile -Value $regScript -Encoding utf8
try {
    Say ((& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $regFile -Exe $exe -Marker (Join-Path $data 'REGISTRATION_PENDING') -Manifest $manifest) -join ' ')
    $markerLines = @(Get-Content -LiteralPath (Join-Path $data 'REGISTRATION_PENDING') -ErrorAction SilentlyContinue)
    Say "marker: $($markerLines -join ' | ')"
    $s4a = Snap '04a-stale-planted'
    $code = Invoke-Launcher @('--dry-run') (Join-Path $Evidence '04-stale-dry-run.txt')
    $k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('SOFTWARE\Khronos\Vulkan\ImplicitLayers')
    $left = if ($k) { $k.GetValue($manifest) } else { $null }
    if ($k) { $k.Close() }
    Say "after --dry-run (exit $code): HKCU value left: $($null -ne $left); marker left: $(Test-Path (Join-Path $data 'REGISTRATION_PENDING')); log line: $([bool](Select-String -LiteralPath (Join-Path $Evidence '04-stale-dry-run.txt') -SimpleMatch 'removed the HKCU layer registration' -Quiet))"
    Say "Khronos keys after the launcher's clean-up: $((@($khronosKeys | ForEach-Object { "$_ $(if (Test-HkcuKey $_) { 'present' } else { 'absent' })" })) -join ', ')"
} finally {
    if ($HostTest) {
        # The host's HKCU back as it was: the exact value, then the Khronos keys that were missing before or that the
        # marker says the registration created, deepest first and only when empty.
        $k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('SOFTWARE\Khronos\Vulkan\ImplicitLayers', $true)
        if ($k) {
            if ($null -ne $k.GetValue($manifest)) { $k.DeleteValue($manifest, $false); Say "host clean-up: removed the value $manifest (the launcher left it)" }
            $k.Close()
        }
        $recorded = @($markerLines | Where-Object { $_ -like 'created key: *' } | ForEach-Object { $_.Substring(13).Trim() })
        $own = @($khronosKeys | Where-Object { $missingBefore -contains $_ -or $recorded -contains $_ })
        [array]::Reverse($own)
        foreach ($key in $own) {
            $k = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($key)
            if (-not $k) { continue }
            $empty = $k.ValueCount -eq 0 -and $k.SubKeyCount -eq 0
            $k.Close()
            if ($empty) { [Microsoft.Win32.Registry]::CurrentUser.DeleteSubKey($key, $false); Say "host clean-up: removed the empty key HKCU\$key (missing before this run)" }
            else { Say "host clean-up: HKCU\$key kept (not empty)" }
        }
    }
}
$s4 = Snap '04-stale-cleaned'

# 05 uninstall as INSTALL.md says
if (-not $HostTest) { Say 'uninstall step 1: open the launcher once and close it'; Invoke-Window '05-uninstall-open' 15 }
Remove-Item -LiteralPath $dest -Recurse -Force -ErrorAction SilentlyContinue
Say "uninstall step 2: deleted $dest (left: $(Test-Path -LiteralPath $dest))"
Remove-Item -LiteralPath $data -Recurse -Force -ErrorAction SilentlyContinue
Say "uninstall step 3: deleted $data (left: $(Test-Path -LiteralPath $data))"
if ($HostTest -and (Test-Path -LiteralPath (Join-Path $Evidence 'host-install'))) { Remove-Item -LiteralPath (Join-Path $Evidence 'host-install') -Recurse -Force }
$s5 = Snap '05-post-uninstall'

foreach ($pair in @(@('01-install', $s1), @('02-first-run', $s2), @('03-dry-run', $s3), @('04a-stale-planted', $s4a), @('04-stale-cleaned', $s4), @('05-post-uninstall', $s5))) {
    $r = Compare-EvrSnapshot $pre $pair[1] (Join-Path $Evidence "diff-00-pre-vs-$($pair[0]).txt")
    Say "diff 00-pre -> $($pair[0]): $((@($r | Where-Object { $_ -like '== *' }) -join '; '))"
}
Say 'DONE'
Set-Content -LiteralPath (Join-Path $Evidence 'DONE') -Value (Get-Date).ToString('s')
