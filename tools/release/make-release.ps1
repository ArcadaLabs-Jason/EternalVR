<#
.SYNOPSIS
Builds the layer and the launcher and packs them, with the tester docs, into a release zip.

.DESCRIPTION
Refuses a checkout with uncommitted changes. Builds the layer (CMake preset windows-msvc-release) and the
launcher (Release), unless -NoBuild, then checks what it is about to ship:
  - the launcher's version (launcher/Directory.Build.props) equals the project version in CMakeLists.txt,
    and the launcher exe was built from this commit (its product version ends in the full commit hash);
  - the layer DLL is not older than any tracked source file and does not import a debug C runtime, and
    its version resource carries the same version (no -dev marker), which the launcher checks at launch;
  - the layer manifest names VK_LAYER_ETERNALVR and points at .\EternalVR.dll;
  - the launcher's data files are byte-identical to launcher/data, and its data\controllers\*.toml to
    data/input/controllers (the built-in controller maps the launcher's Edit controls copies out);
  - every shipped binary is ours or is named in THIRD_PARTY_NOTICES.md, and no other binary slipped in.
Then it assembles EternalVR-<channel>-<version>-<shortsha>\ in the output folder:

  EternalVR.Launcher.exe, EternalVR.Launcher.exe.config, EternalVR.Launcher.Core.dll, data\*.txt,
  data\controllers\*.toml
  layer\EternalVR.dll, layer\VK_LAYER_ETERNALVR.json, layer\openxr_loader.dll
  README-ALPHA.md, docs\INSTALL.md, docs\CONTROLS.md, docs\KNOWN-ISSUES.md, docs\TROUBLESHOOTING.md
  Launch-Parallel-Eye-Test.cmd (starts the launcher with the Parallel Eye Rendering box shown)
  LICENSE, THIRD_PARTY_NOTICES.md, BUILD-INFO.txt, SHA256SUMS.txt (every other file in the zip)

and zips it (entries dated at the commit time), with the PDBs in a separate -symbols zip. The zip is
re-read and every entry checked against SHA256SUMS.txt; each zip's SHA-256 is printed and written next to
it as <zip>.sha256.

The launcher refuses a layer whose release version differs from its own (VersionCheck.cs); both are built
from one commit, and BUILD-INFO.txt records the version, the commit and the supported game builds.

Exit codes: 0 packaged (or validated), 1 error, 2 refused (uncommitted changes, a version mismatch, a
stale or unexpected binary).

.PARAMETER OutDir
Where the folder and the zips go. Default: EVR_RELEASE_OUT, else tmp-release in the workspace
(tools\rig\workspace.ps1: EVR_WORKSPACE, else the folder that contains the checkout). Never on C:.

.PARAMETER NoBuild
Package the existing build outputs instead of building. They are still checked for staleness.

.PARAMETER Validate
Check the sources only (versions, docs, notices, data files, manifest template) and write nothing. Needs no
build, game or GPU; CI runs it.

.PARAMETER AllowDirty
Package a checkout with uncommitted changes (for trying the script). The name gets a -dirty suffix and such
a zip must not be shared.

.PARAMETER LayerPreset
The CMake preset whose layer is packed. Default windows-msvc-release.

.PARAMETER Channel
The name's channel part. Default alpha.
#>
param(
    [string]$OutDir,
    [switch]$NoBuild,
    [switch]$Validate,
    [switch]$AllowDirty,
    [string]$LayerPreset = 'windows-msvc-release',
    [string]$Channel = 'alpha'
)

$ErrorActionPreference = 'Stop'
$script:Utf8 = New-Object System.Text.UTF8Encoding($false)
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'rig\workspace.ps1')

class RefusedException : System.Exception {
    RefusedException([string]$message) : base($message) { }
}

function Write-RelLog([string]$Message, [string]$Level = 'INFO') {
    Write-Host ('{0} [{1}] {2}' -f (Get-Date).ToString('HH:mm:ss'), $Level, $Message)
}

function Stop-Refused([string]$Message) { throw [RefusedException]::new($Message) }

function Invoke-Git([string]$Repo, [string[]]$Arguments) {
    $out = & git -C $Repo @Arguments
    if ($LASTEXITCODE -ne 0) { throw "git $($Arguments -join ' ') failed ($LASTEXITCODE)" }
    return $out
}

