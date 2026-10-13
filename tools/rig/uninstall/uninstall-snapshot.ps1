# Uninstall check (T-093, M10), clean machine: a snapshot of every place the EternalVR package could leave something
# behind, as plain text files that diff line by line. Dot-sourced by uninstall-check.ps1.
#   Save-EvrSnapshot -Dir <out> [-Broad]
# Targeted (always): the Vulkan implicit and explicit layer keys and the OpenXR API layer and runtime keys in HKCU and
# HKLM (64- and 32-bit views), the user and machine environment (values hashed), Run/RunOnce, App Paths and Uninstall entries,
# shortcuts (desktop, Start menu, Startup; user and common), scheduled tasks, services, and every file under
# %LOCALAPPDATA%\EternalVR, %APPDATA%\EternalVR and %ProgramData%\EternalVR.
# Broad (-Broad, for a clean machine only: it exports whole hives): reg export of HKCU\Software and HKLM\SOFTWARE, and
# a listing of %LOCALAPPDATA%, %APPDATA%, %ProgramData% and %TEMP% (three levels, names and sizes).

$script:EvrRegKeys = @(
    'SOFTWARE\Khronos', 'SOFTWARE\Khronos\Vulkan', 'SOFTWARE\Khronos\Vulkan\ImplicitLayers', 'SOFTWARE\Khronos\Vulkan\ExplicitLayers',
    'SOFTWARE\Khronos\OpenXR\1\ApiLayers\Implicit', 'SOFTWARE\Khronos\OpenXR\1\ApiLayers\Explicit',
    'SOFTWARE\Khronos\OpenXR\1', 'SOFTWARE\Microsoft\Windows\CurrentVersion\Run',
    'SOFTWARE\Microsoft\Windows\CurrentVersion\RunOnce', 'SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths',
    'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall', 'SOFTWARE\EternalVR', 'SOFTWARE\ArcadaLabs')

# -HashValues: a value's SHA-256 prefix instead of its text (the environment may hold tokens; a change still shows).
function Get-EvrRegLines([string]$Hive, [string]$View, [string]$Key, [switch]$HashValues) {
    $out = New-Object System.Collections.Generic.List[string]
    $hiveEnum = if ($Hive -eq 'HKCU') { [Microsoft.Win32.RegistryHive]::CurrentUser } else { [Microsoft.Win32.RegistryHive]::LocalMachine }
    $viewEnum = if ($View -eq '32') { [Microsoft.Win32.RegistryView]::Registry32 } else { [Microsoft.Win32.RegistryView]::Registry64 }
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hiveEnum, $viewEnum)
    try {
        $k = $base.OpenSubKey($Key, $false)
        if (-not $k) { $out.Add("$Hive($View)\$Key : (absent)"); return $out }
        try {
            foreach ($n in ($k.GetValueNames() | Sort-Object)) {
                $v = "$($k.GetValue($n, $null, 'DoNotExpandEnvironmentNames'))"
                if ($HashValues) {
                    $sha = [Security.Cryptography.SHA256]::Create()
                    $v = 'sha256 ' + (-join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($v))[0..7] | ForEach-Object { $_.ToString('x2') }))
                    $sha.Dispose()
                }
                $out.Add("$Hive($View)\$Key : [$n] = $v")
            }
            # One level of subkeys (names only) so an added key shows, without walking large trees.
            foreach ($s in ($k.GetSubKeyNames() | Sort-Object)) { $out.Add("$Hive($View)\$Key : subkey $s") }
            if ($out.Count -eq 0) { $out.Add("$Hive($View)\$Key : (empty)") }
        } finally { $k.Close() }
    } finally { $base.Close() }
    return $out
}

