# End-to-end test of the launcher without the game: a stand-in exe (tests/FakeGame) plays the game,
# every settings folder is a fake tree under -Root, and the launcher's data folder is there too.
# Needs a Release build of launcher\EternalVR.sln. Steam must be running (preflight checks it) and an
# OpenXR runtime must be set; nothing outside -Root is written.
#   powershell -NoProfile -ExecutionPolicy Bypass -File launcher\tests\e2e.ps1 [-Root <dir>]
param(
    [string]$Root = $(if ($env:EVR_TEST_TMP) { Join-Path $env:EVR_TEST_TMP 'e2e' } else { Join-Path $env:TEMP 'evr-launcher-e2e' }),
    [string]$Configuration = 'Release',
    # An OpenXR runtime manifest for machines without an active system runtime (written into each tree's launcher.ini).
    [string]$Runtime = ''
)
$ErrorActionPreference = 'Stop'
$launcherDir = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $launcherDir "src\EternalVR.Launcher\bin\$Configuration\EternalVR.Launcher.exe"
$fake = Join-Path $launcherDir "tests\FakeGame\bin\$Configuration\evr-fake-game.exe"
foreach ($f in @($exe, $fake)) { if (-not (Test-Path -LiteralPath $f)) { throw "build first: $f is missing" } }

$script:failures = 0
function Check([bool]$ok, [string]$what) {
    if ($ok) { Write-Host "  ok   $what" } else { Write-Host "  FAIL $what"; $script:failures++ }
}