function Get-RelSha256([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }

function Get-RelStreamSha256([IO.Stream]$Stream) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Stream)) -replace '-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

# ---------------------------------------------------------------------------------------------------
# Sources

function Get-RelState([string]$Repo) {
    $sha = [string](Invoke-Git $Repo @('rev-parse', 'HEAD'))
    $status = @(Invoke-Git $Repo @('status', '--porcelain'))
    $time = [string](Invoke-Git $Repo @('show', '-s', '--format=%cI', 'HEAD'))

    $cmake = [IO.File]::ReadAllText((Join-Path $Repo 'CMakeLists.txt'))
    if ($cmake -notmatch 'project\(EternalVR\s+VERSION\s+(\d+\.\d+\.\d+)') { throw 'No project VERSION in CMakeLists.txt' }
    $layerVersion = $Matches[1]
    $props = [IO.File]::ReadAllText((Join-Path $Repo 'launcher\Directory.Build.props'))
    if ($props -notmatch '<Version>([^<]+)</Version>') { throw 'No <Version> in launcher\Directory.Build.props' }
    $launcherVersion = $Matches[1].Trim()
    $openxr = [IO.File]::ReadAllText((Join-Path $Repo 'cmake\EvrOpenXr.cmake'))
    $openxrVersion = if ($openxr -match 'EVR_OPENXR_VERSION "([^"]+)"') { $Matches[1] } else { 'unknown' }

    return [pscustomobject]@{
        Repo            = $Repo
        Sha             = $sha.Trim()
        ShortSha        = $sha.Trim().Substring(0, 7)
        Dirty           = ($status.Count -gt 0 -and -not [string]::IsNullOrWhiteSpace(($status -join '')))
        DirtyFiles      = $status
        CommitTime      = [DateTimeOffset]::Parse($time.Trim())
        Version         = $layerVersion
        LauncherVersion = $launcherVersion
        OpenXrVersion   = $openxrVersion
    }
}

function Test-RelSources($state) {
    $repo = $state.Repo
    if ($state.Version -ne $state.LauncherVersion) {
        Stop-Refused "Version mismatch: CMakeLists.txt says $($state.Version), launcher\Directory.Build.props says $($state.LauncherVersion). Set both to the release version."
    }
    foreach ($doc in (Get-RelDocs $repo)) {
        if (-not (Test-Path -LiteralPath $doc.Source)) { Stop-Refused "Missing: $($doc.Source)" }
    }
    $builds = @(Get-Content -LiteralPath (Join-Path $repo 'launcher\data\known-builds.txt') | Where-Object { $_ -match '^([0-9a-f]{64}|gamepass)\s*\|' })
    if ($builds.Count -eq 0) { Stop-Refused 'launcher\data\known-builds.txt lists no supported game build' }
    $maps = @(Get-ChildItem -LiteralPath (Join-Path $repo 'data\input\controllers') -File -Filter '*.toml')
    if ($maps.Count -eq 0) { Stop-Refused 'data\input\controllers holds no controller map for the launcher to ship' }
    $manifest = [IO.File]::ReadAllText((Join-Path $repo 'src\vkcore\VK_LAYER_ETERNALVR.json.in'))
    if ($manifest -notmatch '"name":\s*"VK_LAYER_ETERNALVR"') { Stop-Refused 'The layer manifest template does not name VK_LAYER_ETERNALVR' }
    # The tester script shows the experimental box and starts the launcher next to it, nothing else.
    $peCmd = [IO.File]::ReadAllText((Join-Path $repo 'tools\release\Launch-Parallel-Eye-Test.cmd'))
    if ($peCmd -notmatch '(?m)^set ETERNALVR_SHOW_PARALLEL_EYES=1\r$' -or $peCmd -notmatch '(?m)^start "" "%~dp0EternalVR\.Launcher\.exe"\r$') {
        Stop-Refused 'tools\release\Launch-Parallel-Eye-Test.cmd must set ETERNALVR_SHOW_PARALLEL_EYES=1 and start the launcher next to it, with CRLF lines'
    }
    $notices = [IO.File]::ReadAllText((Join-Path $repo 'THIRD_PARTY_NOTICES.md'))
    foreach ($name in $script:ThirdPartyBinaries) {
        if ($notices.IndexOf($name, [StringComparison]::OrdinalIgnoreCase) -lt 0) { Stop-Refused "THIRD_PARTY_NOTICES.md does not name $name" }
    }
    Write-RelLog "sources: version $($state.Version) (layer and launcher agree), $($builds.Count) supported game build(s), $($maps.Count) controller map(s), docs, notices and the tester script present"
    return $builds
}