function Get-EvrFileLines([string]$Root, [int]$Depth = 99) {
    $out = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path -LiteralPath $Root)) { $out.Add("$Root : (absent)"); return $out }
    $rootLen = (Resolve-Path -LiteralPath $Root).Path.TrimEnd('\').Length
    Get-ChildItem -LiteralPath $Root -Recurse -Force -Depth $Depth -ErrorAction SilentlyContinue | Sort-Object FullName | ForEach-Object {
        $rel = $_.FullName.Substring($rootLen)
        if ($_.PSIsContainer) { $out.Add("$Root$rel\") } else { $out.Add("$Root$rel  $($_.Length)") }
    }
    if ($out.Count -eq 0) { $out.Add("$Root : (empty)") }
    return $out
}

function Save-EvrSnapshot([string]$Dir, [switch]$Broad) {
    New-Item -ItemType Directory -Force $Dir | Out-Null
    $reg = New-Object System.Collections.Generic.List[string]
    foreach ($key in $script:EvrRegKeys) {
        foreach ($hv in @(@('HKCU', '64'), @('HKLM', '64'), @('HKLM', '32'))) { $reg.AddRange([string[]]@(Get-EvrRegLines $hv[0] $hv[1] $key)) }
    }
    foreach ($e in @(@('HKCU', 'Environment'), @('HKLM', 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment'))) {
        $reg.AddRange([string[]]@(Get-EvrRegLines $e[0] '64' $e[1] -HashValues))
    }
    Set-Content -LiteralPath (Join-Path $Dir 'registry-targeted.txt') -Value $reg -Encoding utf8

    $files = New-Object System.Collections.Generic.List[string]
    foreach ($r in @("$env:LOCALAPPDATA\EternalVR", "$env:APPDATA\EternalVR", "$env:ProgramData\EternalVR")) {
        $files.AddRange([string[]]@(Get-EvrFileLines $r))
    }
    Set-Content -LiteralPath (Join-Path $Dir 'files-eternalvr.txt') -Value $files -Encoding utf8

    $links = New-Object System.Collections.Generic.List[string]
    $shell = [Environment+SpecialFolder]
    foreach ($f in @($shell::Desktop, $shell::CommonDesktopDirectory, $shell::StartMenu, $shell::CommonStartMenu, $shell::Startup, $shell::CommonStartup)) {
        $p = [Environment]::GetFolderPath($f)
        if ($p) { $links.AddRange([string[]]@(Get-EvrFileLines $p)) }
    }
    Set-Content -LiteralPath (Join-Path $Dir 'shortcuts.txt') -Value $links -Encoding utf8

    $tasks = @(Get-ScheduledTask -ErrorAction SilentlyContinue | ForEach-Object { "$($_.TaskPath)$($_.TaskName)" } | Sort-Object)
    Set-Content -LiteralPath (Join-Path $Dir 'scheduled-tasks.txt') -Value $tasks -Encoding utf8
    $services = @(Get-CimInstance Win32_Service -ErrorAction SilentlyContinue | ForEach-Object { "$($_.Name) | $($_.PathName)" } | Sort-Object)
    Set-Content -LiteralPath (Join-Path $Dir 'services.txt') -Value $services -Encoding utf8

    if ($Broad) {
        & reg.exe export 'HKCU\Software' (Join-Path $Dir 'hkcu-software.reg') /y | Out-Null
        & reg.exe export 'HKLM\SOFTWARE' (Join-Path $Dir 'hklm-software.reg') /y | Out-Null
        $tree = New-Object System.Collections.Generic.List[string]
        foreach ($r in @($env:LOCALAPPDATA, $env:APPDATA, $env:ProgramData, $env:TEMP)) { $tree.AddRange([string[]]@(Get-EvrFileLines $r 2)) }
        Set-Content -LiteralPath (Join-Path $Dir 'files-broad.txt') -Value $tree -Encoding utf8
    }
}

# Lines in $B's file that are not in $A's (added) and the reverse (removed), per snapshot file.
function Compare-EvrSnapshot([string]$A, [string]$B, [string]$OutFile) {
    $report = New-Object System.Collections.Generic.List[string]
    foreach ($f in (Get-ChildItem -LiteralPath $A -File | Sort-Object Name)) {
        $other = Join-Path $B $f.Name
        if (-not (Test-Path -LiteralPath $other)) { $report.Add("== $($f.Name): missing in $B"); continue }
        $enc = if ($f.Extension -eq '.reg') { 'Unicode' } else { 'UTF8' }
        $left = @(Get-Content -LiteralPath $f.FullName -Encoding $enc)
        $right = @(Get-Content -LiteralPath $other -Encoding $enc)
        $d = @(Compare-Object -ReferenceObject $left -DifferenceObject $right)
        $report.Add("== $($f.Name): $(@($d | Where-Object SideIndicator -eq '=>').Count) added, $(@($d | Where-Object SideIndicator -eq '<=').Count) removed")
        foreach ($x in $d) { $report.Add("$(if ($x.SideIndicator -eq '=>') { '+ ' } else { '- ' })$($x.InputObject)") }
    }
    Set-Content -LiteralPath $OutFile -Value $report -Encoding utf8
    return $report
}
