# Shared helpers for the rig scripts (dot-sourced). Windows PowerShell 5.1 compatible.
#
# The invariants these helpers hold are specified by tools/rig/tests/run-tests.ps1:
#   - a run's CLEANUP_PENDING marker is written before any change to the rig and cleared only after a
#     restore of every settings location verified by SHA-256 against config-before\, except the
#     Steam-Cloud files, which keep the game's version and are reported unless -RestoreCloudFiles
#     restores them and makes Steam's record match (T-115);
#   - a file is only ever overwritten after a verified copy of it is kept in config-replaced\, and a file
#     changed after the run ended is never overwritten without an acknowledgement;
#   - one mutating rig script at a time (the LOCK file in the runs root);
#   - only stop.ps1 stops a live run; implicit cleanups never stop processes or touch owner runs;
#   - every wait is bounded; run folders are never moved or deleted;
#   - the scripts never write outside the allowed roots (the runs root and the game's settings
#     folders; in test mode only the test root), and never elevated, never with Steam closed.
#
# Paths come from environment variables (child scripts inherit them): EVR_RIG_RUNS_ROOT,
# EVR_RIG_GAME_ROOT, EVR_RIG_LAB_ROOT, EVR_RIG_SAVED_GAMES, EVR_RIG_STEAM_ROOT, EVR_RIG_BACKUPS_ROOT.
# The runs, lab and backups defaults are folders in the workspace (workspace.ps1, EVR_WORKSPACE).
# Test mode (EVR_RIG_TEST_ROOT set) additionally honours the test seams: EVR_RIG_DISPLAY_STUB (a JSON
# file standing in for the display controller), EVR_RIG_ELEVATION (elevated|not-elevated),
# EVR_RIG_STEAM_PROCESS, EVR_RIG_GAME_PROCESSES (semicolon list), the timeouts (EVR_RIG_*_SEC),
# EVR_RIG_TEST_ABORT_AT (a step at which the script kills its own process) and EVR_RIG_TEST_SLEEP_AT
# (step:seconds); it refuses any path outside the test root. In real mode the seams are ignored.

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'workspace.ps1')

$script:RigDefaultGameProcesses = @('DOOMEternalx64vk', 'DOOMSandBox64vk', 'idTechLauncher')
$script:RigVddMatch = 'MTT1337'
$script:RigVddFriendlyName = 'VDD by MTT'
$script:RigLogFiles = @()
$script:RigUtf8 = New-Object System.Text.UTF8Encoding($false)
$script:RigLockHeld = $false
$script:RigLockFile = $null

# ---------------------------------------------------------------------------------------------------
# Configuration

function Get-RigEnv([string]$Name, $Default) {
    $value = [Environment]::GetEnvironmentVariable($Name)
    if ([string]::IsNullOrWhiteSpace($value)) { return $Default }
    return $value
}

function Get-RigSteamRootDefault {
    # Read-only registry access; falls back to the default install folder.
    try {
        $value = (Get-ItemProperty -LiteralPath 'HKCU:\Software\Valve\Steam' -Name SteamPath -ErrorAction Stop).SteamPath
        if (-not [string]::IsNullOrWhiteSpace($value)) { return [IO.Path]::GetFullPath($value) }
    } catch { }
    return 'C:\Program Files (x86)\Steam'
}