function New-Tree([string]$dir) {
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    $t = @{
        Dir = $dir; Data = "$dir\data"; Saved = "$dir\saved"; Steam = "$dir\steam"; Game = "$dir\game"; Layer = "$dir\layer"
        Local = "$dir\saved\base\DOOMEternalConfig.local"; Record = "$dir\record.txt"
        Remote = "$dir\steam\userdata\111\782330\remote"
    }
    foreach ($d in @("$($t.Saved)\base", "$($t.Remote)\PROFILE", "$($t.Remote)\GAME-AUTOSAVE0", $t.Game, $t.Layer, $t.Data)) {
        New-Item -ItemType Directory -Force -Path $d | Out-Null
    }
    [IO.File]::WriteAllText($t.Local, "r_mode `"25`"`nr_hdrDisplay `"1`"`nr_windowPosX `"100`"`nm_sensitivity `"5`"`n")
    [IO.File]::WriteAllText("$($t.Saved)\base\DOOMEternalConfig.cfg", "configVersion 9`nr_swapInterval `"1`"`n")
    [IO.File]::WriteAllText("$($t.Remote)\PROFILE\profile.bin", 'profile')
    [IO.File]::WriteAllText("$($t.Remote)\GAME-AUTOSAVE0\game.details", 'save')
    [IO.File]::WriteAllText("$($t.Layer)\VK_LAYER_ETERNALVR.json", '{}')
    [IO.File]::WriteAllText("$($t.Layer)\EternalVR.dll", 'stand-in')
    if ($Runtime) { [IO.File]::WriteAllText("$($t.Data)\launcher.ini", "schema_version = 2`nruntime = $Runtime`n") }
    return $t
}

function Set-FakeGame($t, [int]$SleepMs, [switch]$HandOff) {
    # The session sets r_hdrDisplay 0, adds r_dof and the stereo key r_TAASafeMode, changes r_mode, and the
    # player changes m_sensitivity; on exit the stand-in saves the window ETERNALVR_WINDOW placed
    # (r_windowPosX/Y, r_windowWidth/Height, r_fullscreen), as the game does. Only m_sensitivity may survive.
    $ini = "record=$($t.Record)`nconfig=$($t.Local)`nconfig_text=r_mode `"30`"\nr_hdrDisplay `"0`"\nm_sensitivity `"7`"\nr_dof `"0`"\nr_TAASafeMode `"1`"\n`nsleep_ms=$SleepMs`n"
    if ($HandOff) { $ini += "handoff=1`n" }
    [IO.File]::WriteAllText("$($t.Game)\fake-game.ini", $ini)
}

function Test-Output([string]$Path, [string]$Pattern) {
    [bool](Select-String -LiteralPath $Path -Pattern $Pattern -SimpleMatch -Quiet -ErrorAction SilentlyContinue)
}

function Wait-Marker($t, [string]$State) {
    $deadline = (Get-Date).AddSeconds(30)
    while ((Get-Date) -lt $deadline) {
        if ((Test-Path "$($t.Data)\SESSION_PENDING") -and ((Get-Content "$($t.Data)\SESSION_PENDING") -contains "state = $State")) { return $true }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Start-Launcher($t, [string[]]$Extra, [string]$Out) {
    $common = @('--data-root', $t.Data, '--saved-games', $t.Saved, '--steam-root', $t.Steam, '--game-dir', $t.Game,
                '--layer-dir', $t.Layer, '--test-exe', $fake)
    $argLine = (@($Extra) + $common | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '
    $p = Start-Process -FilePath $exe -ArgumentList $argLine -PassThru -NoNewWindow -RedirectStandardOutput $Out -RedirectStandardError "$Out.err"
    $null = $p.Handle # keeps the handle open so ExitCode is readable after the exit
    return $p
}

function Invoke-Launcher($t, [string[]]$Extra, [string]$Out) {
    $common = @('--data-root', $t.Data, '--saved-games', $t.Saved, '--steam-root', $t.Steam, '--game-dir', $t.Game,
                '--layer-dir', $t.Layer, '--test-exe', $fake)
    $argLine = (@($Extra) + $common | ForEach-Object { if ($_ -match '\s') { "`"$_`"" } else { $_ } }) -join ' '
    $p = Start-Process -FilePath $exe -ArgumentList $argLine -Wait -PassThru -NoNewWindow -RedirectStandardOutput $Out -RedirectStandardError "$Out.err"
    return $p.ExitCode
}

$expected = "r_mode `"25`"`nr_hdrDisplay `"1`"`nm_sensitivity `"7`"`nr_windowPosX `"100`"`n"

Write-Host "1. whole session"
$t = New-Tree (Join-Path $Root 'session')
Set-FakeGame $t 1500
$code = Invoke-Launcher $t @('--launch') "$($t.Dir)\launcher.out"
Check ($code -eq 0) "launcher exit code 0 (got $code)"
$record = Get-Content -LiteralPath $t.Record -ErrorAction SilentlyContinue
Check ($record -contains 'env:ETERNALVR_ENABLE_LAYER=1') 'ETERNALVR_ENABLE_LAYER=1 reached the game'
Check ($record -contains "env:VK_ADD_IMPLICIT_LAYER_PATH=$($t.Layer)") 'VK_ADD_IMPLICIT_LAYER_PATH points at the layer folder'
Check ($record -contains 'env:SteamAppId=782330') 'SteamAppId=782330'
Check ($record -contains "cwd=$($t.Game)") 'working directory is the game folder'
Check ([bool]($record | Where-Object { $_ -like 'cmdline=*+r_hdrDisplay 0*' })) 'forced cvars on the command line'
Check ($record -contains 'env:ETERNALVR_MODE=stereo') 'stereo by default'
Check ($record -contains 'env:ETERNALVR_CONTROLLERS=1') 'controllers on by default'
Check ($record -contains 'env:ETERNALVR_AIM=hand') 'hand aim by default'
Check ([bool]($record | Where-Object { $_ -like 'env:ETERNALVR_WINDOW=*,*,*,*' })) 'the stereo window is placed by the layer'
Check ([bool]($record | Where-Object { $_ -like 'cmdline=*+r_TAASafeMode 0*+r_fullscreen 0*' })) 'stereo cvars on the command line'
Check (-not ($record | Where-Object { $_ -like 'cmdline=*+map*' })) 'no +map: the game starts normally'
Check ([bool]($record | Where-Object { $_ -like 'env:ETERNALVR_LOG_DIR=*\data\logs\*' })) 'log folder under the data folder'
$replaced = @(Get-ChildItem "$($t.Data)\snapshots" -Recurse -Filter 'DOOMEternalConfig.local' | Where-Object { $_.FullName -like '*\replaced\*' })
$sessionText = if ($replaced.Count -eq 1) { [IO.File]::ReadAllText($replaced[0].FullName) } else { '' }
Check ($sessionText -match 'r_windowPosY "-?\d+"' -and $sessionText -match 'r_windowWidth "\d+"') 'the stand-in saved the placed window on exit'
Check ([IO.File]::ReadAllText($t.Local) -eq $expected) 'forced and window keys restored, the player''s change kept'
Check (-not (Test-Path "$($t.Data)\SESSION_PENDING")) 'session marker removed'
Check (@(Get-ChildItem "$($t.Data)\save-backups" -Directory).Count -eq 1) 'one save backup'
# The launcher's own environment reaches everything it starts (Explorer from "Open data folder"), so the
# layer's variables may only ever be set on the game's start info, never on the launcher process.
# (OpenXrProbe is the one exception: the OpenXR loader reads its environment, so the probe sets and restores it around one call.)
$setters = @(Select-String -Path "$launcherDir\src\*\*.cs", "$launcherDir\src\*\*\*.cs" -Pattern 'SetEnvironmentVariable' -SimpleMatch |
    Where-Object { $_.Filename -ne 'OpenXrProbe.cs' })
Check ($setters.Count -eq 0) "the launcher never changes its own environment ($($setters.Count) call(s))"

Write-Host "2. launcher killed mid-session, recovery at the next start"
$t = New-Tree (Join-Path $Root 'crash')
Set-FakeGame $t 6000
$p = Start-Launcher $t @('--launch') "$($t.Dir)\launcher.out"
[void](Wait-Marker $t 'running')
Stop-Process -Id $p.Id -Force
$p.WaitForExit()
Check (Test-Path "$($t.Data)\SESSION_PENDING") 'marker left behind by the killed launcher'
$code = Invoke-Launcher $t @('--dry-run') "$($t.Dir)\recover-early.out"
Check ($code -eq 2) "restore deferred while the game still runs (exit $code)"
Check (Test-Path "$($t.Data)\SESSION_PENDING") 'marker kept while the game runs'
$deadline = (Get-Date).AddSeconds(30)
while ((Get-Process -Name 'evr-fake-game' -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
$code = Invoke-Launcher $t @('--dry-run') "$($t.Dir)\recover.out"
Check ($code -eq 0) "dry run after recovery would launch (exit $code)"
Check ([IO.File]::ReadAllText($t.Local) -eq $expected) 'forced keys restored by the next start'
Check (-not (Test-Path "$($t.Data)\SESSION_PENDING")) 'marker removed after the recovery'

Write-Host "2b. launcher killed mid-session, the session finisher restores when the game exits"
$t = New-Tree (Join-Path $Root 'finisher')
Set-FakeGame $t 6000
$p = Start-Launcher $t @('--launch') "$($t.Dir)\launcher.out"
[void](Wait-Marker $t 'running')
# The window starts the finisher with its own path options; start it the same way the window would.
$f = Start-Launcher $t @('--finish-session') "$($t.Dir)\finisher.out"
Stop-Process -Id $p.Id -Force
$p.WaitForExit()
Check (-not $f.HasExited) 'the finisher waits while the game runs'
Check ($f.WaitForExit(30000)) 'the finisher exited after the game'
Check ($f.ExitCode -eq 0) "finisher exit 0 (got $($f.ExitCode))"
Check ([IO.File]::ReadAllText($t.Local) -eq $expected) 'forced keys restored without opening the launcher'
Check (-not (Test-Path "$($t.Data)\SESSION_PENDING")) 'marker removed by the finisher'
Check (Test-Output "$($t.Data)\logs\launcher.log" 'finisher: done') 'the finisher logged the restore'

Write-Host "2c. the finisher leaves the restore to a launcher that is still open"
$t = New-Tree (Join-Path $Root 'finisher-open')
Set-FakeGame $t 3000
$p = Start-Launcher $t @('--launch') "$($t.Dir)\launcher.out"
[void](Wait-Marker $t 'running')
$f = Start-Launcher $t @('--finish-session') "$($t.Dir)\finisher.out"
$p.WaitForExit()
Check ($f.WaitForExit(30000) -and $f.ExitCode -eq 0) 'the finisher exited quietly'
Check ($p.ExitCode -eq 0) "the launcher completed its own session (exit $($p.ExitCode))"
Check (-not (Test-Output "$($t.Data)\logs\launcher.log" 'finisher: the launcher is closed')) 'the finisher did not restore'
Check ([IO.File]::ReadAllText($t.Local) -eq $expected) 'restored once, by the launcher'

Write-Host "3. refusals"
$t = New-Tree (Join-Path $Root 'refuse')
Set-FakeGame $t 100
[IO.File]::WriteAllText("$($t.Data)\launcher.ini", "schema_version = 1`nextra_args = +map game/pvp/pvp_inferno`n")
$code = Invoke-Launcher $t @('--launch') "$($t.Dir)\launcher.out"
Check ($code -eq 1 -and (Test-Output "$($t.Dir)\launcher.out" 'single-player only')) "BATTLEMODE map refused by the single-player check (exit $code)"
Check (-not (Test-Path $t.Record)) 'the stand-in game was not started'
Remove-Item -LiteralPath "$($t.Layer)\EternalVR.dll"
[IO.File]::WriteAllText("$($t.Data)\launcher.ini", "schema_version = 1`n")
$code = Invoke-Launcher $t @('--launch') "$($t.Dir)\launcher2.out"
Check ($code -eq 1 -and (Test-Output "$($t.Dir)\launcher2.out" 'layer is incomplete')) "incomplete layer folder refused by the layer check (exit $code)"
[IO.File]::WriteAllText("$($t.Data)\launcher.ini", "schema_version = 9`n")
$code = Invoke-Launcher $t @('--dry-run') "$($t.Dir)\launcher3.out"
Check ($code -eq 1 -and (Test-Output "$($t.Dir)\launcher3.out.err" 'newer than this launcher')) "newer settings schema refused (exit $code)"
Check ([IO.File]::ReadAllText("$($t.Data)\launcher.ini") -eq "schema_version = 9`n") 'newer settings file left unchanged'

Write-Host "4. restore saves"
$t = New-Tree (Join-Path $Root 'saves')
Set-FakeGame $t 100
$code = Invoke-Launcher $t @('--launch') "$($t.Dir)\launcher.out"
[IO.File]::WriteAllText("$($t.Remote)\GAME-AUTOSAVE0\game.details", 'overwritten')
$code = Invoke-Launcher $t @('--restore-saves') "$($t.Dir)\restore.out"
Check ($code -eq 0) "restore saves exit 0 (got $code)"
Check ([IO.File]::ReadAllText("$($t.Remote)\GAME-AUTOSAVE0\game.details") -eq 'save') 'save slot restored from the backup'
Check (Test-Output "$($t.Dir)\restore.out" 'could not be checked') 'no Steam record: the restore says it is unverified'

# Steam's record of the fake remote folder, in Steam's format (as tools/rig/tests Format-TestCloudRecord).
function Write-CloudRecord($t) {
    $root = (Get-Item -LiteralPath $t.Remote).FullName.TrimEnd('\')
    $text = "`"782330`"`n{`n`t`"ChangeNumber`"`t`t`"7`"`n"
    foreach ($f in @(Get-ChildItem -LiteralPath $root -Recurse -File | Sort-Object FullName)) {
        $rel = $f.FullName.Substring($root.Length + 1) -replace '\\', '/'
        $sha = (Get-FileHash -LiteralPath $f.FullName -Algorithm SHA1).Hash.ToLowerInvariant()
        $text += "`t`"$rel`"`n`t{`n`t`t`"root`"`t`t`"0`"`n`t`t`"size`"`t`t`"$($f.Length)`"`n`t`t`"sha`"`t`t`"$sha`"`n`t`t`"syncstate`"`t`t`"1`"`n`t}`n"
    }
    [IO.File]::WriteAllText((Join-Path (Split-Path -Parent $t.Remote) 'remotecache.vdf'), $text + "}`n")
}

# The resync launch: the stand-in records how it was started and, with -Accept, plays Steam taking the
# files on disk; then it waits to be killed.
function Set-ResyncGame($t, [switch]$Accept) {
    $ini = "record=$($t.Record)`nsleep_ms=60000`n"
    if ($Accept) { $ini += "cloud_accept=$($t.Remote)`n" }
    [IO.File]::WriteAllText("$($t.Game)\fake-game.ini", $ini)
}

function Wait-NoFakeGame {
    $deadline = (Get-Date).AddSeconds(15)
    while ((Get-Process -Name 'evr-fake-game' -ErrorAction SilentlyContinue) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
    return -not (Get-Process -Name 'evr-fake-game' -ErrorAction SilentlyContinue)
}

Write-Host "4b. restore saves with a stale Steam record: the short launch makes Steam take them"
$t = New-Tree (Join-Path $Root 'resync')
Set-FakeGame $t 100
$code = Invoke-Launcher $t @('--launch') "$($t.Dir)\launcher.out"
[IO.File]::WriteAllText("$($t.Remote)\GAME-AUTOSAVE0\game.details", 'overwritten')
Write-CloudRecord $t
Remove-Item -LiteralPath $t.Record
Set-ResyncGame $t -Accept
$localBefore = [IO.File]::ReadAllText($t.Local)
$code = Invoke-Launcher $t @('--restore-saves') "$($t.Dir)\restore.out"
Check ($code -eq 0) "restore saves exit 0 (got $code)"
Check ([IO.File]::ReadAllText("$($t.Remote)\GAME-AUTOSAVE0\game.details") -eq 'save') 'save slot restored from the backup'
$record = Get-Content -LiteralPath $t.Record -ErrorAction SilentlyContinue
Check ([bool]($record | Where-Object { $_ -like 'cmdline=*+r_fullscreen 0 +s_volume 0*' })) 'short launch windowed and muted'
Check ($record -contains 'env:SteamAppId=782330') 'short launch with SteamAppId=782330'
Check (-not ($record | Where-Object { $_ -like 'env:VK_ADD_IMPLICIT_LAYER_PATH=*' })) 'short launch without the VR layer'
Check (Wait-NoFakeGame) 'the short launch was closed'
Check (Test-Output "$($t.Dir)\restore.out" 'now matches') 'the user is told Steam took the restored saves'
$sha = (Get-FileHash -LiteralPath "$($t.Remote)\GAME-AUTOSAVE0\game.details" -Algorithm SHA1).Hash.ToLowerInvariant()
Check (Test-Output "$(Split-Path -Parent $t.Remote)\remotecache.vdf" $sha) 'Steam''s record holds the restored save'
Check ([IO.File]::ReadAllText($t.Local) -eq $localBefore) 'local config untouched by the short launch'

Write-Host "4c. restore saves when the resync does not take: the user is sent to Steam"
$t = New-Tree (Join-Path $Root 'resync-fail')
Set-FakeGame $t 100
$code = Invoke-Launcher $t @('--launch') "$($t.Dir)\launcher.out"
[IO.File]::WriteAllText("$($t.Remote)\GAME-AUTOSAVE0\game.details", 'overwritten')
Write-CloudRecord $t
Set-ResyncGame $t
$code = Invoke-Launcher $t @('--restore-saves') "$($t.Dir)\restore.out"
Check ($code -eq 4) "restore saves exit 4 while Steam's record is stale (got $code)"
Check (Test-Output "$($t.Dir)\restore.out" 'through Steam and quit at the main menu') 'the user is told to launch through Steam'
Check (Wait-NoFakeGame) 'the short launch was closed'
$code = Invoke-Launcher $t @('--dry-run') "$($t.Dir)\dry.out"
Check (Test-Output "$($t.Dir)\dry.out" '[WARN] cloud-record') 'preflight warns about the stale cloud record'
Check ($code -eq 0) "a stale record only warns (dry run exit $code)"

Write-Host "5. Steam hand-off: the started exe exits and a new game process appears two seconds later"
$t = New-Tree (Join-Path $Root 'handoff')
Set-FakeGame $t 1500 -HandOff
$code = Invoke-Launcher $t @('--launch') "$($t.Dir)\launcher.out"
Check (Test-Output "$($t.Dir)\launcher.out" 'hand-off') 'the hand-off is reported by name'
Check (-not (Test-Output "$($t.Dir)\launcher.out" 'the game exited')) 'not reported as a plain early exit'
Check ([bool]((Get-Content -LiteralPath $t.Record -ErrorAction SilentlyContinue) | Where-Object { $_ -like 'child *' })) 'the second game process ran'
Check ([IO.File]::ReadAllText($t.Local) -eq $expected) 'restored only after the second game process exited'
Check (-not (Test-Path "$($t.Data)\SESSION_PENDING")) 'session marker removed'

Write-Host "6. a second launcher on the same data folder is refused"
$t = New-Tree (Join-Path $Root 'second')
Set-FakeGame $t 5000
$p = Start-Launcher $t @('--launch') "$($t.Dir)\launcher.out"
Check (Wait-Marker $t 'running') 'first launcher running a session'
$code = Invoke-Launcher $t @('--dry-run') "$($t.Dir)\second.out"
Check ($code -eq 3 -and (Test-Output "$($t.Dir)\second.out.err" 'already open')) "second launcher refused (exit $code)"
Check ((Get-Content "$($t.Data)\SESSION_PENDING") -contains 'state = running') 'the first session''s marker is untouched'
$p.WaitForExit()
Check ($p.ExitCode -eq 0) "first launcher completed its session (exit $($p.ExitCode))"
Check ([IO.File]::ReadAllText($t.Local) -eq $expected) 'first session restored'

if ($script:failures -gt 0) { Write-Host "E2E FAILED: $script:failures check(s)"; exit 1 }
Write-Host 'E2E OK'
