# End-to-end test of the in-place update on the published releases: downloads release -From from the public repository,
# unpacks it under -Root, runs its launcher with a data folder of its own there and drives the update dialog through UI
# Automation, as a player would. Needs the network (GitHub) and a desktop; the game is not started and nothing outside
# -Root is written (the launcher's settings say no OpenXR runtime, so it reads no headset). Started by powershell.exe
# through run-on-hidden-desktop.ps1, its windows stay off the shown desktop.
#   powershell -NoProfile -ExecutionPolicy Bypass -File launcher\tests\update-e2e.ps1 -Root <dir> [-From 0.1.13]
# Cases, each with a fresh copy of the release and data folder:
#   later    Later closes the dialog and changes nothing; the button stays.
#   skip     Skip this version is saved; the next start offers nothing.
#   offline  With no network (a dead proxy in the copy's exe.config), the update button (from the last check) says the
#            check failed and changes nothing.
#   install  The download fails (its .part file held open), nothing changes and the failure is shown; Try again installs
#            the newest release, the launcher restarts into it with the same data folder, and every file matches it.
param(
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$From = '0.1.13',
    # A folder to start from instead of release -From's zip (a release-shaped build of this checkout, its version -From,
    # with BUILD-INFO.txt and SHA256SUMS.txt), to test this checkout's dialog against the newest published release.
    [string]$FromFolder = '',
    [string[]]$Cases = @('later', 'skip', 'offline', 'install')
)
$ErrorActionPreference = 'Stop'
# -File passes "a,b" as one string.
$Cases = @($Cases | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
Add-Type -Namespace Evr -Name Win -MemberDefinition @'
[DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, int msg, IntPtr w, IntPtr l);
'@
$A = [System.Windows.Automation.AutomationElement]
$script:failures = 0
function Check([bool]$ok, [string]$what) {
    if ($ok) { Write-Host "  ok   $what" } else { Write-Host "  FAIL $what"; $script:failures++ }
}

function Wait-Until([scriptblock]$test, [int]$seconds, [string]$what) {
    $until = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $until) {
        $r = & $test
        if ($r) { return $r }
        Start-Sleep -Milliseconds 250
    }
    throw "timed out after $seconds s: $what"
}

# The process's top-level windows and the dialogs they own (UI Automation lists an owned dialog under its owner).
function Windows-Of([int]$processId) {
    $cond = New-Object System.Windows.Automation.PropertyCondition($A::ProcessIdProperty, $processId)
    $isWindow = New-Object System.Windows.Automation.PropertyCondition($A::ControlTypeProperty, [System.Windows.Automation.ControlType]::Window)
    foreach ($w in @($A::RootElement.FindAll([System.Windows.Automation.TreeScope]::Children, $cond))) {
        $w
        @($w.FindAll([System.Windows.Automation.TreeScope]::Children, $isWindow))
    }
}

function Find-Window([int]$processId, [string]$title) {
    Windows-Of $processId | Where-Object { $_.Current.Name -like $title } | Select-Object -First 1
}

function Find-Named($parent, [string]$name, $type = $null) {
    $cond = New-Object System.Windows.Automation.PropertyCondition($A::NameProperty, $name)
    $found = @($parent.FindAll([System.Windows.Automation.TreeScope]::Descendants, $cond))
    if ($type) { $found = $found | Where-Object { $_.Current.ControlType -eq $type } }
    $found | Where-Object { -not $_.Current.IsOffscreen } | Select-Object -First 1
}

function Find-Like($parent, [string]$pattern) {
    @($parent.FindAll([System.Windows.Automation.TreeScope]::Descendants, [System.Windows.Automation.Condition]::TrueCondition)) |
        Where-Object { $_.Current.Name -like $pattern } | Select-Object -First 1
}

# A click posted to the button (UI Automation's Invoke waits while the button's handler shows a modal dialog).
function Click($element) {
    [void][Evr.Win]::PostMessage([IntPtr]$element.Current.NativeWindowHandle, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)
}

# WinForms buttons show as panes to UI Automation: found by name alone.
function Button([int]$processId, [string]$window, [string]$name, [int]$seconds = 30) {
    Wait-Until { $w = Find-Window $processId $window; if ($w) { Find-Named $w $name } } $seconds "button '$name' in '$window'"
}