function Test-RigPathUnder([string]$Path, [string]$Root) {
    $p = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    $r = [IO.Path]::GetFullPath($Root).TrimEnd('\')
    return ($p.Equals($r, [StringComparison]::OrdinalIgnoreCase) -or
        $p.StartsWith($r + '\', [StringComparison]::OrdinalIgnoreCase))
}

function Get-RigConfig {
    $testRoot = Get-RigEnv 'EVR_RIG_TEST_ROOT' $null
    if ($testRoot) { $testRoot = [IO.Path]::GetFullPath($testRoot) }
    $test = [bool]$testRoot
    # Test seams: read only in test mode.
    $seam = { param($name, $default) if ($test) { Get-RigEnv $name $default } else { $default } }

    $savedGames = Get-RigEnv 'EVR_RIG_SAVED_GAMES' $null
    if (-not $savedGames) { $savedGames = Join-Path $env:USERPROFILE 'Saved Games\id Software\DOOMEternal' }
    $steamRoot = Get-RigEnv 'EVR_RIG_STEAM_ROOT' $null
    if (-not $steamRoot) {
        if ($test) { throw 'Test mode requires EVR_RIG_STEAM_ROOT' }
        $steamRoot = Get-RigSteamRootDefault
    }
    $backups = Get-RigEnv 'EVR_RIG_BACKUPS_ROOT' $null
    if (-not $backups -and -not $test) { $backups = Join-Path (Get-EvrWorkspace) 'backups' }
    $runsRoot = Get-RigEnv 'EVR_RIG_RUNS_ROOT' $null
    if (-not $runsRoot) { $runsRoot = Join-Path (Get-EvrWorkspace) 'runs' }
    $labRoot = Get-RigEnv 'EVR_RIG_LAB_ROOT' $null
    if (-not $labRoot) { $labRoot = Join-Path (Get-EvrWorkspace) 'lab-install' }
    $gameProcesses = & $seam 'EVR_RIG_GAME_PROCESSES' $null
    if ($gameProcesses) {
        $gameProcesses = @($gameProcesses -split ';' | Where-Object { $_ } | ForEach-Object { $_ -replace '\.exe$', '' })
    } else {
        $gameProcesses = $script:RigDefaultGameProcesses
    }

    $cfg = [pscustomobject]@{
        TestMode        = $test
        TestRoot        = $testRoot
        RunsRoot        = [IO.Path]::GetFullPath($runsRoot)
        GameRoot        = [IO.Path]::GetFullPath((Get-RigEnv 'EVR_RIG_GAME_ROOT' 'E:\SteamLibrary\steamapps\common\DOOMEternal'))
        LabRoot         = [IO.Path]::GetFullPath($labRoot)
        SavedGamesDir   = [IO.Path]::GetFullPath($savedGames)
        SteamRoot       = [IO.Path]::GetFullPath($steamRoot)
        BackupsRoot     = $(if ($backups) { [IO.Path]::GetFullPath($backups) } else { $null })
        SteamProcess    = (& $seam 'EVR_RIG_STEAM_PROCESS' 'steam') -replace '\.exe$', ''
        GameProcesses   = $gameProcesses
        CloseTimeoutSec = [int](& $seam 'EVR_RIG_CLOSE_TIMEOUT_SEC' 20)
        KillTimeoutSec  = [int](& $seam 'EVR_RIG_KILL_TIMEOUT_SEC' 10)
        SyncQuietSec    = [int](& $seam 'EVR_RIG_SYNC_QUIET_SEC' 10)
        SyncMaxSec      = [int](& $seam 'EVR_RIG_SYNC_MAX_SEC' 120)
        WatchSec        = [int](& $seam 'EVR_RIG_WATCH_SEC' 10)
        WindowWaitSec   = [int](& $seam 'EVR_RIG_WINDOW_WAIT_SEC' 30)
        MuteWaitSec     = [int](& $seam 'EVR_RIG_MUTE_WAIT_SEC' 60)
        LockWaitSec     = [int](& $seam 'EVR_RIG_LOCK_WAIT_SEC' 120)
        # Settings and saves that changed outside a run stop the next run until the owner confirms them.
        # Off on the rig: the owner's saves are backed up and he does not play on it until release, so a
        # difference is logged and adopted, and a file changed after a run is kept in config-replaced        # before the restore. On in test mode, so the tests keep covering the guarded path.
        OwnerSettingsGuard = ((Get-RigEnv 'EVR_RIG_OWNER_SETTINGS_GUARD' $(if ($test) { '1' } else { '0' })) -eq '1')
        DisplayStub     = (& $seam 'EVR_RIG_DISPLAY_STUB' $null)
        ElevationProbe  = (& $seam 'EVR_RIG_ELEVATION' 'auto')
        AbortAt         = (& $seam 'EVR_RIG_TEST_ABORT_AT' $null)
        SleepAt         = (& $seam 'EVR_RIG_TEST_SLEEP_AT' $null)
    }

    if ($test) {
        if (-not $cfg.DisplayStub) { throw 'Test mode requires EVR_RIG_DISPLAY_STUB (no real display changes in tests)' }
        foreach ($name in 'RunsRoot', 'GameRoot', 'LabRoot', 'SavedGamesDir', 'SteamRoot', 'DisplayStub', 'BackupsRoot') {
            if ($cfg.$name -and -not (Test-RigPathUnder $cfg.$name $testRoot)) {
                throw "Test mode: $name '$($cfg.$name)' is outside the test root '$testRoot'"
            }
        }
    } elseif ($cfg.RunsRoot.StartsWith('C:', [StringComparison]::OrdinalIgnoreCase)) {
        throw "The runs root must not be on C: ($($cfg.RunsRoot))"
    }
    return $cfg
}

# Every write the scripts make goes through this check. Real mode: the runs root and the game's
# settings folders (D-038, D-040). Test mode: the test root only.
function Get-RigAllowedRoots($cfg) {
    if ($cfg.TestMode) { return @($cfg.TestRoot) }
    $roots = @($cfg.RunsRoot, $cfg.SavedGamesDir)
    foreach ($loc in (Get-SettingsLocations $cfg)) { $roots += $loc.Path }
    return $roots
}

function Assert-RigWritable($cfg, [string]$Path) {
    foreach ($root in (Get-RigAllowedRoots $cfg)) {
        if (Test-RigPathUnder $Path $root) { return }
    }
    throw "Refusing to write outside the allowed roots: $Path"
}

function Invoke-RigAbortPoint($cfg, [string]$Step) {
    # Test seams: pause at a step (to test concurrency), or simulate the script being killed there
    # (no finally blocks, no cleanup).
    if (-not $cfg.TestMode) { return }
    if ($cfg.SleepAt -and $cfg.SleepAt -match "^$([regex]::Escape($Step)):(\d+)$") { Start-Sleep -Seconds ([int]$Matches[1]) }
    if ($cfg.AbortAt -and ($cfg.AbortAt -eq $Step)) { Stop-Process -Id $PID -Force }
}

# Splits list parameters that arrive comma-joined: "powershell -File run.ps1 -Args "a","b"" passes the
# single string "a,b". A comma starts a new item only where the next item begins as expected.
function Split-RigList([string[]]$Items, [string]$ItemStart) {
    $out = @()
    foreach ($item in @($Items | Where-Object { $_ })) {
        $out += @([regex]::Split($item, ",(?=\s*$ItemStart)") | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    }
    return $out
}

# ---------------------------------------------------------------------------------------------------
# Logging and small file helpers

function Set-RigLogFiles([string[]]$Files) { $script:RigLogFiles = @($Files) }

function Write-RigLog([string]$Message, [string]$Level = 'INFO') {
    $line = '{0} [{1}] {2}' -f (Get-Date).ToString('yyyy-MM-dd HH:mm:ss.fff'), $Level, $Message
    Write-Host $line
    foreach ($file in $script:RigLogFiles) {
        try {
            $dir = Split-Path -Parent $file
            if (Test-Path -LiteralPath $dir) { [IO.File]::AppendAllText($file, $line + "`r`n", $script:RigUtf8) }
        } catch { }
    }
}

function Write-RigText($cfg, [string]$Path, [string]$Text) {
    Assert-RigWritable $cfg $Path
    $tmp = $Path + '.tmp'
    [IO.File]::WriteAllText($tmp, $Text, $script:RigUtf8)
    if (Test-Path -LiteralPath $Path) { [IO.File]::Replace($tmp, $Path, [NullString]::Value) } else { [IO.File]::Move($tmp, $Path) }
}

function Write-RigJson($cfg, [string]$Path, $Object) {
    Write-RigText $cfg $Path (ConvertTo-Json -InputObject $Object -Depth 12)
}

function Read-RigJson([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return $null }
    return ([IO.File]::ReadAllText($Path) | ConvertFrom-Json)
}

function Get-RigProp($Object, [string]$Name, $Default = $null) {
    if ($null -eq $Object) { return $Default }
    if ($Object -is [System.Collections.IDictionary]) {
        if ($Object.Contains($Name)) { return $Object[$Name] }
        return $Default
    }
    $prop = $Object.PSObject.Properties[$Name]
    if ($null -eq $prop) { return $Default }
    return $prop.Value
}

function Update-RigJson($cfg, [string]$Path, [hashtable]$Values) {
    $obj = Read-RigJson $Path
    if ($null -eq $obj) { $obj = New-Object psobject }
    foreach ($key in $Values.Keys) { $obj | Add-Member -NotePropertyName $key -NotePropertyValue $Values[$key] -Force }
    Write-RigJson $cfg $Path $obj
}

function Get-RigSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function New-RigDirectory($cfg, [string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) {
        Assert-RigWritable $cfg $Path
        [void][IO.Directory]::CreateDirectory($Path)
    }
}

# ---------------------------------------------------------------------------------------------------
# Lock: one mutating rig script at a time. <runs>\LOCK holds the owner's PID and start time; a lock
# whose process is gone (or whose PID was reused) is stale and taken over. Bounded wait, then refuse.

function Get-RigProcessStartFileTime($Process) {
    try { return $Process.StartTime.ToFileTimeUtc() } catch { return $null }
}

function Test-RigProcessAlive($ProcessId, $StartFileTimeUtc) {
    if (-not $ProcessId) { return $false }
    $p = Get-Process -Id ([int]$ProcessId) -ErrorAction SilentlyContinue
    if (-not $p) { return $false }
    try { if ($p.HasExited) { return $false } } catch { }
    if ($StartFileTimeUtc) { return ((Get-RigProcessStartFileTime $p) -eq [long]$StartFileTimeUtc) }
    return $true
}

function Enter-RigLock($cfg, [string]$Script) {
    if ($script:RigLockHeld) { return $true }
    New-RigDirectory $cfg $cfg.RunsRoot
    $file = Join-Path $cfg.RunsRoot 'LOCK'
    Assert-RigWritable $cfg $file
    $me = Get-Process -Id $PID
    $content = ConvertTo-Json -Compress -InputObject ([ordered]@{ pid = $PID; startFileTimeUtc = (Get-RigProcessStartFileTime $me); script = $Script; time = (Get-Date).ToString('o') })
    $deadline = (Get-Date).AddSeconds($cfg.LockWaitSec)
    $reported = $false
    while ($true) {
        try {
            $fs = New-Object IO.FileStream($file, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
            try { $bytes = $script:RigUtf8.GetBytes($content); $fs.Write($bytes, 0, $bytes.Length) } finally { $fs.Dispose() }
            $script:RigLockHeld = $true
            $script:RigLockFile = $file
            return $true
        } catch [System.IO.IOException] {
            $holder = $null
            try { $holder = Read-RigJson $file } catch { }
            if ($holder -and -not (Test-RigProcessAlive $holder.pid $holder.startFileTimeUtc)) {
                Write-RigLog "stale lock of $($holder.script) (pid $($holder.pid)) taken over"
                try { Remove-Item -LiteralPath $file -Force } catch { }
                continue
            }
            if ((Get-Date) -ge $deadline) {
                $who = 'another rig script'
                if ($holder) { $who = "$($holder.script) (pid $($holder.pid))" }
                Write-RigLog "BUSY: $who holds the rig lock ($file); gave up after $($cfg.LockWaitSec) s" 'ERROR'
                return $false
            }
            if (-not $reported -and $holder) { Write-RigLog "waiting for $($holder.script) (pid $($holder.pid)) to release the rig lock"; $reported = $true }
            Start-Sleep -Milliseconds 250
        }
    }
}

function Exit-RigLock {
    if (-not $script:RigLockHeld) { return }
    try {
        $holder = Read-RigJson $script:RigLockFile
        if ($holder -and [int]$holder.pid -eq $PID) { Remove-Item -LiteralPath $script:RigLockFile -Force }
    } catch { }
    $script:RigLockHeld = $false
}

function Exit-RigScript([int]$Code) {
    Exit-RigLock
    exit $Code
}

# ---------------------------------------------------------------------------------------------------
# Session identity: <runs>\SESSION holds the current work session; DISPLAY_ADDED and run markers
# record it, so a display left by an earlier session is recognised and removed.

function Get-RigSession($cfg, [string]$SessionId) {
    if ($SessionId) { return $SessionId }
    $file = Join-Path $cfg.RunsRoot 'SESSION'
    $existing = Read-RigJson $file
    if ($existing -and (Get-RigProp $existing 'id')) { return $existing.id }
    return (New-RigSession $cfg)
}

function New-RigSession($cfg) {
    New-RigDirectory $cfg $cfg.RunsRoot
    $id = '{0}-{1}' -f (Get-Date).ToString('yyyyMMddHHmmss'), ([guid]::NewGuid().ToString('N').Substring(0, 8))
    Write-RigJson $cfg (Join-Path $cfg.RunsRoot 'SESSION') ([ordered]@{ id = $id; started = (Get-Date).ToString('o') })
    return $id
}

# ---------------------------------------------------------------------------------------------------
# Probes (read-only)

function Test-RigElevated($cfg) {
    if ($cfg.ElevationProbe -eq 'elevated') { return $true }
    if ($cfg.ElevationProbe -eq 'not-elevated') { return $false }
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Test-RigSteamRunning($cfg) {
    return [bool](Get-Process -Name $cfg.SteamProcess -ErrorAction SilentlyContinue)
}

# Game processes that are really running (an exited process can still be listed briefly).
function Get-RigGameProcesses($cfg) {
    return @(Get-Process -Name $cfg.GameProcesses -ErrorAction SilentlyContinue | Where-Object {
            $gone = $false; try { $gone = $_.HasExited } catch { }; -not $gone })
}

function Get-RigHags {
    try {
        $value = (Get-ItemProperty -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\GraphicsDrivers' -Name HwSchMode -ErrorAction Stop).HwSchMode
        $state = switch ($value) { 2 { 'on' } 1 { 'off' } default { 'unknown' } }
        return [ordered]@{ HwSchMode = $value; state = $state }
    } catch {
        return [ordered]@{ HwSchMode = $null; state = 'not set (driver default)' }
    }
}

# ---------------------------------------------------------------------------------------------------
# Settings locations, manifests and snapshots

function Get-SettingsLocations($cfg) {
    $list = @([pscustomobject]@{ Name = 'saved-games'; Kind = 'saved-games'; Path = $cfg.SavedGamesDir })
    $userdata = Join-Path $cfg.SteamRoot 'userdata'
    if (Test-Path -LiteralPath $userdata) {
        foreach ($dir in (Get-ChildItem -LiteralPath $userdata -Directory -Force -ErrorAction SilentlyContinue | Sort-Object Name)) {
            $remote = Join-Path $dir.FullName '782330\remote'
            if (Test-Path -LiteralPath $remote -PathType Container) {
                $list += [pscustomobject]@{ Name = "steam-$($dir.Name)"; Kind = 'steam-remote'; Path = $remote }
            }
        }
    }
    return $list
}

function Get-TreeFiles([string]$Root) {
    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return @() }
    $full = (Get-Item -LiteralPath $Root -Force).FullName.TrimEnd('\')
    $out = @()
    foreach ($f in (Get-ChildItem -LiteralPath $full -Recurse -File -Force -ErrorAction Stop)) {
        if ($f.Name -like '*.evr-restore.tmp') { continue }
        $out += [pscustomobject]@{ Rel = $f.FullName.Substring($full.Length + 1); Full = $f.FullName }
    }
    return $out
}

function New-RigHashTable { return New-Object System.Collections.Hashtable ([StringComparer]::OrdinalIgnoreCase) }

function Read-RigManifest([string]$File) {
    # SHA256SUMS: "<sha256> *<location>\<relative path>" per line.
    $table = New-RigHashTable
    foreach ($line in [IO.File]::ReadAllLines($File)) {
        if ($line -match '^([0-9a-f]{64}) \*(.+)$') { $table[$Matches[2]] = $Matches[1] }
    }
    return $table
}

function Format-RigManifest($Table) {
    $lines = foreach ($key in ($Table.Keys | Sort-Object)) { '{0} *{1}' -f $Table[$key], $key }
    return (($lines -join "`n") + "`n")
}

# Copies every settings location into <run>\<Name>\ (written as <Name>.partial, renamed when complete),
# with locations.json and SHA256SUMS. A folder named <Name> is therefore always complete.
function New-SettingsSnapshot($cfg, [string]$RunDir, [string]$Name, $Locations) {
    $final = Join-Path $RunDir $Name
    $partial = "$final.partial"
    Assert-RigWritable $cfg $partial
    if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Recurse -Force }
    New-RigDirectory $cfg $partial
    $manifest = New-RigHashTable
    $locInfo = @()
    foreach ($loc in $Locations) {
        $exists = Test-Path -LiteralPath $loc.Path -PathType Container
        $count = 0
        if ($exists) {
            foreach ($f in (Get-TreeFiles $loc.Path)) {
                $dest = Join-Path (Join-Path $partial $loc.Name) $f.Rel
                New-RigDirectory $cfg (Split-Path -Parent $dest)
                $ok = $false
                for ($attempt = 0; $attempt -lt 3 -and -not $ok; $attempt++) {
                    [IO.File]::Copy($f.Full, $dest, $true)
                    $copyHash = Get-RigSha256 $dest
                    $ok = ($copyHash -eq (Get-RigSha256 $f.Full))
                    if (-not $ok) { Start-Sleep -Milliseconds 500 }
                }
                if (-not $ok) { throw "File changed while being copied: $($f.Full)" }
                $manifest[(Join-Path $loc.Name $f.Rel)] = $copyHash
                $count++
            }
        }
        $locInfo += [ordered]@{ name = $loc.Name; kind = $loc.Kind; path = $loc.Path; existed = $exists; files = $count }
    }
    Write-RigJson $cfg (Join-Path $partial 'locations.json') @($locInfo)
    Write-RigText $cfg (Join-Path $partial 'SHA256SUMS') (Format-RigManifest $manifest)
    Rename-Item -LiteralPath $partial -NewName $Name
    return [pscustomobject]@{ Path = $final; Manifest = $manifest; Locations = $locInfo }
}

function Test-SettingsSnapshot([string]$SnapshotDir) {
    # Returns the list of problems (empty when every file matches SHA256SUMS).
    $problems = @()
    $sums = Join-Path $SnapshotDir 'SHA256SUMS'
    if (-not (Test-Path -LiteralPath $sums)) { return @("missing $sums") }
    $manifest = Read-RigManifest $sums
    foreach ($key in $manifest.Keys) {
        $file = Join-Path $SnapshotDir $key
        if (-not (Test-Path -LiteralPath $file)) { $problems += "snapshot file missing: $key"; continue }
        if ((Get-RigSha256 $file) -ne $manifest[$key]) { $problems += "snapshot file does not match its SHA-256: $key" }
    }
    return $problems
}

function Get-SnapshotLocations([string]$SnapshotDir) {
    return @(Read-RigJson (Join-Path $SnapshotDir 'locations.json'))
}

# The game's own log files under Saved Games\base (qconsole.log, structured.log, ...): rewritten by every
# game start, through the rig or not, and no setting. The snapshots keep them, but they are never compared
# or restored, so a game run outside the rig scripts does not block the next run.
function Test-RigGameLogFile([string]$LocationName, [string]$Rel) {
    return ($LocationName -eq 'saved-games' -and $Rel -match '^base\\(.+\\)?[^\\]+\.log$')
}

# Compares the live settings with a manifest. Returns changed, removed and added lists of
# "<location>\<relative path>" keys (the game's log files left out, Test-RigGameLogFile).
function Compare-SettingsWithManifest($Locations, $Manifest) {
    $changed = @(); $removed = @(); $added = @(); $unchanged = 0; $unreadable = @()
    foreach ($loc in $Locations) {
        $prefix = $loc.name + '\'
        $live = New-RigHashTable
        foreach ($f in (Get-TreeFiles $loc.path)) {
            if (Test-RigGameLogFile $loc.name $f.Rel) { continue }
            try { $live[$f.Rel] = Get-RigSha256 $f.Full } catch { $unreadable += ($prefix + $f.Rel) }
        }
        foreach ($key in $Manifest.Keys) {
            if (-not $key.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { continue }
            $rel = $key.Substring($prefix.Length)
            if (Test-RigGameLogFile $loc.name $rel) { continue }
            if (-not $live.ContainsKey($rel)) {
                if ($unreadable -notcontains $key) { $removed += $key }
            } elseif ($live[$rel] -ne $Manifest[$key]) { $changed += $key } else { $unchanged++ }
        }
        foreach ($rel in $live.Keys) {
            if (-not $Manifest.ContainsKey($prefix + $rel)) { $added += ($prefix + $rel) }
        }
    }
    return [pscustomobject]@{ Changed = $changed; Removed = $removed; Added = $added; Unchanged = $unchanged; Unreadable = $unreadable }
}

# The pre-development backups (the first baseline, used until a run has been restored and verified):
# <backups>\doom-eternal-savedgames\*-pre-dev and <backups>\doom-eternal-remote\*-pre-dev, each with
# SHA256SUMS.txt lines "<SHA256>  <size>  <relative path>". Returns $null when there are none.
function Get-RigPreDevBaseline($cfg) {
    if (-not $cfg.BackupsRoot -or -not (Test-Path -LiteralPath $cfg.BackupsRoot)) { return $null }
    $locations = @(Get-SettingsLocations $cfg)
    $steam = @($locations | Where-Object { $_.Kind -eq 'steam-remote' })
    $map = @(@{ Folder = 'doom-eternal-savedgames'; Loc = @($locations | Where-Object { $_.Kind -eq 'saved-games' })[0] })
    if ($steam.Count -eq 1) { $map += @{ Folder = 'doom-eternal-remote'; Loc = $steam[0] } }
    $manifest = New-RigHashTable
    $locs = @(); $sources = @()
    foreach ($m in $map) {
        $parent = Join-Path $cfg.BackupsRoot $m.Folder
        if (-not (Test-Path -LiteralPath $parent)) { continue }
        $latest = @(Get-ChildItem -LiteralPath $parent -Directory | Where-Object { $_.Name -like '*-pre-dev' } | Sort-Object Name) | Select-Object -Last 1
        if (-not $latest) { continue }
        $sums = Join-Path $latest.FullName 'SHA256SUMS.txt'
        if (-not (Test-Path -LiteralPath $sums)) { continue }
        foreach ($line in [IO.File]::ReadAllLines($sums)) {
            if ($line -match '^([0-9A-Fa-f]{64})\s+(?:\d+\s+)?\*?(.+?)\s*$') { $manifest[(Join-Path $m.Loc.Name $Matches[2])] = $Matches[1].ToLowerInvariant() }
        }
        $locs += [pscustomobject]@{ name = $m.Loc.Name; path = $m.Loc.Path; existed = $true }
        $sources += $latest.FullName
    }
    if ($locs.Count -eq 0) { return $null }
    return [pscustomobject]@{ Name = ($sources -join ', '); Locations = $locs; Manifest = $manifest }
}

# Writes one file from the snapshot into place: copy to <target>.evr-restore.tmp, verify, rename over
# the target (ReplaceFile keeps the target's attributes and security), verify again.
function Restore-RigFile($cfg, [string]$Source, [string]$Target, [string]$ExpectedHash) {
    Assert-RigWritable $cfg $Target
    New-RigDirectory $cfg (Split-Path -Parent $Target)
    $tmp = "$Target.evr-restore.tmp"
    [IO.File]::Copy($Source, $tmp, $true)
    try {
        if ((Get-RigSha256 $tmp) -ne $ExpectedHash) { throw "temporary copy does not match SHA-256" }
        if (Test-Path -LiteralPath $Target) { [IO.File]::Replace($tmp, $Target, [NullString]::Value) } else { [IO.File]::Move($tmp, $Target) }
    } finally {
        if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
    }
    if ((Get-RigSha256 $Target) -ne $ExpectedHash) { throw "restored file does not match SHA-256" }
}

# The live path of a "<location>\<relative path>" snapshot key.
function Resolve-RigSnapshotTarget($Locations, [string]$Key) {
    $locName = $Key.Substring(0, $Key.IndexOf('\'))
    $loc = @($Locations | Where-Object { $_.name -eq $locName })[0]
    if (-not $loc) { throw "snapshot key names an unknown location: $Key" }
    return (Join-Path $loc.path $Key.Substring($locName.Length + 1))
}

# Restores one snapshot key: the file being replaced is first kept (verified) under $ReplacedDir, then
# the snapshot's copy is written atomically and verified. Returns $true when a replaced copy was kept.
function Restore-RigRunKey($cfg, $Locations, [string]$BeforeDir, $Manifest, [string]$Key, [string]$ReplacedDir) {
    $target = Resolve-RigSnapshotTarget $Locations $Key
    $kept = $false
    if (Test-Path -LiteralPath $target) {
        $liveHash = Get-RigSha256 $target
        $copy = Join-Path $ReplacedDir $Key
        New-RigDirectory $cfg (Split-Path -Parent $copy)
        [IO.File]::Copy($target, $copy, $true)
        if ((Get-RigSha256 $copy) -ne $liveHash) { throw 'copy into config-replaced does not match SHA-256' }
        $kept = $true
    }
    Restore-RigFile $cfg (Join-Path $BeforeDir $Key) $target $Manifest[$Key]
    return $kept
}

function Format-RigFacts($Facts) {
    if (-not $Facts) { return 'absent' }
    return ('{0} bytes, SHA-256 {1}' -f $Facts.size, $Facts.sha256)
}

function Remove-RigRestoreTemps($cfg, $Locations) {
    # Leftovers of our own atomic writes from an interrupted restore (only files we name).
    foreach ($loc in $Locations) {
        if (-not (Test-Path -LiteralPath $loc.path)) { continue }
        foreach ($f in (Get-ChildItem -LiteralPath $loc.path -Recurse -File -Force -Filter '*.evr-restore.tmp' -ErrorAction SilentlyContinue)) {
            Assert-RigWritable $cfg $f.FullName
            Remove-Item -LiteralPath $f.FullName -Force
            Write-RigLog "removed leftover temporary file $($f.FullName)"
        }
    }
}

# Waits until Steam has finished its post-exit sync: nothing under userdata\*\782330 changes for
# SyncQuietSec seconds. Bounded by SyncMaxSec. Returns $true when settled.
function Wait-RigSteamSync($cfg) {
    $roots = @()
    $userdata = Join-Path $cfg.SteamRoot 'userdata'
    if (Test-Path -LiteralPath $userdata) {
        foreach ($dir in (Get-ChildItem -LiteralPath $userdata -Directory -Force -ErrorAction SilentlyContinue)) {
            $app = Join-Path $dir.FullName '782330'
            if (Test-Path -LiteralPath $app) { $roots += $app }
        }
    }
    if ($roots.Count -eq 0) { return $true }
    $signature = {
        $parts = foreach ($r in $roots) {
            Get-ChildItem -LiteralPath $r -Recurse -File -Force -ErrorAction SilentlyContinue |
                ForEach-Object { '{0}|{1}|{2}' -f $_.FullName, $_.Length, $_.LastWriteTimeUtc.Ticks }
        }
        ($parts | Sort-Object) -join "`n"
    }
    $start = Get-Date
    $last = & $signature
    $quietSince = Get-Date
    while ($true) {
        if (((Get-Date) - $quietSince).TotalSeconds -ge $cfg.SyncQuietSec) { return $true }
        if (((Get-Date) - $start).TotalSeconds -ge $cfg.SyncMaxSec) { return $false }
        Start-Sleep -Milliseconds 500
        $now = & $signature
        if ($now -ne $last) { $last = $now; $quietSince = Get-Date }
    }
}

# ---------------------------------------------------------------------------------------------------
# Steam Cloud (T-115). Two classes of settings file:
#   - local: the Saved Games tree (plain text config, key bindings, logs). A whole-file restore is safe.
#   - cloud: everything under Steam's userdata\<id>\782330\remote\ (PROFILE\profile.bin and the save
#     slots). The game reads and writes them through Steam, and Steam keeps each file's size and SHA-1 in
#     782330\remotecache.vdf (Auto-Cloud root 0 = the remote folder). A file put back behind Steam's back
#     leaves that record stale, and at the next launch the game reports "Profile corrupt, creating a new
#     one" and uploads a fresh profile (docs/rig-findings/launcher-live.md, open question 8). Cloud files
#     are therefore never restored by default: the cleanup keeps the game's version and reports it; the
#     opt-in -RestoreCloudFiles restores them and then runs Invoke-RigCloudResync.
# remotecache.vdf is only ever read here, never written.

function Test-RigCloudLocation($Location) {
    $kind = Get-RigProp $Location 'kind'
    if (-not $kind) { $kind = Get-RigProp $Location 'Kind' }
    return ($kind -eq 'steam-remote')
}

# The names of the snapshot locations whose files are Steam-Cloud synced.
function Get-RigCloudLocationNames($Locations) {
    return @($Locations | Where-Object { Test-RigCloudLocation $_ } | ForEach-Object { Get-RigProp $_ 'name' })
}

function Test-RigCloudKey([string[]]$CloudNames, [string]$Key) {
    $i = $Key.IndexOf('\')
    if ($i -lt 1) { return $false }
    return ($CloudNames -contains $Key.Substring(0, $i))
}

# Minimal KeyValues (VDF) reader: quoted strings and braces, // comments. Returns nested hashtables
# (case-insensitive keys); a value is a string or a hashtable.
function Read-RigVdf([string]$Path) {
    $text = [IO.File]::ReadAllText($Path)
    $tokens = [regex]::Matches($text, '"((?:[^"\\]|\\.)*)"|([{}])|//[^\n]*')
    $root = New-RigHashTable
    $stack = New-Object System.Collections.Stack
    $current = $root
    $pendingKey = $null
    foreach ($m in $tokens) {
        if ($m.Value.StartsWith('//')) { continue }
        if ($m.Groups[2].Success) {
            if ($m.Value -eq '{') {
                if ($null -eq $pendingKey) { throw "VDF: '{' without a key in $Path" }
                $child = New-RigHashTable
                $current[$pendingKey] = $child
                $stack.Push($current)
                $current = $child
                $pendingKey = $null
            } else {
                if ($stack.Count -eq 0) { throw "VDF: unbalanced '}' in $Path" }
                $current = $stack.Pop()
            }
            continue
        }
        $value = $m.Groups[1].Value -replace '\\(.)', '$1'
        if ($null -eq $pendingKey) { $pendingKey = $value } else { $current[$pendingKey] = $value; $pendingKey = $null }
    }
    if ($stack.Count -ne 0) { throw "VDF: unterminated block in $Path" }
    return $root
}

# Steam's record of one cloud location: <remote>\..\remotecache.vdf. Returns Present (the file exists
# and parses), Error, and Files: "<relative path with \>" -> Size, Sha1, SyncState.
function Get-RigSteamCloudCache([string]$RemoteDir) {
    $file = Join-Path (Split-Path -Parent $RemoteDir) 'remotecache.vdf'
    $result = [pscustomobject]@{ Path = $file; Present = $false; Error = $null; Files = (New-RigHashTable) }
    if (-not (Test-Path -LiteralPath $file)) { return $result }
    try {
        $vdf = Read-RigVdf $file
        $app = $null
        foreach ($k in $vdf.Keys) { if ($vdf[$k] -is [System.Collections.IDictionary]) { $app = $vdf[$k]; break } }
        if ($app) {
            foreach ($k in $app.Keys) {
                $e = $app[$k]
                if (-not ($e -is [System.Collections.IDictionary])) { continue }
                # Only root 0 (the remote folder) is a file of this location.
                if ($e.Contains('root') -and $e['root'] -ne '0') { continue }
                $size = $null; if ($e.Contains('size')) { $size = [long]$e['size'] }
                $sha = $null; if ($e.Contains('sha')) { $sha = ([string]$e['sha']).ToLowerInvariant() }
                $sync = $null; if ($e.Contains('syncstate')) { $sync = $e['syncstate'] }
                $result.Files[($k -replace '/', '\')] = [pscustomobject]@{ Size = $size; Sha1 = $sha; SyncState = $sync }
            }
        }
        $result.Present = $true
    } catch {
        $result.Error = $_.Exception.Message
    }
    return $result
}

function Get-RigSha1([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA1).Hash.ToLowerInvariant()
}

function Get-RigFileFacts([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    return [ordered]@{ sha256 = (Get-RigSha256 $Path); size = (Get-Item -LiteralPath $Path -Force).Length }
}

# Compares every cloud file on disk with Steam's record. Stale: tracked with another size or SHA-1 (the
# game's next launch may reset its profile). Untracked: on disk, not in the record (Steam takes it as new
# at the next app session). Returns Stale and Untracked lists of "<location>\<relative path>" keys, the
# details per stale key, and Unreadable (a record that is missing or does not parse).
function Test-RigCloudConsistency($Locations) {
    $out = [pscustomobject]@{ Stale = @(); Untracked = @(); Details = @(); Unreadable = @() }
    foreach ($loc in @($Locations | Where-Object { Test-RigCloudLocation $_ })) {
        $name = Get-RigProp $loc 'name'; if (-not $name) { $name = Get-RigProp $loc 'Name' }
        $path = Get-RigProp $loc 'path'; if (-not $path) { $path = Get-RigProp $loc 'Path' }
        if (-not (Test-Path -LiteralPath $path -PathType Container)) { continue }
        $cache = Get-RigSteamCloudCache $path
        if (-not $cache.Present) {
            $why = 'missing'; if ($cache.Error) { $why = $cache.Error }
            $out.Unreadable += "$name ($($cache.Path): $why)"
            continue
        }
        foreach ($f in (Get-TreeFiles $path)) {
            $key = "$name\$($f.Rel)"
            if (-not $cache.Files.ContainsKey($f.Rel)) { $out.Untracked += $key; continue }
            $rec = $cache.Files[$f.Rel]
            $size = (Get-Item -LiteralPath $f.Full -Force).Length
            $sha1 = Get-RigSha1 $f.Full
            if (($null -ne $rec.Size -and $rec.Size -ne $size) -or ($rec.Sha1 -and $rec.Sha1 -ne $sha1)) {
                $out.Stale += $key
                $out.Details += [ordered]@{ key = $key; fileSize = $size; fileSha1 = $sha1; steamSize = $rec.Size; steamSha1 = $rec.Sha1 }
            }
        }
    }
    return $out
}

# The cvars a run set on the command line (+name value), from run.json's args. Commands that take a
# path (map, devmap, exec) are left out.
function Get-RigForcedCvars($Run) {
    $argString = [string](Get-RigProp $Run 'args' '')
    $names = @()
    foreach ($m in [regex]::Matches($argString, '(?:^|\s)\+([A-Za-z_][A-Za-z0-9_]*)\s+("[^"]*"|[^\s+]+)')) {
        $n = $m.Groups[1].Value
        if (@('map', 'devmap', 'exec', 'connect_lobby') -contains $n.ToLowerInvariant()) { continue }
        if ($names -notcontains $n) { $names += $n }
    }
    return $names
}

function Get-RigAsciiCount([byte[]]$Bytes, [string]$Needle) {
    if ($null -eq $Bytes -or $Bytes.Length -eq 0) { return 0 }
    # Latin-1 maps each byte to one char, so a byte search becomes a string search.
    $hay = [Text.Encoding]::GetEncoding(28591).GetString($Bytes)
    return ([regex]::Matches($hay, [regex]::Escape($Needle), 'IgnoreCase')).Count
}

# T-092 save-point check for a cloud file the cleanup keeps: which forced cvar names occur more often in
# the game's version than before the run (a hint that a runtime value was saved into it).
function Find-RigCvarsInCloudFile([string]$BeforeFile, [string]$AfterFile, [string[]]$Cvars) {
    $found = @()
    if (-not $AfterFile -or -not (Test-Path -LiteralPath $AfterFile)) { return $found }
    $after = [IO.File]::ReadAllBytes($AfterFile)
    $before = $null
    if ($BeforeFile -and (Test-Path -LiteralPath $BeforeFile)) { $before = [IO.File]::ReadAllBytes($BeforeFile) }
    foreach ($c in $Cvars) {
        if ((Get-RigAsciiCount $after $c) -gt (Get-RigAsciiCount $before $c)) { $found += $c }
    }
    return $found
}

# The resync step (proven on the rig, launcher-live.md open question 8): with the cloud files restored
# and the game closed, start the game directly (SteamAppId=782330) and kill it about 4 s in, before it
# loads its profile (game/shell/shell). At the end of that app session Steam finds the restored files
# changed and takes them as the current version: remotecache.vdf and the cloud then hold them. Waits for
# Steam's sync afterwards. The caller checks the result with Test-RigCloudConsistency and restores any
# local file the short launch touched. Never run while a game process runs; never elevated; Steam first.
# Returns Ok, Note and the record appended to <run>\resync.json.
function Invoke-RigCloudResync($cfg, [string]$RunDir) {
    $rec = [ordered]@{ time = (Get-Date).ToString('o'); exe = $null; args = $null; pid = $null; killAfterMs = $null; outcome = 'failed'; note = '' }
    $done = {
        param($ok, $note)
        $rec.note = $note
        if ($ok) { $rec.outcome = 'done' }
        if ($RunDir) {
            $file = Join-Path $RunDir 'resync.json'
            $history = @(Read-RigJson $file)
            Write-RigJson $cfg $file (@($history | Where-Object { $_ }) + @($rec))
        }
        Write-RigLog "cloud resync: $note" $(if ($ok) { 'INFO' } else { 'ERROR' })
        return [pscustomobject]@{ Ok = [bool]$ok; Note = $note }
    }
    $exe = Join-Path $cfg.GameRoot 'DOOMEternalx64vk.exe'
    $argString = '+r_fullscreen 0 +s_volume 0'
    $killMs = 4000
    if ($cfg.TestMode) {
        $exe = Get-RigEnv 'EVR_RIG_RESYNC_EXE' $exe
        $argString = Get-RigEnv 'EVR_RIG_RESYNC_ARGS' $argString
        $killMs = [int](Get-RigEnv 'EVR_RIG_RESYNC_KILL_MS' $killMs)
    }
    $rec.exe = $exe; $rec.args = $argString; $rec.killAfterMs = $killMs
    if (Test-RigElevated $cfg) { return (& $done $false 'refused: running elevated') }
    if (-not (Test-RigSteamRunning $cfg)) { return (& $done $false "refused: $($cfg.SteamProcess).exe is not running (Steam must see the app session)") }
    $games = @(Get-RigGameProcesses $cfg)
    if ($games.Count -gt 0) { return (& $done $false ("refused: a game process is running ({0})" -f (($games | ForEach-Object { "$($_.ProcessName) $($_.Id)" }) -join ', '))) }
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { return (& $done $false "exe not found: $exe") }

    $existing = @{}
    foreach ($p in @(Get-Process -ErrorAction SilentlyContinue)) { $existing[$p.Id] = $true }
    [Environment]::SetEnvironmentVariable('SteamAppId', '782330', 'Process')
    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.WorkingDirectory = Split-Path -Parent $exe
    $psi.Arguments = $argString
    $psi.UseShellExecute = $true
    $psi.WindowStyle = 'Minimized'
    $proc = [Diagnostics.Process]::Start($psi)
    $rec.pid = $proc.Id
    Write-RigLog "cloud resync: started pid $($proc.Id) ($exe $argString); killing it after $killMs ms, before the profile load"
    Start-Sleep -Milliseconds $killMs
    $targets = @($proc)
    # A hand-off (the game restarting itself through Steam) is killed as well.
    $targets += @(Get-RigGameProcesses $cfg | Where-Object { -not $existing.ContainsKey($_.Id) -and $_.Id -ne $proc.Id })
    $survivors = @()
    foreach ($p in $targets) {
        try { if (-not $p.HasExited) { $p.Kill() } } catch { }
        if (-not $p.WaitForExit($cfg.KillTimeoutSec * 1000)) { $survivors += $p.Id }
    }
    if ($survivors.Count -gt 0) { return (& $done $false "process(es) $($survivors -join ',') did not exit after the kill") }
    if (-not (Wait-RigSteamSync $cfg)) { return (& $done $false "Steam's 782330 folder kept changing for $($cfg.SyncMaxSec) s after the resync launch") }
    return (& $done $true 'launched and killed before the profile load; Steam sync settled')
}

# ---------------------------------------------------------------------------------------------------
# Native helpers: windows, display configuration (CCD API) and per-process audio sessions

$script:RigNativeSource = @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace EvrRig
{
    public class WindowInfo
    {
        public long Handle; public int Pid; public bool Visible; public string ClassName; public string Title;
        public int X; public int Y; public int Width; public int Height;
    }

    public static class Windows
    {
        delegate bool EnumProc(IntPtr hwnd, IntPtr lparam);
        [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr lparam);
        [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
        [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
        [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr hwnd, uint cmd);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassNameW(IntPtr hwnd, StringBuilder sb, int max);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowTextW(IntPtr hwnd, StringBuilder sb, int max);
        [DllImport("user32.dll")] static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr w, IntPtr l);
        [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
        [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr hwnd, out RECT r);
        [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
        [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr value);
        [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr hwnd);
        [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr hwnd);
        [DllImport("user32.dll")] static extern bool IsWindow(IntPtr hwnd);
        [DllImport("user32.dll")] static extern bool AttachThreadInput(uint attach, uint to, bool doAttach);
        [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
        [StructLayout(LayoutKind.Sequential)] struct RECT { public int Left, Top, Right, Bottom; }

        public static long Foreground() { return GetForegroundWindow().ToInt64(); }

        public static int ForegroundPid()
        {
            uint pid;
            IntPtr h = GetForegroundWindow();
            if (h == IntPtr.Zero) return 0;
            GetWindowThreadProcessId(h, out pid);
            return (int)pid;
        }

        // Gives the foreground back to a window (the owner's), attaching to the current foreground
        // thread's input so Windows allows the switch.
        public static bool RestoreForeground(long handle)
        {
            IntPtr target = new IntPtr(handle);
            if (target == IntPtr.Zero || !IsWindow(target)) return false;
            IntPtr fg = GetForegroundWindow();
            uint unused;
            uint fgThread = fg == IntPtr.Zero ? 0 : GetWindowThreadProcessId(fg, out unused);
            uint me = GetCurrentThreadId();
            bool attached = fgThread != 0 && fgThread != me && AttachThreadInput(me, fgThread, true);
            BringWindowToTop(target);
            SetForegroundWindow(target);
            if (attached) AttachThreadInput(me, fgThread, false);
            return GetForegroundWindow() == target;
        }

        public static void MakeDpiAware()
        {
            try { if (SetProcessDpiAwarenessContext(new IntPtr(-4))) return; } catch (EntryPointNotFoundException) { }
            try { SetProcessDPIAware(); } catch (EntryPointNotFoundException) { }
        }

        public static List<WindowInfo> TopLevel(int[] pids)
        {
            HashSet<int> set = new HashSet<int>(pids);
            List<WindowInfo> list = new List<WindowInfo>();
            EnumProc cb = delegate (IntPtr h, IntPtr l)
            {
                uint pid;
                GetWindowThreadProcessId(h, out pid);
                if (!set.Contains((int)pid)) return true;
                if (GetWindow(h, 4) != IntPtr.Zero) return true;
                StringBuilder cls = new StringBuilder(256); GetClassNameW(h, cls, 256);
                StringBuilder title = new StringBuilder(512); GetWindowTextW(h, title, 512);
                RECT r; GetWindowRect(h, out r);
                WindowInfo w = new WindowInfo();
                w.Handle = h.ToInt64(); w.Pid = (int)pid; w.Visible = IsWindowVisible(h);
                w.ClassName = cls.ToString(); w.Title = title.ToString();
                w.X = r.Left; w.Y = r.Top; w.Width = r.Right - r.Left; w.Height = r.Bottom - r.Top;
                list.Add(w);
                return true;
            };
            EnumWindows(cb, IntPtr.Zero);
            GC.KeepAlive(cb);
            return list;
        }

        public static bool Close(long handle) { return PostMessageW(new IntPtr(handle), 0x0010, IntPtr.Zero, IntPtr.Zero); }

        // SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
        public static bool Move(long handle, int x, int y) { return SetWindowPos(new IntPtr(handle), IntPtr.Zero, x, y, 0, 0, 0x0001 | 0x0004 | 0x0010); }
    }

    public class TargetDesc
    {
        public uint AdapterLow; public int AdapterHigh; public uint SourceId; public uint TargetId;
        public bool Active; public bool Available; public bool HasMode; public bool Primary; public bool IsVdd;
        public string FriendlyName; public string DevicePath; public string GdiName;
        public int X; public int Y; public int Width; public int Height; public uint Rotation; public double Refresh;
    }

    public class ModeDesc { public int Width; public int Height; public int Frequency; public int Bpp; }

    // Display modes of one GDI device (\\.\DISPLAYn). Set changes only that device, dynamically:
    // no CDS_UPDATEREGISTRY, so nothing is written to the registry by us.
    public static class DisplayModes
    {
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct DEVMODE
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string DeviceName;
            public ushort SpecVersion; public ushort DriverVersion; public ushort Size; public ushort DriverExtra;
            public uint Fields; public int PositionX; public int PositionY; public uint DisplayOrientation; public uint DisplayFixedOutput;
            public short Color; public short Duplex; public short YResolution; public short TTOption; public short Collate;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string FormName;
            public ushort LogPixels; public uint BitsPerPel; public uint PelsWidth; public uint PelsHeight; public uint DisplayFlags; public uint DisplayFrequency;
            public uint ICMMethod; public uint ICMIntent; public uint MediaType; public uint DitherType; public uint Reserved1; public uint Reserved2; public uint PanningWidth; public uint PanningHeight;
        }

        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern bool EnumDisplaySettingsW(string device, int mode, ref DEVMODE dm);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int ChangeDisplaySettingsExW(string device, ref DEVMODE dm, IntPtr hwnd, uint flags, IntPtr param);

        static DEVMODE New() { DEVMODE dm = new DEVMODE(); dm.Size = (ushort)Marshal.SizeOf(typeof(DEVMODE)); return dm; }

        static ModeDesc Desc(DEVMODE dm)
        {
            ModeDesc m = new ModeDesc();
            m.Width = (int)dm.PelsWidth; m.Height = (int)dm.PelsHeight; m.Frequency = (int)dm.DisplayFrequency; m.Bpp = (int)dm.BitsPerPel;
            return m;
        }

        public static List<ModeDesc> List(string device)
        {
            List<ModeDesc> list = new List<ModeDesc>();
            DEVMODE dm = New();
            for (int i = 0; EnumDisplaySettingsW(device, i, ref dm); i++) { list.Add(Desc(dm)); dm = New(); }
            return list;
        }

        public static ModeDesc Current(string device)
        {
            DEVMODE dm = New();
            return EnumDisplaySettingsW(device, -1, ref dm) ? Desc(dm) : null;
        }

        public static int Set(string device, int width, int height, int frequency)
        {
            DEVMODE dm = New();
            if (!EnumDisplaySettingsW(device, -1, ref dm)) return -100;
            dm.PelsWidth = (uint)width; dm.PelsHeight = (uint)height;
            dm.Fields = 0x80000 | 0x100000;
            if (frequency > 0) { dm.DisplayFrequency = (uint)frequency; dm.Fields |= 0x400000; }
            return ChangeDisplaySettingsExW(device, ref dm, IntPtr.Zero, 0, IntPtr.Zero);
        }
    }

    public static class Ccd
    {
        const uint QDC_ALL_PATHS = 1, QDC_ONLY_ACTIVE_PATHS = 2;
        const uint SDC_USE_SUPPLIED_DISPLAY_CONFIG = 0x20, SDC_VALIDATE = 0x40, SDC_APPLY = 0x80, SDC_ALLOW_CHANGES = 0x400;
        const uint PATH_ACTIVE = 1, MODE_IDX_INVALID = 0xFFFFFFFF;

        [StructLayout(LayoutKind.Explicit, Size = 72)]
        public struct PathInfo
        {
            [FieldOffset(0)] public uint SrcAdapterLow; [FieldOffset(4)] public int SrcAdapterHigh;
            [FieldOffset(8)] public uint SrcId; [FieldOffset(12)] public uint SrcModeIdx; [FieldOffset(16)] public uint SrcStatus;
            [FieldOffset(20)] public uint TgtAdapterLow; [FieldOffset(24)] public int TgtAdapterHigh;
            [FieldOffset(28)] public uint TgtId; [FieldOffset(32)] public uint TgtModeIdx; [FieldOffset(36)] public uint OutputTechnology;
            [FieldOffset(40)] public uint Rotation; [FieldOffset(44)] public uint Scaling;
            [FieldOffset(48)] public uint RefreshNum; [FieldOffset(52)] public uint RefreshDen;
            [FieldOffset(56)] public uint ScanLineOrdering; [FieldOffset(60)] public int TargetAvailable;
            [FieldOffset(64)] public uint TgtStatus; [FieldOffset(68)] public uint Flags;
        }

        [StructLayout(LayoutKind.Explicit, Size = 64)]
        public struct ModeInfo
        {
            [FieldOffset(0)] public uint InfoType; [FieldOffset(4)] public uint Id;
            [FieldOffset(8)] public uint AdapterLow; [FieldOffset(12)] public int AdapterHigh;
            [FieldOffset(16)] public ulong R0; [FieldOffset(24)] public ulong R1; [FieldOffset(32)] public ulong R2;
            [FieldOffset(40)] public ulong R3; [FieldOffset(48)] public ulong R4; [FieldOffset(56)] public ulong R5;
            [FieldOffset(16)] public uint SrcWidth; [FieldOffset(20)] public uint SrcHeight;
            [FieldOffset(24)] public uint SrcPixelFormat; [FieldOffset(28)] public int SrcX; [FieldOffset(32)] public int SrcY;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct TargetName
        {
            public uint Type; public uint Size; public uint AdapterLow; public int AdapterHigh; public uint Id;
            public uint Flags; public uint OutputTechnology; public ushort EdidManufactureId; public ushort EdidProductCodeId;
            public uint ConnectorInstance;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string FriendlyName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DevicePath;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct SourceName
        {
            public uint Type; public uint Size; public uint AdapterLow; public int AdapterHigh; public uint Id;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string GdiName;
        }

        [DllImport("user32.dll")] static extern int GetDisplayConfigBufferSizes(uint flags, out uint numPath, out uint numMode);
        [DllImport("user32.dll")] static extern int QueryDisplayConfig(uint flags, ref uint numPath, [Out] PathInfo[] paths, ref uint numMode, [Out] ModeInfo[] modes, IntPtr topology);
        [DllImport("user32.dll")] static extern int SetDisplayConfig(uint numPath, [In] PathInfo[] paths, uint numMode, [In] ModeInfo[] modes, uint flags);
        [DllImport("user32.dll")] static extern int DisplayConfigGetDeviceInfo(ref TargetName info);
        [DllImport("user32.dll")] static extern int DisplayConfigGetDeviceInfo(ref SourceName info);

        public class Config { public PathInfo[] Paths; public ModeInfo[] Modes; }

        public static Config Query(bool activeOnly)
        {
            uint flags = activeOnly ? QDC_ONLY_ACTIVE_PATHS : QDC_ALL_PATHS;
            for (int attempt = 0; attempt < 5; attempt++)
            {
                uint np, nm;
                int rc = GetDisplayConfigBufferSizes(flags, out np, out nm);
                if (rc != 0) throw new InvalidOperationException("GetDisplayConfigBufferSizes failed: " + rc);
                PathInfo[] paths = new PathInfo[np];
                ModeInfo[] modes = new ModeInfo[nm];
                rc = QueryDisplayConfig(flags, ref np, paths, ref nm, modes, IntPtr.Zero);
                if (rc == 122) continue;
                if (rc != 0) throw new InvalidOperationException("QueryDisplayConfig failed: " + rc);
                Array.Resize(ref paths, (int)np);
                Array.Resize(ref modes, (int)nm);
                Config c = new Config(); c.Paths = paths; c.Modes = modes;
                return c;
            }
            throw new InvalidOperationException("QueryDisplayConfig kept changing size");
        }

        static void TargetNames(PathInfo p, out string friendly, out string device)
        {
            TargetName t = new TargetName();
            t.Type = 2; t.Size = (uint)Marshal.SizeOf(typeof(TargetName));
            t.AdapterLow = p.TgtAdapterLow; t.AdapterHigh = p.TgtAdapterHigh; t.Id = p.TgtId;
            if (DisplayConfigGetDeviceInfo(ref t) == 0) { friendly = t.FriendlyName ?? ""; device = t.DevicePath ?? ""; }
            else { friendly = ""; device = ""; }
        }

        static string GdiName(PathInfo p)
        {
            SourceName s = new SourceName();
            s.Type = 1; s.Size = (uint)Marshal.SizeOf(typeof(SourceName));
            s.AdapterLow = p.SrcAdapterLow; s.AdapterHigh = p.SrcAdapterHigh; s.Id = p.SrcId;
            return DisplayConfigGetDeviceInfo(ref s) == 0 ? (s.GdiName ?? "") : "";
        }

        static bool Matches(string friendly, string device, string match, string friendlyMatch)
        {
            return device.IndexOf(match, StringComparison.OrdinalIgnoreCase) >= 0 ||
                   string.Equals(friendly, friendlyMatch, StringComparison.OrdinalIgnoreCase);
        }

        static bool IsVdd(PathInfo p, string match, string friendlyMatch)
        {
            string f, d; TargetNames(p, out f, out d);
            return Matches(f, d, match, friendlyMatch);
        }

        public static List<TargetDesc> Describe(bool activeOnly, string match, string friendlyMatch)
        {
            Config c = Query(activeOnly);
            List<TargetDesc> list = new List<TargetDesc>();
            foreach (PathInfo p in c.Paths)
            {
                TargetDesc t = new TargetDesc();
                t.AdapterLow = p.TgtAdapterLow; t.AdapterHigh = p.TgtAdapterHigh; t.SourceId = p.SrcId; t.TargetId = p.TgtId;
                t.Active = (p.Flags & PATH_ACTIVE) != 0; t.Available = p.TargetAvailable != 0;
                string f, d; TargetNames(p, out f, out d);
                t.FriendlyName = f; t.DevicePath = d; t.IsVdd = Matches(f, d, match, friendlyMatch);
                t.Rotation = p.Rotation;
                t.Refresh = p.RefreshDen == 0 ? 0 : Math.Round((double)p.RefreshNum / p.RefreshDen, 2);
                if (t.Active)
                {
                    t.GdiName = GdiName(p);
                    if (p.SrcModeIdx != MODE_IDX_INVALID && p.SrcModeIdx < c.Modes.Length && c.Modes[p.SrcModeIdx].InfoType == 1)
                    {
                        ModeInfo m = c.Modes[p.SrcModeIdx];
                        t.HasMode = true; t.X = m.SrcX; t.Y = m.SrcY; t.Width = (int)m.SrcWidth; t.Height = (int)m.SrcHeight;
                        t.Primary = (m.SrcX == 0 && m.SrcY == 0);
                    }
                }
                list.Add(t);
            }
            return list;
        }

        static byte[] ToBytes<T>(T[] items) where T : struct
        {
            int size = Marshal.SizeOf(typeof(T));
            byte[] bytes = new byte[size * items.Length];
            IntPtr buf = Marshal.AllocHGlobal(size);
            try
            {
                for (int i = 0; i < items.Length; i++) { Marshal.StructureToPtr(items[i], buf, false); Marshal.Copy(buf, bytes, i * size, size); }
            }
            finally { Marshal.FreeHGlobal(buf); }
            return bytes;
        }

        static T[] FromBytes<T>(byte[] bytes) where T : struct
        {
            int size = Marshal.SizeOf(typeof(T));
            T[] items = new T[bytes.Length / size];
            IntPtr buf = Marshal.AllocHGlobal(size);
            try
            {
                for (int i = 0; i < items.Length; i++) { Marshal.Copy(bytes, i * size, buf, size); items[i] = (T)Marshal.PtrToStructure(buf, typeof(T)); }
            }
            finally { Marshal.FreeHGlobal(buf); }
            return items;
        }

        // The active configuration as two base64 strings (paths, modes), for display-before.json.
        public static string[] SaveActive()
        {
            Config c = Query(true);
            return new string[] { Convert.ToBase64String(ToBytes(c.Paths)), Convert.ToBase64String(ToBytes(c.Modes)) };
        }

        static uint Flags(bool validateOnly)
        {
            return SDC_USE_SUPPLIED_DISPLAY_CONFIG | SDC_ALLOW_CHANGES | (validateOnly ? SDC_VALIDATE : SDC_APPLY);
        }

        static string Result(int rc)
        {
            if (rc == 0) return "";
            if (rc == 5) return "access denied by SetDisplayConfig: the change would need elevation, and the rig scripts never elevate";
            return "SetDisplayConfig failed: " + rc;
        }

        // Extends the desktop onto the virtual monitor: the active paths plus one path to it, with its
        // modes left for Windows to choose. Returns "" on success, "already-active", or an error.
        public static string AddVdd(string match, string friendlyMatch, bool validateOnly)
        {
            Config active = Query(true);
            foreach (PathInfo a in active.Paths) { if (IsVdd(a, match, friendlyMatch)) return "already-active"; }
            Config all = Query(false);
            int index = -1;
            for (int i = 0; i < all.Paths.Length && index < 0; i++)
            {
                PathInfo p = all.Paths[i];
                if (p.TargetAvailable == 0 || !IsVdd(p, match, friendlyMatch)) continue;
                bool sourceUsed = false;
                foreach (PathInfo a in active.Paths)
                {
                    if (a.SrcAdapterLow == p.SrcAdapterLow && a.SrcAdapterHigh == p.SrcAdapterHigh && a.SrcId == p.SrcId) sourceUsed = true;
                }
                if (!sourceUsed) index = i;
            }
            if (index < 0) return "no available path to the virtual monitor";
            PathInfo[] paths = new PathInfo[active.Paths.Length + 1];
            Array.Copy(active.Paths, paths, active.Paths.Length);
            PathInfo added = all.Paths[index];
            added.Flags |= PATH_ACTIVE; added.SrcModeIdx = MODE_IDX_INVALID; added.TgtModeIdx = MODE_IDX_INVALID;
            paths[paths.Length - 1] = added;
            int rc = SetDisplayConfig((uint)paths.Length, paths, (uint)active.Modes.Length, active.Modes, Flags(validateOnly));
            return Result(rc);
        }

        // Deactivates the virtual monitor's path and keeps every other active path as it is.
        public static string RemoveVdd(string match, string friendlyMatch, bool validateOnly)
        {
            Config active = Query(true);
            List<PathInfo> keep = new List<PathInfo>();
            foreach (PathInfo a in active.Paths) { if (!IsVdd(a, match, friendlyMatch)) keep.Add(a); }
            if (keep.Count == active.Paths.Length) return "not-active";
            List<ModeInfo> modes = new List<ModeInfo>();
            Dictionary<uint, uint> map = new Dictionary<uint, uint>();
            PathInfo[] paths = keep.ToArray();
            for (int i = 0; i < paths.Length; i++)
            {
                paths[i].SrcModeIdx = Remap(paths[i].SrcModeIdx, active.Modes, modes, map);
                paths[i].TgtModeIdx = Remap(paths[i].TgtModeIdx, active.Modes, modes, map);
            }
            ModeInfo[] modeArray = modes.ToArray();
            int rc = SetDisplayConfig((uint)paths.Length, paths, (uint)modeArray.Length, modeArray, Flags(validateOnly));
            return Result(rc);
        }

        static uint Remap(uint idx, ModeInfo[] source, List<ModeInfo> dest, Dictionary<uint, uint> map)
        {
            if (idx == MODE_IDX_INVALID || idx >= source.Length) return MODE_IDX_INVALID;
            uint mapped;
            if (!map.TryGetValue(idx, out mapped)) { mapped = (uint)dest.Count; dest.Add(source[idx]); map[idx] = mapped; }
            return mapped;
        }

        // Applies a configuration saved by SaveActive.
        public static string Restore(string pathsB64, string modesB64, bool validateOnly)
        {
            PathInfo[] paths = FromBytes<PathInfo>(Convert.FromBase64String(pathsB64));
            ModeInfo[] modes = FromBytes<ModeInfo>(Convert.FromBase64String(modesB64));
            int rc = SetDisplayConfig((uint)paths.Length, paths, (uint)modes.Length, modes, Flags(validateOnly));
            return Result(rc);
        }
    }

    [ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorCom { }

    [ComImport, Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceEnumerator
    {
        [PreserveSig] int EnumAudioEndpoints(int dataFlow, int stateMask, out IMMDeviceCollection devices);
    }

    [ComImport, Guid("0BD7A1BE-7A1A-44DB-8397-CC5392387B5E"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDeviceCollection
    {
        [PreserveSig] int GetCount(out uint count);
        [PreserveSig] int Item(uint index, out IMMDevice device);
    }

    [ComImport, Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IMMDevice
    {
        [PreserveSig] int Activate(ref Guid iid, int clsCtx, IntPtr activationParams, [MarshalAs(UnmanagedType.IUnknown)] out object iface);
    }

    [ComImport, Guid("77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioSessionManager2
    {
        [PreserveSig] int GetAudioSessionControl(IntPtr guid, uint flags, out IntPtr control);
        [PreserveSig] int GetSimpleAudioVolume(IntPtr guid, uint flags, out IntPtr volume);
        [PreserveSig] int GetSessionEnumerator(out IAudioSessionEnumerator sessions);
    }

    [ComImport, Guid("E2F5BB11-0570-40CA-ACDD-3AA01277DEE8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioSessionEnumerator
    {
        [PreserveSig] int GetCount(out int count);
        [PreserveSig] int GetSession(int index, out IAudioSessionControl2 session);
    }

    [ComImport, Guid("bfb7ff88-7239-4fc9-8fa2-07c950be9c6d"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface IAudioSessionControl2
    {
        [PreserveSig] int GetState(out int state);
        [PreserveSig] int GetDisplayName(out IntPtr name);
        [PreserveSig] int SetDisplayName(IntPtr name, IntPtr context);
        [PreserveSig] int GetIconPath(out IntPtr path);
        [PreserveSig] int SetIconPath(IntPtr path, IntPtr context);
        [PreserveSig] int GetGroupingParam(out Guid param);
        [PreserveSig] int SetGroupingParam(IntPtr param, IntPtr context);
        [PreserveSig] int RegisterAudioSessionNotification(IntPtr client);
        [PreserveSig] int UnregisterAudioSessionNotification(IntPtr client);
        [PreserveSig] int GetSessionIdentifier(out IntPtr id);
        [PreserveSig] int GetSessionInstanceIdentifier(out IntPtr id);
        [PreserveSig] int GetProcessId(out uint pid);
    }

    [ComImport, Guid("87CE5498-68D6-44E5-9215-6DA47EF883D8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    interface ISimpleAudioVolume
    {
        [PreserveSig] int SetMasterVolume(float level, ref Guid context);
        [PreserveSig] int GetMasterVolume(out float level);
        [PreserveSig] int SetMute(int mute, ref Guid context);
        [PreserveSig] int GetMute(out int mute);
    }

    public class AudioSessionState { public int Pid; public bool Muted; }

    // Mute of a process's own audio sessions on every active render device. The system volume is
    // never touched.
    public static class Audio
    {
        static List<KeyValuePair<int, ISimpleAudioVolume>> Sessions(int[] pids)
        {
            HashSet<int> set = new HashSet<int>(pids);
            List<KeyValuePair<int, ISimpleAudioVolume>> list = new List<KeyValuePair<int, ISimpleAudioVolume>>();
            IMMDeviceEnumerator enumerator = (IMMDeviceEnumerator)(new MMDeviceEnumeratorCom());
            IMMDeviceCollection devices;
            if (enumerator.EnumAudioEndpoints(0, 1, out devices) != 0) return list;
            uint count; devices.GetCount(out count);
            Guid iid = typeof(IAudioSessionManager2).GUID;
            for (uint i = 0; i < count; i++)
            {
                IMMDevice device;
                if (devices.Item(i, out device) != 0) continue;
                object o;
                if (device.Activate(ref iid, 23, IntPtr.Zero, out o) != 0) continue;
                IAudioSessionManager2 manager = (IAudioSessionManager2)o;
                IAudioSessionEnumerator sessions;
                if (manager.GetSessionEnumerator(out sessions) != 0) continue;
                int n; sessions.GetCount(out n);
                for (int s = 0; s < n; s++)
                {
                    IAudioSessionControl2 control;
                    if (sessions.GetSession(s, out control) != 0) continue;
                    uint pid;
                    if (control.GetProcessId(out pid) != 0 || !set.Contains((int)pid)) continue;
                    ISimpleAudioVolume volume = control as ISimpleAudioVolume;
                    if (volume != null) list.Add(new KeyValuePair<int, ISimpleAudioVolume>((int)pid, volume));
                }
            }
            return list;
        }

        public static List<AudioSessionState> Query(int[] pids)
        {
            List<AudioSessionState> result = new List<AudioSessionState>();
            foreach (KeyValuePair<int, ISimpleAudioVolume> kv in Sessions(pids))
            {
                int muted; kv.Value.GetMute(out muted);
                AudioSessionState s = new AudioSessionState(); s.Pid = kv.Key; s.Muted = muted != 0;
                result.Add(s);
            }
            return result;
        }

        public static int SetMute(int[] pids, bool mute)
        {
            int changed = 0;
            Guid context = Guid.Empty;
            foreach (KeyValuePair<int, ISimpleAudioVolume> kv in Sessions(pids))
            {
                if (kv.Value.SetMute(mute ? 1 : 0, ref context) == 0) changed++;
            }
            return changed;
        }
    }
}
'@

function Initialize-RigNative {
    if (-not ('EvrRig.Windows' -as [type])) {
        Add-Type -TypeDefinition $script:RigNativeSource -Language CSharp
    }
    [EvrRig.Windows]::MakeDpiAware()
}

# ---------------------------------------------------------------------------------------------------
# Virtual display. Real mode uses the CCD API on the 'VDD by MTT' monitor; test mode uses a JSON stub.
# DISPLAY_ADDED (in the runs root) marks a display we added for the current work block; the topology
# before the add is kept in display-before.json and every action is appended to display-log.jsonl.
# No display change is made while a game process runs, and a failed query is an error, never
# "unavailable".

function Get-RigDisplayStub($cfg) {
    $stub = Read-RigJson $cfg.DisplayStub
    if ($null -eq $stub) { $stub = New-Object psobject }
    $defaults = [ordered]@{ available = $true; active = $false; failAdd = $false; queryError = $null
        rect = [pscustomobject]@{ x = -20000; y = -20000; width = 640; height = 360 }
        modes = @('640x360', '1920x1080', '2560x1440'); calls = @() }
    foreach ($k in $defaults.Keys) {
        if ($null -eq $stub.PSObject.Properties[$k]) { $stub | Add-Member -NotePropertyName $k -NotePropertyValue $defaults[$k] }
    }
    return $stub
}

function Save-RigDisplayStub($cfg, $stub) { Write-RigJson $cfg $cfg.DisplayStub $stub }

# Picks the requested mode (highest refresh), else the largest mode the display reports.
function Select-RigDisplayMode($Modes, [int]$Width, [int]$Height) {
    $exact = @($Modes | Where-Object { $_.Width -eq $Width -and $_.Height -eq $Height } | Sort-Object Frequency -Descending)
    if ($exact.Count -gt 0) { return $exact[0] }
    return @($Modes | Sort-Object @{ Expression = { $_.Width * $_.Height } }, Frequency -Descending)[0]
}

function Get-RigDisplayState($cfg) {
    if ($cfg.DisplayStub) {
        $stub = Get-RigDisplayStub $cfg
        if ($stub.queryError) {
            return [pscustomobject]@{ Mode = 'stub'; Available = $false; Active = $false; VddPrimary = $false; Rect = $null; VddMode = $null; LargestMode = $null; Topology = @(); Error = $stub.queryError }
        }
        $rect = $null; $vddMode = $null
        if ($stub.active) { $rect = $stub.rect; $vddMode = '{0}x{1}@60' -f $stub.rect.width, $stub.rect.height }
        $topology = @([ordered]@{ name = 'stub primary'; gdi = '\\.\DISPLAY1'; x = 0; y = 0; width = 1920; height = 1080; refresh = 60; rotation = 1; primary = $true; vdd = $false })
        if ($stub.active) {
            $topology += [ordered]@{ name = 'stub vdd'; gdi = '\\.\DISPLAY9'; x = $stub.rect.x; y = $stub.rect.y; width = $stub.rect.width; height = $stub.rect.height; refresh = 60; rotation = 1; primary = $false; vdd = $true }
        }
        return [pscustomobject]@{ Mode = 'stub'; Available = [bool]$stub.available; Active = [bool]$stub.active
            VddPrimary = $false; Rect = $rect; VddMode = $vddMode; LargestMode = @($stub.modes)[-1]; Topology = $topology; Error = $null }
    }
    Initialize-RigNative
    try {
        $active = @([EvrRig.Ccd]::Describe($true, $script:RigVddMatch, $script:RigVddFriendlyName))
        $all = @([EvrRig.Ccd]::Describe($false, $script:RigVddMatch, $script:RigVddFriendlyName))
    } catch {
        return [pscustomobject]@{ Mode = 'ccd'; Available = $false; Active = $false; VddPrimary = $false; Rect = $null; VddMode = $null; LargestMode = $null; Topology = @(); Error = $_.Exception.Message }
    }
    $vddActive = @($active | Where-Object { $_.IsVdd })
    $available = [bool](@($all | Where-Object { $_.IsVdd -and $_.Available }).Count)
    $rect = $null; $vddMode = $null; $largest = $null
    if ($vddActive.Count -gt 0 -and $vddActive[0].HasMode) {
        $v = $vddActive[0]
        $rect = [pscustomobject]@{ x = $v.X; y = $v.Y; width = $v.Width; height = $v.Height }
        try {
            $cur = [EvrRig.DisplayModes]::Current($v.GdiName)
            if ($cur) { $vddMode = '{0}x{1}@{2}' -f $cur.Width, $cur.Height, $cur.Frequency }
            $big = Select-RigDisplayMode @([EvrRig.DisplayModes]::List($v.GdiName)) 0 0
            if ($big) { $largest = '{0}x{1}@{2}' -f $big.Width, $big.Height, $big.Frequency }
        } catch { }
    }
    $topology = @($active | ForEach-Object {
            [ordered]@{ name = $_.FriendlyName; gdi = $_.GdiName; x = $_.X; y = $_.Y; width = $_.Width; height = $_.Height
                refresh = $_.Refresh; rotation = $_.Rotation; primary = $_.Primary; vdd = $_.IsVdd }
        })
    return [pscustomobject]@{ Mode = 'ccd'; Available = $available; Active = ($vddActive.Count -gt 0)
        VddPrimary = [bool](@($vddActive | Where-Object { $_.Primary }).Count); Rect = $rect; VddMode = $vddMode; LargestMode = $largest
        Topology = $topology; Error = $null }
}

function Get-RigPrimaryName($State) {
    $p = @($State.Topology | Where-Object { $_.primary })
    if ($p.Count -gt 0) { return ('{0}|{1}' -f $p[0].name, $p[0].gdi) }
    return ''
}

function Format-RigTopologyKey($Entry) {
    return ('{0}|{1}|{2},{3}|{4}x{5}|{6}|{7}|{8}' -f $Entry.name, $Entry.gdi, $Entry.x, $Entry.y, $Entry.width, $Entry.height, $Entry.refresh, $Entry.rotation, $Entry.primary)
}

function Add-RigDisplayLog($cfg, [hashtable]$Entry) {
    $file = Join-Path $cfg.RunsRoot 'display-log.jsonl'
    Assert-RigWritable $cfg $file
    $Entry['time'] = (Get-Date).ToString('o')
    [IO.File]::AppendAllText($file, ((ConvertTo-Json -InputObject $Entry -Depth 8 -Compress) + "`n"), $script:RigUtf8)
}

function Get-RigDisplayMarker($cfg) { return Read-RigJson (Join-Path $cfg.RunsRoot 'DISPLAY_ADDED') }

function Test-RigGameRunningForDisplay($cfg) {
    $games = @(Get-RigGameProcesses $cfg)
    if ($games.Count -eq 0) { return $null }
    return ('a game process is running ({0}); no display change while the game runs' -f (($games | ForEach-Object { "$($_.ProcessName) $($_.Id)" }) -join ', '))
}

# Sets the virtual display's mode (only that device, dynamically). Returns a note.
function Set-RigDisplayMode($cfg, [int]$Width, [int]$Height) {
    if ($cfg.DisplayStub) {
        $stub = Get-RigDisplayStub $cfg
        $modes = @($stub.modes | ForEach-Object { $w, $h = $_ -split 'x'; [pscustomobject]@{ Width = [int]$w; Height = [int]$h; Frequency = 60 } })
        $pick = Select-RigDisplayMode $modes $Width $Height
        $stub.rect.width = $pick.Width; $stub.rect.height = $pick.Height
        $stub.calls = @($stub.calls) + @("mode $($pick.Width)x$($pick.Height)")
        Save-RigDisplayStub $cfg $stub
        return "mode $($pick.Width)x$($pick.Height)"
    }
    $state = Get-RigDisplayState $cfg
    $vdd = @($state.Topology | Where-Object { $_.vdd })
    if ($vdd.Count -eq 0) { return 'mode not set: the virtual display is not active' }
    $gdi = $vdd[0].gdi
    $modes = @([EvrRig.DisplayModes]::List($gdi) | Where-Object { $_.Bpp -ge 32 })
    if ($modes.Count -eq 0) { return "mode not set: $gdi reports no modes" }
    $pick = Select-RigDisplayMode $modes $Width $Height
    $cur = [EvrRig.DisplayModes]::Current($gdi)
    if ($cur -and $cur.Width -eq $pick.Width -and $cur.Height -eq $pick.Height -and $cur.Frequency -eq $pick.Frequency) {
        return "mode $($pick.Width)x$($pick.Height)@$($pick.Frequency) (already set)"
    }
    $rc = [EvrRig.DisplayModes]::Set($gdi, $pick.Width, $pick.Height, $pick.Frequency)
    if ($rc -ne 0) { return "mode change to $($pick.Width)x$($pick.Height) on $gdi failed ($rc); kept $($cur.Width)x$($cur.Height)" }
    Start-Sleep -Milliseconds 1000
    return "mode $($pick.Width)x$($pick.Height)@$($pick.Frequency) on $gdi"
}

# Ensures the virtual display is part of the desktop for this work block. Returns an object with Ok,
# Unavailable, Rect and Note.
function Add-RigDisplay($cfg, [string]$Session, [string]$By, [int]$Width = 2560, [int]$Height = 1440) {
    $markerFile = Join-Path $cfg.RunsRoot 'DISPLAY_ADDED'
    $busy = Test-RigGameRunningForDisplay $cfg
    if ($busy) { return [pscustomobject]@{ Ok = $false; Unavailable = $false; Rect = $null; Note = $busy } }
    $before = Get-RigDisplayState $cfg
    if ($before.Error) {
        return [pscustomobject]@{ Ok = $false; Unavailable = $false; Rect = $null; Note = "display configuration query failed: $($before.Error)" }
    }
    if (-not $before.Available -and -not $before.Active) {
        return [pscustomobject]@{ Ok = $false; Unavailable = $true; Rect = $null; Note = 'virtual monitor (VDD by MTT) not available' }
    }
    if ($before.Active) {
        $note = "virtual display already part of the desktop ($($before.VddMode))"
        if (-not (Test-Path -LiteralPath $markerFile)) { $note += ' (not added by us; left as it is)' }
        return [pscustomobject]@{ Ok = $true; Unavailable = $false; Rect = $before.Rect; Note = $note }
    }

    # The marker and the saved topology come first, so an interrupted add is still cleaned up.
    $saved = $null
    if ($before.Mode -eq 'ccd') { $saved = [EvrRig.Ccd]::SaveActive() }
    Write-RigJson $cfg (Join-Path $cfg.RunsRoot 'display-before.json') ([ordered]@{
            time = (Get-Date).ToString('o'); session = $Session; topology = $before.Topology
            paths = $(if ($saved) { $saved[0] } else { $null }); modes = $(if ($saved) { $saved[1] } else { $null })
        })
    Write-RigJson $cfg $markerFile ([ordered]@{ session = $Session; time = (Get-Date).ToString('o'); by = $By })

    $err = ''
    if ($before.Mode -eq 'stub') {
        $stub = Get-RigDisplayStub $cfg
        if ($stub.failAdd) { $err = 'stub add failure' } else { $stub.active = $true }
        $stub.calls = @($stub.calls) + @('add')
        Save-RigDisplayStub $cfg $stub
    } else {
        $err = [EvrRig.Ccd]::AddVdd($script:RigVddMatch, $script:RigVddFriendlyName, $false)
        if ($err -eq 'already-active') { $err = '' }
        Start-Sleep -Milliseconds 1500
    }
    $after = Get-RigDisplayState $cfg
    if (-not $err -and $after.Error) { $err = "display configuration query failed: $($after.Error)" }
    if (-not $err -and -not $after.Active) { $err = 'the virtual display did not become active' }
    if (-not $err -and ((Get-RigPrimaryName $after) -ne (Get-RigPrimaryName $before) -or $after.VddPrimary)) {
        $err = 'the primary display changed'
    }
    if ($err) {
        # Put the desktop back as it was; the marker stays unless that worked.
        if ($after.Active) { [void](Remove-RigDisplay $cfg -Reason "failed add: $err") }
        $final = Get-RigDisplayState $cfg
        if (-not $final.Error -and -not $final.Active -and (Test-Path -LiteralPath $markerFile)) { Remove-Item -LiteralPath $markerFile -Force }
        Add-RigDisplayLog $cfg @{ action = 'add'; session = $Session; ok = $false; error = $err; before = $before.Topology; after = $final.Topology }
        return [pscustomobject]@{ Ok = $false; Unavailable = $false; Rect = $null; Note = "adding the virtual display failed: $err" }
    }
    $modeNote = Set-RigDisplayMode $cfg $Width $Height
    $after = Get-RigDisplayState $cfg
    Add-RigDisplayLog $cfg @{ action = 'add'; session = $Session; ok = $true; mode = $modeNote; before = $before.Topology; after = $after.Topology }
    Write-RigLog ('virtual display added at {0},{1}, {2}' -f $after.Rect.x, $after.Rect.y, $modeNote)
    return [pscustomobject]@{ Ok = $true; Unavailable = $false; Rect = $after.Rect; Note = "virtual display added, $modeNote" }
}

# Removes the virtual display if we added it (DISPLAY_ADDED): only its path is deactivated, every other
# display stays as it is. The result is compared with the topology saved before the add.
function Remove-RigDisplay($cfg, [string]$Reason = '') {
    $markerFile = Join-Path $cfg.RunsRoot 'DISPLAY_ADDED'
    $beforeFile = Join-Path $cfg.RunsRoot 'display-before.json'
    if (-not (Test-Path -LiteralPath $markerFile)) {
        return [pscustomobject]@{ Ok = $true; Note = 'no DISPLAY_ADDED marker; nothing of ours to remove' }
    }
    $busy = Test-RigGameRunningForDisplay $cfg
    if ($busy) { return [pscustomobject]@{ Ok = $false; Note = "virtual display kept: $busy" } }
    $marker = Get-RigDisplayMarker $cfg
    $state = Get-RigDisplayState $cfg
    if ($state.Error) { return [pscustomobject]@{ Ok = $false; Note = "virtual display kept: display configuration query failed: $($state.Error)" } }
    $err = ''
    if ($state.Active) {
        if ($state.Mode -eq 'stub') {
            $stub = Get-RigDisplayStub $cfg
            $stub.active = $false
            $stub.calls = @($stub.calls) + @('remove')
            Save-RigDisplayStub $cfg $stub
        } else {
            $err = [EvrRig.Ccd]::RemoveVdd($script:RigVddMatch, $script:RigVddFriendlyName, $false)
            if ($err -eq 'not-active') { $err = '' }
            Start-Sleep -Milliseconds 1500
        }
    }
    $after = Get-RigDisplayState $cfg
    if (-not $err -and $after.Error) { $err = "display configuration query failed: $($after.Error)" }
    if (-not $err -and $after.Active) { $err = 'the virtual display is still active' }
    Add-RigDisplayLog $cfg @{ action = 'remove'; session = (Get-RigProp $marker 'session'); reason = $Reason; ok = (-not $err); error = $err; before = $state.Topology; after = $after.Topology }
    if ($err) { return [pscustomobject]@{ Ok = $false; Note = "removing the virtual display failed: $err" } }
    $saved = Read-RigJson $beforeFile
    if ($saved) {
        $was = @($saved.topology | Where-Object { -not $_.vdd } | ForEach-Object { Format-RigTopologyKey $_ } | Sort-Object) -join '; '
        $now = @($after.Topology | Where-Object { -not $_.vdd } | ForEach-Object { Format-RigTopologyKey $_ } | Sort-Object) -join '; '
        if ($was -eq $now) { Write-RigLog 'displays match the topology saved before the add' }
        else { Write-RigLog "displays differ from the topology saved before the add (left as they are): before [$was] now [$now]" 'WARN' }
    }
    Remove-Item -LiteralPath $markerFile -Force
    if (Test-Path -LiteralPath $beforeFile) { Remove-Item -LiteralPath $beforeFile -Force }
    Write-RigLog "virtual display removed ($Reason)"
    return [pscustomobject]@{ Ok = $true; Note = 'virtual display removed' }
}

# ---------------------------------------------------------------------------------------------------
# Runs

function Get-RigRunDirs($cfg) {
    if (-not (Test-Path -LiteralPath $cfg.RunsRoot)) { return @() }
    return @(Get-ChildItem -LiteralPath $cfg.RunsRoot -Directory | Where-Object { $_.Name -match '^\d{8}-\d{6}' } | Sort-Object Name | ForEach-Object { $_.FullName })
}

function Resolve-RigRunDir($cfg, [string]$Run) {
    if (-not $Run -or $Run -eq 'latest') {
        $dirs = @(Get-RigRunDirs $cfg)
        if ($dirs.Count -eq 0) { throw "No run folders in $($cfg.RunsRoot)" }
        return $dirs[-1]
    }
    if (Test-Path -LiteralPath $Run -PathType Container) { return (Get-Item -LiteralPath $Run).FullName }
    $candidate = Join-Path $cfg.RunsRoot $Run
    if (Test-Path -LiteralPath $candidate -PathType Container) { return $candidate }
    throw "Run folder not found: $Run"
}

function Test-RigPending([string]$RunDir) { return (Test-Path -LiteralPath (Join-Path $RunDir 'CLEANUP_PENDING')) }

# The processes of a run that are still alive: the recorded ones (PID and start time must both match,
# so a reused PID is never touched), plus, for a run killed between process start and recording, a
# process of the run's exe started within 60 s after the recorded launch time.
function Get-RigRunLiveProcesses($RunDir) {
    $run = Read-RigJson (Join-Path $RunDir 'run.json')
    $live = @()
    if ($null -eq $run) { return $live }
    $seen = @{}
    foreach ($rec in @(Get-RigProp $run 'processes' @())) {
        if (Test-RigProcessAlive $rec.pid $rec.startFileTimeUtc) {
            $p = Get-Process -Id $rec.pid -ErrorAction SilentlyContinue
            if ($p) { $live += $p; $seen[$p.Id] = $true }
        }
    }
    $exe = Get-RigProp $run 'exe'
    $launch = Get-RigProp $run 'launchFileTimeUtc'
    if ($exe -and $launch) {
        $name = [IO.Path]::GetFileNameWithoutExtension($exe.path)
        foreach ($p in @(Get-Process -Name $name -ErrorAction SilentlyContinue)) {
            if ($seen.ContainsKey($p.Id)) { continue }
            $gone = $false; try { $gone = $p.HasExited } catch { }
            if ($gone) { continue }
            $path = $null; try { $path = $p.Path } catch { }
            $start = Get-RigProcessStartFileTime $p
            if ($path -and $start -and ($path -eq $exe.path) -and ($start -ge ([long]$launch - 20000000)) -and ($start -le ([long]$launch + 600000000))) {
                $live += $p; $seen[$p.Id] = $true
            }
        }
    }
    return $live
}

function Get-RigCloseTargets([int[]]$Pids) {
    Initialize-RigNative
    $windows = @([EvrRig.Windows]::TopLevel($Pids))
    $visible = @($windows | Where-Object { $_.Visible })
    if ($visible.Count -gt 0) { return $visible }
    return @($windows | Where-Object { $_.ClassName -ne 'IME' -and $_.ClassName -ne 'MSCTFIME UI' })
}

# Stops the run's live processes: put the audio session mute back to its prior state, WM_CLOSE to their
# top-level windows, wait CloseTimeoutSec, then a forced stop, then wait (bounded) until each process is
# gone from the process list. Appends the outcome to stop.json and returns it.
function Stop-RigRunProcesses($cfg, [string]$RunDir) {
    $runFile = Join-Path $RunDir 'run.json'
    $run = Read-RigJson $runFile
    $procs = @(Get-RigRunLiveProcesses $RunDir)
    $mute = Get-RigProp $run 'sessionMute'
    $muteApplied = [bool](Get-RigProp $mute 'applied' $false)
    $muteRestored = [bool](Get-RigProp $mute 'restored' $false)
    $result = [ordered]@{ time = (Get-Date).ToString('o'); processes = @(); unmuted = $false; muteMayPersist = $false }

    if ($muteApplied -and -not $muteRestored) {
        $prior = [bool](Get-RigProp $mute 'priorMuted' $false)
        if ($procs.Count -gt 0) {
            try {
                Initialize-RigNative
                $n = [EvrRig.Audio]::SetMute([int[]]@($procs | ForEach-Object { $_.Id }), $prior)
                $result.unmuted = $true
                Write-RigLog "audio session mute put back to its prior state (muted=$prior) on $n session(s)"
                $mute | Add-Member -NotePropertyName restored -NotePropertyValue $true -Force
                Update-RigJson $cfg $runFile @{ sessionMute = $mute }
            } catch {
                Write-RigLog "restoring the audio session mute failed: $($_.Exception.Message)" 'WARN'
            }
        }
        if (-not $result.unmuted) {
            if ($prior) {
                Write-RigLog 'the game exited while muted by us; it was muted before as well, so nothing persists that was not there'
            } else {
                $result.muteMayPersist = $true
                Update-RigJson $cfg $runFile @{ muteMayPersist = $true }
                Write-RigLog 'the game exited while its audio session was muted by us: mute may persist for its next launch (Windows mixer)' 'WARN'
            }
        }
    }

    if ($procs.Count -gt 0) {
        $pids = [int[]]@($procs | ForEach-Object { $_.Id })
        $targets = @(Get-RigCloseTargets $pids)
        foreach ($w in $targets) { [void][EvrRig.Windows]::Close($w.Handle) }
        Write-RigLog ("window-close request sent to {0} window(s) of pid(s) {1}" -f $targets.Count, ($pids -join ','))
        $deadline = (Get-Date).AddSeconds($cfg.CloseTimeoutSec)
        while ((Get-Date) -lt $deadline -and @($procs | Where-Object { -not $_.HasExited }).Count -gt 0) {
            Start-Sleep -Milliseconds 250
        }
        foreach ($p in $procs) {
            $outcome = 'closed'
            if (-not $p.HasExited) {
                try { $p.Kill() } catch { }
                if ($p.WaitForExit($cfg.KillTimeoutSec * 1000)) { $outcome = 'killed' } else { $outcome = 'still-running' }
            }
            $code = $null; try { $code = $p.ExitCode } catch { }
            $id = $p.Id
            $p.Dispose()
            # Exit is signalled before the process leaves the process list; wait until it has.
            $goneBy = (Get-Date).AddSeconds($cfg.KillTimeoutSec)
            while ((Get-Date) -lt $goneBy -and (Get-Process -Id $id -ErrorAction SilentlyContinue)) { Start-Sleep -Milliseconds 100 }
            $listed = [bool](Get-Process -Id $id -ErrorAction SilentlyContinue)
            $result.processes += [ordered]@{ pid = $id; outcome = $outcome; exitCode = $code; stillListed = $listed }
            Write-RigLog ("process {0}: {1}{2}" -f $id, $outcome, $(if ($listed) { ' (still listed after the wait)' } else { '' }))
        }
    }

    $stopFile = Join-Path $RunDir 'stop.json'
    $history = @(Read-RigJson $stopFile)
    Write-RigJson $cfg $stopFile (@($history | Where-Object { $_ }) + @($result))
    return $result
}

# Completes one run's cleanup (T-089 procedure as the T-097 contract). Returns Ok, Skipped and Reason.
# Idempotent: a run without CLEANUP_PENDING is left alone. Only -AllowStop (stop.ps1) stops a live
# run; an owner run is only cleaned when named explicitly (-Explicit). Steam-Cloud files are kept as the
# game left them and reported; -RestoreCloud restores them and runs the resync launch (T-115).
function Invoke-RigRunCleanup($cfg, [string]$RunDir, [switch]$Explicit, [switch]$AllowStop, [switch]$Acknowledge, [switch]$RestoreCloud) {
    if (-not $cfg.OwnerSettingsGuard) { $Acknowledge = [switch]$true }
    $marker = Join-Path $RunDir 'CLEANUP_PENDING'
    $name = Split-Path -Leaf $RunDir
    if (-not (Test-Path -LiteralPath $marker)) { return [pscustomobject]@{ Ok = $true; Skipped = $false; Reason = 'not pending' } }
    $markerInfo = $null; try { $markerInfo = Read-RigJson $marker } catch { }
    $setupPid = Get-RigProp $markerInfo 'pid'
    if ($setupPid -and [int]$setupPid -ne $PID -and (Test-RigProcessAlive $setupPid (Get-RigProp $markerInfo 'startFileTimeUtc'))) {
        return [pscustomobject]@{ Ok = $false; Skipped = $true; Reason = "run.ps1 (pid $setupPid) is still setting up $name" }
    }
    $run = Read-RigJson (Join-Path $RunDir 'run.json')
    if ((Get-RigProp $run 'owner' $false) -and -not $Explicit) {
        return [pscustomobject]@{ Ok = $false; Skipped = $true; Reason = "owner run $name is not cleaned implicitly: finish it with stop.ps1 -Run $name" }
    }
    $live = @(Get-RigRunLiveProcesses $RunDir)
    if ($live.Count -gt 0 -and -not $AllowStop) {
        return [pscustomobject]@{ Ok = $false; Skipped = $true; Reason = ("$name is still running (pid {0}): stop it with stop.ps1 -Run $name" -f (($live | ForEach-Object { $_.Id }) -join ',')) }
    }
    $previousLogs = $script:RigLogFiles
    Set-RigLogFiles (@($previousLogs) + @(Join-Path $RunDir 'rig.log'))
    try {
        Write-RigLog "cleanup of $RunDir"
        $record = [ordered]@{ time = (Get-Date).ToString('o'); outcome = 'failed'; reason = ''; changed = @(); removed = @(); added = @(); held = @(); restored = @(); replaced = $null }
        $fail = {
            param($why)
            $record.reason = $why
            Write-RigJson $cfg (Join-Path $RunDir 'cleanup.json') $record
            Write-RigLog "cleanup FAILED, CLEANUP_PENDING kept: $why" 'ERROR'
            return [pscustomobject]@{ Ok = $false; Skipped = $false; Reason = $why }
        }

        # 1. Stop what is still running (bounded; only with -AllowStop), undo the session mute.
        $stop = Stop-RigRunProcesses $cfg $RunDir
        if (@($stop.processes | Where-Object { $_.outcome -eq 'still-running' }).Count -gt 0) {
            return (& $fail 'a recorded process could not be stopped')
        }
        $deadline = (Get-Date).AddSeconds($cfg.KillTimeoutSec)
        $others = @(Get-RigGameProcesses $cfg)
        while ($others.Count -gt 0 -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 250; $others = @(Get-RigGameProcesses $cfg) }
        if ($others.Count -gt 0) {
            return (& $fail ("a game process is running ({0}); restore deferred" -f (($others | ForEach-Object { "$($_.ProcessName) $($_.Id)" }) -join ', ')))
        }

        # 2. The snapshot must be complete and intact.
        $before = Join-Path $RunDir 'config-before'
        if (-not (Test-Path -LiteralPath $before)) {
            if (-not (Get-RigProp $run 'launchFileTimeUtc')) {
                $record.outcome = 'nothing-to-restore'
                $record.reason = 'the run stopped before its snapshot was complete; the game was never started'
                Write-RigJson $cfg (Join-Path $RunDir 'cleanup.json') $record
                Remove-Item -LiteralPath $marker -Force
                Write-RigLog "cleanup done: $($record.reason)"
                return [pscustomobject]@{ Ok = $true; Skipped = $false; Reason = $record.reason }
            }
            return (& $fail 'config-before is missing although the game was started')
        }
        $problems = @(Test-SettingsSnapshot $before)
        if ($problems.Count -gt 0) { return (& $fail ('config-before failed verification: ' + ($problems -join '; '))) }
        $locations = Get-SnapshotLocations $before
        $manifest = Read-RigManifest (Join-Path $before 'SHA256SUMS')

        # 3. Steam's post-exit sync must be over before anything is copied or written.
        if (-not (Wait-RigSteamSync $cfg)) {
            return (& $fail "Steam's 782330 folder kept changing for $($cfg.SyncMaxSec) s")
        }

        # 4. Keep the post-exit state (only once: a retry must not overwrite it).
        Remove-RigRestoreTemps $cfg $locations
        $afterDir = Join-Path $RunDir 'config-after'
        if (-not (Test-Path -LiteralPath $afterDir)) {
            $liveLocations = @($locations | ForEach-Object { [pscustomobject]@{ Name = $_.name; Kind = $_.kind; Path = $_.path } })
            [void](New-SettingsSnapshot $cfg $RunDir 'config-after' $liveLocations)
        }
        $afterManifest = Read-RigManifest (Join-Path $afterDir 'SHA256SUMS')
        Invoke-RigAbortPoint $cfg 'cleanup-after-copy'

        # 5. Classify and restore (T-115). Local files (the Saved Games tree) that changed or were
        # removed are restored whole. Steam-Cloud files (782330\remote) are kept as the game left them
        # and reported with their before and after hashes and sizes; only -RestoreCloudFiles restores
        # them, followed by the resync step (6). A file about to be restored that differs from its
        # post-exit state was changed after the run ended (by the owner or by Steam): it is held, not
        # overwritten, unless acknowledged. Every file about to be overwritten is first kept in
        # config-replaced\<attempt>\. Added files are listed, never deleted.
        $diff = Compare-SettingsWithManifest $locations $manifest
        $record.changed = @($diff.Changed); $record.removed = @($diff.Removed); $record.added = @($diff.Added)
        if ($diff.Unreadable.Count -gt 0) { return (& $fail ('unreadable settings files: ' + ($diff.Unreadable -join ', '))) }
        $cloudNames = @(Get-RigCloudLocationNames $locations)
        $record.cloudPolicy = $(if ($RestoreCloud) { 'restore-and-resync' } else { 'keep-game-version' })
        $record.cloudFiles = @(); $record.cloudKept = @(); $record.cloudWarnings = @()
        $forced = @(Get-RigForcedCvars $run)
        $toRestore = @()
        foreach ($key in (@($diff.Changed) + @($diff.Removed) + @($diff.Added))) {
            $isAdded = ($diff.Added -contains $key)
            if (-not (Test-RigCloudKey $cloudNames $key)) {
                if (-not $isAdded) { $toRestore += $key }
                continue
            }
            $change = 'changed'
            if ($diff.Removed -contains $key) { $change = 'removed' } elseif ($isAdded) { $change = 'added' }
            $target = Resolve-RigSnapshotTarget $locations $key
            $entry = [ordered]@{ key = $key; change = $change; before = (Get-RigFileFacts (Join-Path $before $key)); after = (Get-RigFileFacts (Join-Path $afterDir $key))
                live = (Get-RigFileFacts $target); action = 'kept'; forcedCvarsFound = @() }
            if ($RestoreCloud -and -not $isAdded) { $entry.action = 'restore'; $toRestore += $key }
            if ($change -ne 'removed') {
                $found = @(Find-RigCvarsInCloudFile (Join-Path $before $key) $target $forced)
                $entry.forcedCvarsFound = $found
                if ($found.Count -gt 0) {
                    $w = "forced cvar name(s) $($found -join ', ') occur more often in $key than before the run: a runtime value may have been saved into this Steam Cloud file (T-092)"
                    if ($entry.action -eq 'kept') { $w += '; the file is kept as the game left it' }
                    $record.cloudWarnings += $w
                    Write-RigLog $w 'WARN'
                }
            }
            if ($entry.action -eq 'kept') {
                if ($isAdded) {
                    Write-RigLog ("Steam Cloud file added during the run (kept): {0}, {1}" -f $key, (Format-RigFacts $entry.live)) 'WARN'
                } else {
                    $record.cloudKept += $key
                    Write-RigLog ("Steam Cloud file {0} during the run, kept as the game left it (not restored): {1}; before {2}, now {3}" -f $change, $key, (Format-RigFacts $entry.before), (Format-RigFacts $entry.live)) 'WARN'
                }
            }
            $record.cloudFiles += $entry
        }
        if ($record.cloudKept.Count -gt 0) {
            Write-RigLog ("{0} Steam Cloud file(s) keep the game's version, so Steam's record stays consistent. To put the pre-run versions back, clean the run with -RestoreCloudFiles while it is pending (restore, then the resync launch); config-before\ holds them" -f $record.cloudKept.Count) 'WARN'
        }
        $keptPresent = @($record.cloudFiles | Where-Object { $_.action -eq 'kept' -and $_.change -ne 'removed' })
        if ($forced.Count -gt 0 -and $keptPresent.Count -gt 0) {
            $w = "the run forced $($forced -join ', ') and these Steam Cloud file(s) changed and are kept: $(@($keptPresent | ForEach-Object { $_.key }) -join ', '); check that they do not carry those values (T-092)"
            $record.cloudWarnings += $w
            Write-RigLog $w 'WARN'
        }
        $replacedDir = Join-Path $RunDir ('config-replaced\' + (Get-Date).ToString('yyyyMMdd-HHmmss-fff'))
        $errors = @(); $held = @()
        $first = $true
        foreach ($key in $toRestore) {
            $target = Resolve-RigSnapshotTarget $locations $key
            try {
                $liveHash = $null
                if (Test-Path -LiteralPath $target) { $liveHash = Get-RigSha256 $target }
                $afterHash = $null
                if ($afterManifest.ContainsKey($key)) { $afterHash = $afterManifest[$key] }
                if ($liveHash -ne $afterHash) {
                    if (-not $Acknowledge) { $held += $key; Write-RigLog "held (changed after the run ended): $key" 'WARN'; continue }
                    Write-RigLog "restoring over a file changed after the run ended (acknowledged): $key" 'WARN'
                }
                if (Restore-RigRunKey $cfg $locations $before $manifest $key $replacedDir) { $record.replaced = $replacedDir }
                $record.restored += $key
                Write-RigLog "restored $key"
            } catch {
                $errors += "$key ($($_.Exception.Message))"
            }
            if ($first) { $first = $false; Invoke-RigAbortPoint $cfg 'cleanup-mid-restore' }
        }
        foreach ($key in @($diff.Added | Where-Object { -not (Test-RigCloudKey $cloudNames $_) })) { Write-RigLog "file added during the run (kept): $key" }
        $record.held = $held
        if ($errors.Count -gt 0) { return (& $fail ('restore failed: ' + ($errors -join '; '))) }
        if ($held.Count -gt 0) {
            return (& $fail ("changed after the run ended, kept as they are and not restored: {0}. Report this to the owner; once he agrees, run cleanup.ps1 -Run $name -AcknowledgeSettingsChange (the current versions are then kept in config-replaced\)" -f ($held -join ', ')))
        }

        # The state the settings must now be in: config-before, except the cloud files kept as the game
        # left them (a removed one stays removed).
        $expected = New-RigHashTable
        foreach ($k in $manifest.Keys) { $expected[$k] = $manifest[$k] }
        foreach ($e in @($record.cloudFiles | Where-Object { $_.action -eq 'kept' -and $_.change -ne 'added' })) {
            if ($e.live) { $expected[$e.key] = $e.live.sha256 } else { $expected.Remove($e.key) }
        }

        # 6. Steam's record of the cloud files (remotecache.vdf, read only). With -RestoreCloudFiles, a
        # restored or stale cloud file gets the resync launch, which must leave the cloud files as
        # restored; a local file it touched is restored again. Without it, a stale record is reported:
        # the cleanup wrote no cloud file, so it predates this cleanup.
        $consistency = Test-RigCloudConsistency $locations
        $restoredCloud = @($record.restored | Where-Object { Test-RigCloudKey $cloudNames $_ })
        if ($RestoreCloud -and ($restoredCloud.Count -gt 0 -or $consistency.Stale.Count -gt 0)) {
            $rs = Invoke-RigCloudResync $cfg $RunDir
            $record.resync = $rs.Note
            if (-not $rs.Ok) {
                return (& $fail ("cloud file(s) restored, but the resync step failed: $($rs.Note). Steam's record may be stale: retry with cleanup.ps1 -Run $name -RestoreCloudFiles before the game is started again"))
            }
            $post = Compare-SettingsWithManifest $locations $expected
            $cloudTouched = @((@($post.Changed) + @($post.Removed)) | Where-Object { Test-RigCloudKey $cloudNames $_ })
            if ($cloudTouched.Count -gt 0) {
                return (& $fail ("the resync launch changed cloud file(s) $($cloudTouched -join ', '): the game got as far as its profile before it was killed. Retry with cleanup.ps1 -Run $name -RestoreCloudFiles"))
            }
            $againDir = Join-Path $RunDir ('config-replaced\' + (Get-Date).ToString('yyyyMMdd-HHmmss-fff') + '-resync')
            foreach ($key in (@($post.Changed) + @($post.Removed))) {
                try {
                    [void](Restore-RigRunKey $cfg $locations $before $manifest $key $againDir)
                    Write-RigLog "restored $key again (touched by the resync launch)"
                } catch {
                    return (& $fail "restore after the resync launch failed: $key ($($_.Exception.Message))")
                }
            }
            $consistency = Test-RigCloudConsistency $locations
            if ($consistency.Stale.Count -gt 0) {
                $record.cloudStale = @($consistency.Details)
                return (& $fail ("after the resync launch Steam's record still describes other versions of: $($consistency.Stale -join ', '). Do not start the game directly; retry with cleanup.ps1 -Run $name -RestoreCloudFiles, or start it once through Steam"))
            }
            Write-RigLog 'cloud files restored and Steam''s record matches them'
        }
        $record.cloudStale = @($consistency.Details)
        foreach ($d in $consistency.Details) {
            Write-RigLog ("Steam's record is stale for {0} (record: {1} bytes, SHA-1 {2}; file: {3} bytes, SHA-1 {4}): the next launch may reset the profile. Clean a pending run with -RestoreCloudFiles, or start the game once through Steam" -f $d.key, $d.steamSize, $d.steamSha1, $d.fileSize, $d.fileSha1) 'ERROR'
        }
        foreach ($u in $consistency.Unreadable) { Write-RigLog "Steam's cloud record could not be read: $u" 'WARN' }
        if ($consistency.Untracked.Count -gt 0) { Write-RigLog ("cloud files not in Steam's record (taken as new at the next app session): {0}" -f ($consistency.Untracked -join ', ')) }

        # 7. Verify everything against the expected state before the marker goes.
        $verify = Compare-SettingsWithManifest $locations $expected
        if ($verify.Changed.Count -gt 0 -or $verify.Removed.Count -gt 0 -or $verify.Unreadable.Count -gt 0) {
            return (& $fail ('verification failed after restore: ' + ((@($verify.Changed) + @($verify.Removed) + @($verify.Unreadable)) -join ', ')))
        }
        $record.outcome = 'restored'
        $record.verified = $true
        Write-RigJson $cfg (Join-Path $RunDir 'cleanup.json') $record
        Remove-Item -LiteralPath $marker -Force
        Write-RigLog ("cleanup done: {0} file(s) restored and verified, {1} Steam Cloud file(s) kept as the game left them, {2} added file(s) kept" -f $record.restored.Count, $record.cloudKept.Count, $record.added.Count)
        return [pscustomobject]@{ Ok = $true; Skipped = $false; Reason = 'restored' }
    } catch {
        Write-RigLog "cleanup error, CLEANUP_PENDING kept: $($_.Exception.Message)" 'ERROR'
        return [pscustomobject]@{ Ok = $false; Skipped = $false; Reason = $_.Exception.Message }
    } finally {
        Set-RigLogFiles $previousLogs
    }
}

# Cleans the given runs (default: every pending run), then handles the display: DISPLAY_ADDED is
# removed at the end of a work block (-EndBlock) or when it was written by an earlier session, and
# only while no run has a live process. Returns Failed (a cleanup or the display removal failed) and
# Pending (runs still pending, with the reason).
function Invoke-RigCleanup($cfg, [string[]]$RunDirs, [switch]$Explicit, [switch]$AllowStop, [switch]$EndBlock, [string]$Session, [switch]$Acknowledge, [switch]$RestoreCloud) {
    $result = [pscustomobject]@{ Failed = $false; Pending = @() }
    if (-not $RunDirs) { $RunDirs = @(Get-RigRunDirs $cfg | Where-Object { Test-RigPending $_ }) }
    foreach ($dir in $RunDirs) {
        $r = Invoke-RigRunCleanup $cfg $dir -Explicit:$Explicit -AllowStop:$AllowStop -Acknowledge:$Acknowledge -RestoreCloud:$RestoreCloud
        if ($r.Ok) { continue }
        $result.Pending += [pscustomobject]@{ Dir = $dir; Reason = $r.Reason; Skipped = $r.Skipped }
        if ($r.Skipped) {
            Write-RigLog "skipped: $($r.Reason)" 'WARN'
            if ($Explicit) { $result.Failed = $true }
        } else {
            $result.Failed = $true
            Write-RigLog "run $(Split-Path -Leaf $dir) still pending: $($r.Reason)" 'ERROR'
        }
    }
    $marker = Get-RigDisplayMarker $cfg
    if ($marker) {
        $stale = ($Session -and ((Get-RigProp $marker 'session') -ne $Session))
        if ($EndBlock -or $stale) {
            $busy = @(Get-RigRunDirs $cfg | Where-Object { (Test-RigPending $_) -and @(Get-RigRunLiveProcesses $_).Count -gt 0 })
            if ($busy.Count -gt 0) {
                Write-RigLog 'virtual display kept: a run still has a live process' 'WARN'
            } else {
                $why = 'end of work block'
                if ($stale) { $why = 'left by an earlier session' }
                $d = Remove-RigDisplay $cfg -Reason $why
                if (-not $d.Ok) { $result.Failed = $true; Write-RigLog $d.Note 'ERROR' }
            }
        }
    }
    return $result
}