# Binaries we ship that are not ours; each must be named in THIRD_PARTY_NOTICES.md.
$script:ThirdPartyBinaries = @('openxr_loader.dll')

function Get-RelDocs([string]$Repo) {
    $d = Join-Path $Repo 'docs\release'
    return @(
        [pscustomobject]@{ Source = (Join-Path $d 'README-ALPHA.md'); Target = 'README-ALPHA.md' }
        [pscustomobject]@{ Source = (Join-Path $d 'INSTALL.md'); Target = 'docs\INSTALL.md' }
        [pscustomobject]@{ Source = (Join-Path $d 'CONTROLS.md'); Target = 'docs\CONTROLS.md' }
        [pscustomobject]@{ Source = (Join-Path $d 'KNOWN-ISSUES.md'); Target = 'docs\KNOWN-ISSUES.md' }
        [pscustomobject]@{ Source = (Join-Path $d 'TROUBLESHOOTING.md'); Target = 'docs\TROUBLESHOOTING.md' }
        [pscustomobject]@{ Source = (Join-Path $Repo 'tools\release\Launch-Parallel-Eye-Test.cmd'); Target = 'Launch-Parallel-Eye-Test.cmd' }
        [pscustomobject]@{ Source = (Join-Path $Repo 'LICENSE'); Target = 'LICENSE' }
        [pscustomobject]@{ Source = (Join-Path $Repo 'THIRD_PARTY_NOTICES.md'); Target = 'THIRD_PARTY_NOTICES.md' }
    )
}

# ---------------------------------------------------------------------------------------------------
# Build

function Invoke-RelNative([string]$Exe, [string[]]$Arguments) {
    Write-RelLog "running: $Exe $($Arguments -join ' ')"
    & $Exe @Arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "failed ($LASTEXITCODE): $Exe $($Arguments -join ' ')" }
}

function Invoke-RelBuild($state) {
    $repo = $state.Repo
    # The rig's wrappers set up the compiler and keep every cache on the development drive.
    $tools = if ($env:EVR_RIG_TOOLS) { $env:EVR_RIG_TOOLS } else { Join-Path (Get-EvrWorkspace) 'tools' }
    $layerTool = Join-Path $tools 'evr-build-at.cmd'
    $launcherTool = Join-Path $tools 'evr-launcher.cmd'

    Push-Location -LiteralPath $repo
    try {
        if (Get-Command cl.exe -ErrorAction SilentlyContinue) {
            Invoke-RelNative 'cmake' @('--preset', $LayerPreset)
            Invoke-RelNative 'cmake' @('--build', '--preset', $LayerPreset)
            Invoke-RelNative 'ctest' @('--preset', $LayerPreset)
        } elseif (Test-Path -LiteralPath $layerTool) {
            Invoke-RelNative $layerTool @($repo, $LayerPreset)
        } else {
            throw 'No MSVC environment (cl.exe not on PATH) and no evr-build-at.cmd; build the layer yourself and pass -NoBuild'
        }

        if (Test-Path -LiteralPath $launcherTool) {
            Invoke-RelNative $launcherTool @($repo)
        } elseif (Get-Command dotnet -ErrorAction SilentlyContinue) {
            Invoke-RelNative 'dotnet' @('build', 'launcher\EternalVR.sln', '-c', 'Release')
            Invoke-RelNative 'dotnet' @('test', 'launcher\tests\EternalVR.Launcher.Core.Tests', '-c', 'Release', '--no-build')
        } else {
            throw 'No .NET SDK found; build the launcher yourself and pass -NoBuild'
        }
    } finally { Pop-Location }
}

# ---------------------------------------------------------------------------------------------------
# What ships