function Get-Release([string]$version) {
    $all = Invoke-RestMethod -Uri 'https://api.github.com/repos/ArcadaLabs-Jason/EternalVR/releases?per_page=20' -Headers @{ 'User-Agent' = 'EternalVR-update-e2e' }
    $r = $all | Where-Object { -not $_.draft -and $_.tag_name -match '^v\d+\.\d+\.\d+$' } | Sort-Object { [version]($_.tag_name.TrimStart('v')) } -Descending
    if ($version) { $r = $r | Where-Object { $_.tag_name -eq "v$version" } }
    $rel = $r | Select-Object -First 1
    $zip = $rel.assets | Where-Object { $_.name -match '^EternalVR-.*\.zip$' -and $_.name -notmatch 'symbols' } | Select-Object -First 1
    [pscustomobject]@{ Version = $rel.tag_name.TrimStart('v'); Zip = $zip; Sha = ($zip.digest -replace '^sha256:', '') }
}

function Test-Sums([string]$dir) {
    $bad = @()
    foreach ($line in Get-Content -LiteralPath (Join-Path $dir 'SHA256SUMS.txt')) {
        if (-not $line.Trim()) { continue }
        $hash = $line.Substring(0, 64); $rel = $line.Substring(64).Trim().TrimStart('*')
        $path = Join-Path $dir $rel
        if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $hash.ToUpperInvariant()) { $bad += $rel }
    }
    $bad
}

function New-Install([string]$case) {
    $dir = Join-Path $Root $case
    if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
    $program = Join-Path $dir 'program'; $data = Join-Path $dir 'data'
    New-Item -ItemType Directory -Force -Path $program, $data | Out-Null
    if ($FromFolder) { Get-ChildItem -LiteralPath $FromFolder -Force | Copy-Item -Destination $program -Recurse }
    else {
        Expand-Archive -LiteralPath $script:oldZip -DestinationPath (Join-Path $dir 'unzip')
        $top = Get-ChildItem -LiteralPath (Join-Path $dir 'unzip') -Directory | Select-Object -First 1
        Get-ChildItem -LiteralPath $top.FullName -Force | Move-Item -Destination $program
        Remove-Item -LiteralPath (Join-Path $dir 'unzip') -Recurse -Force
    }
    # No OpenXR runtime: the launcher reads no headset at start.
    [IO.File]::WriteAllText((Join-Path $data 'launcher.ini'), "schema_version = 2`nruntime = $(Join-Path $dir 'no-runtime.json')`n")
    [IO.File]::WriteAllText((Join-Path $data 'player-file.txt'), 'kept')
    [pscustomobject]@{ Dir = $dir; Program = $program; Data = $data; Exe = Join-Path $program 'EternalVR.Launcher.exe'; Log = Join-Path $data 'logs\launcher.log' }
}

function Start-Launcher($t) {
    $p = Start-Process -FilePath $t.Exe -ArgumentList '--data-root', "`"$($t.Data)`"" -PassThru
    Wait-Until { Find-Window $p.Id 'EternalVR Launcher*' } 60 'the launcher window' | Out-Null
    $p
}

function Stop-Launcher($p) {
    if ($p.HasExited) { return }
    [void]$p.CloseMainWindow()
    if (-not $p.WaitForExit(15000)) { $p.Kill() }
}

function Log-Has($t, [string]$text) { [bool](Select-String -LiteralPath $t.Log -Pattern $text -SimpleMatch -Quiet -ErrorAction SilentlyContinue) }

New-Item -ItemType Directory -Force -Path $Root | Out-Null
# The launcher refuses to install while the game runs (that is tested by the update dialog's own checks, not here).
if (($Cases -contains 'install') -and (Get-Process DOOMEternalx64vk, DOOMEternalx64 -ErrorAction SilentlyContinue)) { throw 'DOOM Eternal is running: the install case needs it closed' }
$new = Get-Release $null
if ($FromFolder) { $old = [pscustomobject]@{ Version = $From } }
else {
    $old = Get-Release $From
    $script:oldZip = Join-Path $Root $old.Zip.name
    if (-not (Test-Path -LiteralPath $script:oldZip)) { Invoke-WebRequest -Uri $old.Zip.browser_download_url -OutFile $script:oldZip -UseBasicParsing }
    Check ((Get-FileHash -LiteralPath $script:oldZip -Algorithm SHA256).Hash -eq $old.Sha.ToUpperInvariant()) "the $($old.Version) zip is the published one"
}
Write-Host "from $($old.Version)$(if ($FromFolder) { " ($FromFolder)" }) to $($new.Version)"
$offer = "Update to $($new.Version)..."

foreach ($case in $Cases) {
    Write-Host "== $case"
    $t = New-Install $case
    if ($case -eq 'offline') {
        $config = "$($t.Exe).config"
        $xml = [xml](Get-Content -LiteralPath $config -Raw)
        $net = $xml.CreateElement('system.net')
        $net.InnerXml = '<defaultProxy enabled="true"><proxy proxyaddress="http://127.0.0.1:9" bypassonlocal="False" usesystemdefault="False" /></defaultProxy>'
        [void]$xml.configuration.AppendChild($net)
        $xml.Save($config)
        New-Item -ItemType Directory -Force -Path (Join-Path $t.Data 'updates') | Out-Null
        [IO.File]::WriteAllText((Join-Path $t.Data 'updates\state.txt'), "check = on`nfound = $($new.Version)`nlast_check = $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))`n")
    }
    $p = Start-Launcher $t
    try {
        switch ($case) {
            'later' {
                Click (Button $p.Id 'EternalVR Launcher*' $offer 60)
                Click (Button $p.Id 'EternalVR update' 'Later')
                Wait-Until { -not (Find-Window $p.Id 'EternalVR update') } 10 'the dialog to close' | Out-Null
                Check ($null -ne (Find-Named (Find-Window $p.Id 'EternalVR Launcher*') $offer)) 'Later keeps the update button'
                Check (@(Test-Sums $t.Program).Count -eq 0) 'Later changes no file'
                Check (-not (Select-String -LiteralPath (Join-Path $t.Data 'updates\state.txt') -Pattern 'skip' -Quiet)) 'Later skips nothing'
            }
            'skip' {
                Click (Button $p.Id 'EternalVR Launcher*' $offer 60)
                Click (Button $p.Id 'EternalVR update' 'Skip this version')
                Wait-Until { -not (Find-Window $p.Id 'EternalVR update') } 10 'the dialog to close' | Out-Null
                Check (Select-String -LiteralPath (Join-Path $t.Data 'updates\state.txt') -Pattern "skip = $($new.Version)" -SimpleMatch -Quiet) 'the skip is saved'
                Check (Log-Has $t "update: skipping EternalVR $($new.Version)") 'the skip is logged'
                Stop-Launcher $p
                # Asked again at the next start: the skipped release is not offered.
                $state = Join-Path $t.Data 'updates\state.txt'
                (Get-Content -LiteralPath $state) | Where-Object { $_ -notmatch '^last_check' } | Set-Content -LiteralPath $state
                $p = Start-Launcher $t
                Start-Sleep -Seconds 10
                Check ($null -eq (Find-Named (Find-Window $p.Id 'EternalVR Launcher*') $offer)) 'the next start offers no update'
            }
            'offline' {
                # The button comes from the last check's answer (an hour is not up); its click asks GitHub again.
                Click (Button $p.Id 'EternalVR Launcher*' $offer 60)
                $box = Wait-Until { Windows-Of $p.Id | Where-Object { $_.Current.Name -eq 'EternalVR update' -and (Find-Like $_ 'The update check failed*') } | Select-Object -First 1 } 60 'the failed check'
                Check $true "the failed check is said: $((Find-Like $box 'The update check failed*').Current.Name)"
                Click (Find-Named $box 'OK')
                Check (@(Test-Sums $t.Program | Where-Object { $_ -ne 'EternalVR.Launcher.exe.config' }).Count -eq 0) 'nothing changed (but the proxy in the exe.config)'
                Check (Log-Has $t 'update: the check failed: ') 'the failed check is logged'
            }
            'install' {
                $part = Join-Path $t.Data "updates\$($new.Version)\$($new.Zip.name).part"
                New-Item -ItemType Directory -Force -Path (Split-Path $part) | Out-Null
                $hold = [IO.File]::Open($part, 'Create', 'ReadWrite', 'None')
                try {
                    Click (Button $p.Id 'EternalVR Launcher*' $offer 60)
                    $install = Button $p.Id 'EternalVR update' 'Download and install'
                    if (-not $install.Current.IsEnabled) {
                        $why = Find-Like (Find-Window $p.Id 'EternalVR update') '*DOOM Eternal*'
                        throw "Download and install is off: $(if ($why) { $why.Current.Name } else { 'no reason found' })"
                    }
                    Click $install
                    $failed = Wait-Until { $w = Find-Window $p.Id 'EternalVR update'; if ($w) { Find-Like $w 'The update failed*' } } 60 'the failure'
                    Check $true "the failure is shown: $($failed.Current.Name)"
                    Check (@(Test-Sums $t.Program).Count -eq 0) 'the failed download changes no file'
                    Check ((Get-Content -LiteralPath (Join-Path $t.Program 'BUILD-INFO.txt') -TotalCount 1) -match [regex]::Escape($old.Version)) "BUILD-INFO.txt still says $($old.Version)"
                }
                finally { $hold.Dispose() }
                Click (Button $p.Id 'EternalVR update' 'Try again')
                $box = Wait-Until { Windows-Of $p.Id | Where-Object { $_.Current.Name -eq 'EternalVR update' -and (Find-Like $_ '*is installed*') } | Select-Object -First 1 } 180 'the installed message'
                Check $true 'the install is said'
                $oldId = $p.Id
                Click (Find-Named $box 'OK')
                Check ($p.WaitForExit(30000)) 'the old launcher closes'
                $p = Wait-Until { Get-Process EternalVR.Launcher -ErrorAction SilentlyContinue | Where-Object { $_.Id -ne $oldId -and $_.Path -eq $t.Exe } | Select-Object -First 1 } 30 'the new launcher'
                $w = Wait-Until { Find-Window $p.Id "EternalVR Launcher $($new.Version)" } 60 "the $($new.Version) window"
                Check $true "the launcher restarted into $($new.Version): $($w.Current.Name)"
                Start-Sleep -Seconds 5
                $bad = @(Test-Sums $t.Program)
                Check ($bad.Count -eq 0) "every file is the new release's ($($bad -join ', '))"
                Check ((Get-Content -LiteralPath (Join-Path $t.Program 'BUILD-INFO.txt') -TotalCount 1) -match [regex]::Escape($new.Version)) "BUILD-INFO.txt says $($new.Version)"
                Check (@(Get-ChildItem -LiteralPath $t.Program -Recurse -Filter '*.evr-old').Count -eq 0) 'the replaced files are removed by the new launcher'
                Check (-not (Test-Path -LiteralPath (Join-Path $t.Data "updates\$($new.Version)"))) 'the download and staging folder is removed'
                Check ((Test-Path -LiteralPath (Join-Path $t.Data 'player-file.txt')) -and (Select-String -LiteralPath (Join-Path $t.Data 'launcher.ini') -Pattern 'no-runtime.json' -SimpleMatch -Quiet)) 'the data folder is kept'
                Check (Log-Has $t '(SHA-256 checked)') 'the zip was checked against its SHA-256'
                Check (Log-Has $t "update: installed EternalVR $($new.Version) over") 'the install is logged'
                Check (Log-Has $t 'update failed: ') 'the failed download is logged'
            }
            default { throw "no such case: $case" }
        }
    }
    catch {
        Write-Host "  FAIL $case stopped: $($_.Exception.Message)"
        $script:failures++
    }
    finally {
        Stop-Launcher $p
        Get-Process EternalVR.Launcher -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $t.Exe } | ForEach-Object { Stop-Launcher $_ }
        Write-Host "  log: $($t.Log)"
    }
}
if ($script:failures -gt 0) { Write-Host "$($script:failures) check(s) failed"; exit 1 }
Write-Host 'all checks passed'