function Get-RelPayload($state) {
    $repo = $state.Repo
    $layerDir = Join-Path $repo "build\$LayerPreset\src\vkcore"
    $launcherDir = Join-Path $repo 'launcher\src\EternalVR.Launcher\bin\Release'
    $files = New-Object System.Collections.ArrayList
    $symbols = New-Object System.Collections.ArrayList
    $add = { param($list, $src, $dst) [void]$list.Add([pscustomobject]@{ Source = $src; Target = $dst }) }

    foreach ($name in 'EternalVR.Launcher.exe', 'EternalVR.Launcher.exe.config', 'EternalVR.Launcher.Core.dll') {
        $p = Join-Path $launcherDir $name
        if (-not (Test-Path -LiteralPath $p)) { Stop-Refused "Launcher output missing: $p (build it, or drop -NoBuild)" }
        & $add $files $p $name
    }
    foreach ($f in @(Get-ChildItem -LiteralPath $launcherDir -File)) {
        if ($f.Extension -eq '.pdb') { & $add $symbols $f.FullName ('launcher\' + $f.Name); continue }
        if (-not ($files | Where-Object { $_.Target -eq $f.Name })) {
            Stop-Refused "Unexpected file in the launcher output: $($f.Name). Ship it on purpose (and name it in THIRD_PARTY_NOTICES.md if it is not ours) or remove it."
        }
    }
    $dataSrc = Join-Path $repo 'launcher\data'
    $dataOut = Join-Path $launcherDir 'data'
    $want = @(Get-ChildItem -LiteralPath $dataSrc -File -Filter '*.txt' | Sort-Object Name)
    $have = @(Get-ChildItem -LiteralPath $dataOut -File -ErrorAction SilentlyContinue | Sort-Object Name)
    if (($want.Name -join '|') -ne ($have.Name -join '|')) { Stop-Refused "The launcher's data folder ($dataOut) does not hold exactly launcher\data\*.txt" }
    foreach ($f in $want) {
        $built = Join-Path $dataOut $f.Name
        if ((Get-RelSha256 $built) -ne (Get-RelSha256 $f.FullName)) { Stop-Refused "Stale launcher data file: $built differs from $($f.FullName)" }
        & $add $files $built ('data\' + $f.Name)
    }
    # The built-in controller maps, the defaults of the player's controls folder (Edit controls).
    $mapsSrc = Join-Path $repo 'data\input\controllers'
    $mapsOut = Join-Path $dataOut 'controllers'
    $subdirs = @(Get-ChildItem -LiteralPath $dataOut -Directory -ErrorAction SilentlyContinue | Sort-Object Name)
    if (($subdirs.Name -join '|') -ne 'controllers') { Stop-Refused "The launcher's data folder ($dataOut) must hold exactly one subfolder, controllers" }
    $want = @(Get-ChildItem -LiteralPath $mapsSrc -File -Filter '*.toml' | Sort-Object Name)
    $have = @(Get-ChildItem -LiteralPath $mapsOut -File -ErrorAction SilentlyContinue | Sort-Object Name)
    if (($want.Name -join '|') -ne ($have.Name -join '|')) { Stop-Refused "The launcher's controller maps ($mapsOut) are not exactly data\input\controllers\*.toml" }
    if (@(Get-ChildItem -LiteralPath $mapsOut -Directory).Count -gt 0) { Stop-Refused "Unexpected folder in $mapsOut" }
    foreach ($f in $want) {
        $built = Join-Path $mapsOut $f.Name
        if ((Get-RelSha256 $built) -ne (Get-RelSha256 $f.FullName)) { Stop-Refused "Stale controller map: $built differs from $($f.FullName)" }
        & $add $files $built ('data\controllers\' + $f.Name)
    }

    foreach ($name in 'EternalVR.dll', 'VK_LAYER_ETERNALVR.json', 'openxr_loader.dll') {
        $p = Join-Path $layerDir $name
        if (-not (Test-Path -LiteralPath $p)) { Stop-Refused "Layer output missing: $p (build preset $LayerPreset, or drop -NoBuild)" }
        & $add $files $p ('layer\' + $name)
    }
    $layerPdb = Join-Path $layerDir 'EternalVR.pdb'
    if (Test-Path -LiteralPath $layerPdb) { & $add $symbols $layerPdb 'layer\EternalVR.pdb' }
    else { Write-RelLog "no EternalVR.pdb in $layerDir; the symbols zip holds the launcher's only" 'WARN' }

    foreach ($doc in (Get-RelDocs $repo)) { & $add $files $doc.Source $doc.Target }
    return [pscustomobject]@{ Files = $files; Symbols = $symbols; LayerDir = $layerDir; LauncherDir = $launcherDir }
}

function Test-RelBinaries($state, $payload) {
    $launcherExe = Join-Path $payload.LauncherDir 'EternalVR.Launcher.exe'
    $product = [Diagnostics.FileVersionInfo]::GetVersionInfo($launcherExe).ProductVersion
    $expected = "$($state.Version)+$($state.Sha)"
    if ($product -ne $expected) {
        $msg = "The launcher exe says $product, not ${expected}: it was not built from this commit"
        if ($state.Dirty -and $product -like "$($state.Version)+*") { Write-RelLog "$msg (dirty checkout, allowed)" 'WARN' } else { Stop-Refused $msg }
    }

    $dll = Get-Item -LiteralPath (Join-Path $payload.LayerDir 'EternalVR.dll')
    $newest = [datetime]::MinValue
    $newestName = $null
    foreach ($rel in @(Invoke-Git $state.Repo @('ls-files', '--', 'src', 'cmake', 'CMakeLists.txt', 'CMakePresets.json', 'data'))) {
        $p = Join-Path $state.Repo $rel
        if (-not (Test-Path -LiteralPath $p)) { continue }
        $t = (Get-Item -LiteralPath $p).LastWriteTimeUtc
        if ($t -gt $newest) { $newest = $t; $newestName = $rel }
    }
    if ($dll.LastWriteTimeUtc -lt $newest) { Stop-Refused "The layer DLL ($($dll.LastWriteTime)) is older than $newestName; rebuild preset $LayerPreset" }

    $ascii = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($dll.FullName))
    foreach ($debugCrt in 'VCRUNTIME140D.dll', 'MSVCP140D.dll', 'ucrtbased.dll') {
        if ($ascii.IndexOf($debugCrt, [StringComparison]::OrdinalIgnoreCase) -ge 0) { Stop-Refused "The layer DLL imports ${debugCrt}: a Debug build. Use a release preset." }
    }

    $layerVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($dll.FullName).ProductVersion
    if ($layerVersion -ne $state.Version) {
        Stop-Refused "The layer DLL's version resource says '$layerVersion', not $($state.Version); the launcher would refuse or only warn. Rebuild preset $LayerPreset (a release build type)."
    }

    $manifest = [IO.File]::ReadAllText((Join-Path $payload.LayerDir 'VK_LAYER_ETERNALVR.json')) | ConvertFrom-Json
    if ($manifest.layer.name -ne 'VK_LAYER_ETERNALVR' -or $manifest.layer.library_path -ne '.\EternalVR.dll') {
        Stop-Refused "The layer manifest must name VK_LAYER_ETERNALVR with library_path .\EternalVR.dll (found $($manifest.layer.name), $($manifest.layer.library_path))"
    }

    foreach ($f in $payload.Files) {
        $leaf = Split-Path -Leaf $f.Target
        if ($leaf -notmatch '\.(dll|exe)$' -or $leaf -like 'EternalVR*') { continue }
        if ($script:ThirdPartyBinaries -notcontains $leaf) { Stop-Refused "$leaf is not ours and not in the known third-party list; add it to THIRD_PARTY_NOTICES.md and to this script" }
    }
    Write-RelLog "binaries: launcher $product; layer $layerVersion, $($dll.LastWriteTime.ToString('yyyy-MM-dd HH:mm')) from preset $LayerPreset, release C runtime"
}

# ---------------------------------------------------------------------------------------------------
# Package

function Write-RelBuildInfo($state, [string]$Path, $builds) {
    $lines = @(
        "EternalVR $Channel $($state.Version)"
        "commit: $($state.Sha)$(if ($state.Dirty) { ' (with uncommitted changes: not for sharing)' })"
        "commit time: $($state.CommitTime.ToString('yyyy-MM-dd HH:mm:ss zzz'))"
        "packaged: $([DateTimeOffset]::Now.ToString('yyyy-MM-dd HH:mm:ss zzz'))"
        "layer: EternalVR.dll, CMake preset $LayerPreset; OpenXR loader $($state.OpenXrVersion)"
        "launcher: $($state.LauncherVersion) (.NET Framework 4.8)"
        "launcher and layer: built from the same commit; the launcher refuses a layer of another version"
        ''
        'Supported DOOM Eternal builds (sha256 of DOOMEternalx64vk.exe | Steam build id | description):'
    ) + @($builds | ForEach-Object { '  ' + $_.Trim() })
    [IO.File]::WriteAllText($Path, (($lines -join "`r`n") + "`r`n"), $script:Utf8)
}

function Write-RelSums([string]$Root) {
    $rootFull = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $lines = foreach ($f in @(Get-ChildItem -LiteralPath $Root -File -Recurse | Sort-Object { $_.FullName.Substring($rootFull.Length).ToLowerInvariant() })) {
        $rel = $f.FullName.Substring($rootFull.Length).Replace('\', '/')
        if ($rel -eq 'SHA256SUMS.txt') { continue }
        '{0}  {1}' -f (Get-RelSha256 $f.FullName), $rel
    }
    [IO.File]::WriteAllText((Join-Path $Root 'SHA256SUMS.txt'), (($lines -join "`r`n") + "`r`n"), $script:Utf8)
    return @($lines).Count
}

function New-RelZip([string]$SourceDir, [string]$ZipPath, [string]$Prefix, [DateTimeOffset]$Time) {
    if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
    $srcFull = [IO.Path]::GetFullPath($SourceDir).TrimEnd('\') + '\'
    $zip = [IO.Compression.ZipFile]::Open($ZipPath, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($f in @(Get-ChildItem -LiteralPath $SourceDir -File -Recurse | Sort-Object FullName)) {
            $name = $Prefix + '/' + $f.FullName.Substring($srcFull.Length).Replace('\', '/')
            $entry = $zip.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $Time
            $out = $entry.Open()
            $in = [IO.File]::OpenRead($f.FullName)
            try { $in.CopyTo($out) } finally { $in.Dispose(); $out.Dispose() }
        }
    } finally { $zip.Dispose() }
}

function Test-RelZip([string]$ZipPath, [string]$Prefix, [switch]$Symbols) {
    $zip = [IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $entries = @{}
        foreach ($e in $zip.Entries) {
            if (-not $Symbols -and $e.FullName -like '*.pdb') { Stop-Refused "A .pdb is in the zip: $($e.FullName)" }
            if (-not $e.FullName.StartsWith($Prefix + '/')) { Stop-Refused "Zip entry outside the top folder: $($e.FullName)" }
            $entries[$e.FullName.Substring($Prefix.Length + 1)] = $e
        }
        $sumsEntry = $entries['SHA256SUMS.txt']
        if (-not $sumsEntry) { Stop-Refused 'SHA256SUMS.txt is missing from the zip' }
        $reader = New-Object IO.StreamReader($sumsEntry.Open())
        try { $sums = $reader.ReadToEnd() } finally { $reader.Dispose() }
        $listed = @{}
        foreach ($line in ($sums -split "`r?`n" | Where-Object { $_ })) {
            if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { Stop-Refused "Bad SHA256SUMS line: $line" }
            $listed[$Matches[2]] = $Matches[1]
        }
        foreach ($name in $entries.Keys) {
            if ($name -eq 'SHA256SUMS.txt') { continue }
            if (-not $listed.ContainsKey($name)) { Stop-Refused "$name is in the zip but not in SHA256SUMS.txt" }
            $s = $entries[$name].Open()
            try { $h = Get-RelStreamSha256 $s } finally { $s.Dispose() }
            if ($h -ne $listed[$name]) { Stop-Refused "$name does not match its SHA256SUMS.txt line" }
        }
        foreach ($name in $listed.Keys) { if (-not $entries.ContainsKey($name)) { Stop-Refused "$name is in SHA256SUMS.txt but not in the zip" } }
        return $entries.Count
    } finally { $zip.Dispose() }
}

function Write-RelZipHash([string]$ZipPath) {
    $hash = Get-RelSha256 $ZipPath
    [IO.File]::WriteAllText($ZipPath + '.sha256', ('{0}  {1}' -f $hash, (Split-Path -Leaf $ZipPath)) + "`r`n", $script:Utf8)
    return $hash
}

# ---------------------------------------------------------------------------------------------------

try {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $repo = [string](& git -C $PSScriptRoot rev-parse --show-toplevel)
    if ($LASTEXITCODE -ne 0 -or -not $repo) { throw 'Not inside a git checkout' }
    $repo = [IO.Path]::GetFullPath($repo.Trim())
    $state = Get-RelState $repo

    if ($state.Dirty) {
        $list = ($state.DirtyFiles | Select-Object -First 10) -join '; '
        if ($Validate) { Write-RelLog "uncommitted changes (a package would be refused): $list" 'WARN' }
        elseif (-not $AllowDirty) { Stop-Refused "Uncommitted changes; commit or remove them first: $list" }
        else { Write-RelLog "uncommitted changes, allowed by -AllowDirty: $list" 'WARN' }
    }
    $builds = Test-RelSources $state
    if ($Validate) {
        Write-RelLog "RESULT: sources valid for EternalVR-$Channel-$($state.Version)-$($state.ShortSha); nothing written"
        exit 0
    }

    if (-not $OutDir) { $OutDir = if ($env:EVR_RELEASE_OUT) { $env:EVR_RELEASE_OUT } else { Join-Path (Get-EvrWorkspace) 'tmp-release' } }
    $OutDir = [IO.Path]::GetFullPath($OutDir)
    if ($OutDir.StartsWith('C:', [StringComparison]::OrdinalIgnoreCase)) { Stop-Refused "The output folder must not be on C: ($OutDir)" }

    if (-not $NoBuild) { Invoke-RelBuild $state }
    $payload = Get-RelPayload $state
    Test-RelBinaries $state $payload
    $now = Get-RelState $repo
    if ($now.Sha -ne $state.Sha -or ($now.Dirty -and -not $AllowDirty)) { Stop-Refused 'The checkout changed while packaging' }

    $name = "EternalVR-$Channel-$($state.Version)-$($state.ShortSha)$(if ($state.Dirty) { '-dirty' })"
    $stage = Join-Path $OutDir $name
    $symStage = Join-Path $OutDir "$name-symbols"
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    foreach ($d in $stage, $symStage) { if (Test-Path -LiteralPath $d) { Remove-Item -LiteralPath $d -Recurse -Force } }

    foreach ($pair in @(@($payload.Files, $stage), @($payload.Symbols, $symStage))) {
        foreach ($f in $pair[0]) {
            $dst = Join-Path $pair[1] $f.Target
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $dst) | Out-Null
            Copy-Item -LiteralPath $f.Source -Destination $dst
        }
    }
    Write-RelBuildInfo $state (Join-Path $stage 'BUILD-INFO.txt') $builds
    Copy-Item -LiteralPath (Join-Path $stage 'BUILD-INFO.txt') -Destination (Join-Path $symStage 'BUILD-INFO.txt')
    $summed = Write-RelSums $stage
    [void](Write-RelSums $symStage)

    $zipPath = Join-Path $OutDir "$name.zip"
    $symZip = Join-Path $OutDir "$name-symbols.zip"
    New-RelZip $stage $zipPath $name $state.CommitTime
    New-RelZip $symStage $symZip "$name-symbols" $state.CommitTime
    $count = Test-RelZip $zipPath $name
    [void](Test-RelZip $symZip "$name-symbols" -Symbols)

    $hash = Write-RelZipHash $zipPath
    $symHash = Write-RelZipHash $symZip
    Write-RelLog "packaged $count files ($summed in SHA256SUMS.txt); every entry checked against it"
    Write-RelLog "zip:     $zipPath"
    Write-RelLog "sha256:  $hash"
    Write-RelLog "symbols: $symZip"
    Write-RelLog "sha256:  $symHash"
    exit 0
} catch [RefusedException] {
    Write-RelLog "REFUSED: $($_.Exception.Message)" 'ERROR'
    exit 2
} catch {
    Write-RelLog "ERROR: $($_.Exception.Message)" 'ERROR'
    exit 1
}
