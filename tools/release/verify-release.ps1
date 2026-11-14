<#
.SYNOPSIS
Checks a published EternalVR release against its checksums: the uploaded zip, its .sha256 and the SHA256SUMS.txt
inside it (T-105, ROADMAP M10).

.DESCRIPTION
Downloads the release's zip and its .sha256 with the GitHub CLI (gh release download) into a fresh temporary folder
and checks, as a player's download would be:
  - the release is published and holds one EternalVR zip (a -symbols zip aside) and its <zip>.sha256;
  - the download is the uploaded size, and its SHA-256 equals the one in the .sha256 file, which names the zip, and
    the digest GitHub lists for the uploaded asset (when GitHub lists one);
  - the zip has one top folder, named as the zip, and its SHA256SUMS.txt lists every other file in it exactly once,
    each matching its line;
  - BUILD-INFO.txt names the release's version and its commit, and lists the supported game builds, each Steam build
    with its exe's SHA-256.
Nothing is written outside the temporary folder, which is removed afterwards. Runs by hand and in CI (ci.yml, job
"Published release"); needs gh, logged in or with GH_TOKEN set, and the network. With -Folder it checks a package
before its upload instead (make-release.ps1's output folder: the zip and its .sha256), without gh.

  powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\verify-release.ps1 [-Tag v0.1.92]
  powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\verify-release.ps1 -Folder <release output folder>

Exit codes: 0 every check passed, 1 a check failed, 2 the release or its files could not be read.

.PARAMETER Repo
The repository whose release is checked. Default: the public one, ArcadaLabs-Jason/EternalVR.

.PARAMETER Tag
The release's tag. Default: the newest published release (prereleases included, drafts not); with -Folder, the
zip's version.

.PARAMETER WaitSeconds
How long to wait for the zip and its .sha256 to be uploaded (a release checked as soon as it is published may not
have them yet). Default 0.

.PARAMETER Folder
A folder holding one EternalVR zip and its .sha256, checked in place of a release's upload.
#>
param(
    [string]$Repo = 'ArcadaLabs-Jason/EternalVR',
    [string]$Tag,
    [int]$WaitSeconds = 0,
    [string]$Folder
)

$ErrorActionPreference = 'Stop'
$script:failures = 0

function Write-Check([bool]$Ok, [string]$What) {
    if ($Ok) { Write-Host "  ok   $What" } else { Write-Host "  FAIL $What"; $script:failures++ }
}

function Stop-Unreadable([string]$Why) { Write-Host "RESULT: could not check: $Why"; exit 2 }

function Invoke-Gh([string[]]$Arguments) {
    $out = & gh @Arguments
    if ($LASTEXITCODE -ne 0) { Stop-Unreadable "gh $($Arguments -join ' ') failed ($LASTEXITCODE)" }
    return $out
}

function Get-StreamSha256([IO.Stream]$Stream) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Stream)) -replace '-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Read-EntryText($Entry) {
    $reader = New-Object IO.StreamReader($Entry.Open())
    try { return $reader.ReadToEnd() } finally { $reader.Dispose() }
}

# The release's upload: its zip asset (name, size, digest), downloaded with its .sha256 into $Work.
function Get-ReleaseFiles([string]$Work) {
    if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { Stop-Unreadable 'the GitHub CLI (gh) is not installed' }
    if (-not $script:Tag) {
        $script:Tag = ([string](Invoke-Gh @('release', 'list', '-R', $Repo, '--limit', '1', '--exclude-drafts', '--json', 'tagName', '--jq', '.[0].tagName'))).Trim()
        if (-not $script:Tag) { Stop-Unreadable "$Repo has no published release" }
    }
    Write-Host "release $script:Tag of $Repo"
    $deadline = (Get-Date).AddSeconds($WaitSeconds)
    while ($true) {
        $release = (Invoke-Gh @('release', 'view', $script:Tag, '-R', $Repo, '--json', 'isDraft,assets')) -join "`n" | ConvertFrom-Json
        $zips = @($release.assets | Where-Object { $_.name -like 'EternalVR-*.zip' -and $_.name -notlike '*-symbols.zip' -and $_.state -eq 'uploaded' })
        $sums = @($release.assets | Where-Object { $zips.Count -eq 1 -and $_.name -eq ($zips[0].name + '.sha256') -and $_.state -eq 'uploaded' })
        if (($zips.Count -eq 1 -and $sums.Count -eq 1) -or (Get-Date) -ge $deadline) { break }
        Start-Sleep -Seconds 15
    }
    Write-Check (-not $release.isDraft) 'the release is published (not a draft)'
    Write-Check ($zips.Count -eq 1) "one EternalVR zip is uploaded ($(@($zips | ForEach-Object { $_.name }) -join ', '))"
    Write-Check ($sums.Count -eq 1) 'its .sha256 is uploaded next to it'
    if ($zips.Count -ne 1 -or $sums.Count -ne 1) { return $null }
    $name = $zips[0].name
    [void](Invoke-Gh @('release', 'download', $script:Tag, '-R', $Repo, '-p', $name, '-p', "$name.sha256", '-D', $Work))
    return [pscustomobject]@{ Zip = (Join-Path $Work $name); Size = [long]$zips[0].size; Digest = [string]$zips[0].digest }
}

# A package in -Folder: its one zip (a -symbols zip aside), with its .sha256 next to it.
function Get-FolderFiles {
    $found = @(Get-ChildItem -LiteralPath $Folder -File -Filter 'EternalVR-*.zip' | Where-Object { $_.Name -notlike '*-symbols.zip' })
    if ($found.Count -ne 1) { Stop-Unreadable "$Folder holds $($found.Count) EternalVR zip(s), not one" }
    if ($found[0].Name -notmatch '^EternalVR-[a-z]+-(\d+\.\d+\.\d+)-') { Stop-Unreadable "$($found[0].Name) is not named EternalVR-<channel>-<version>-<commit>.zip" }
    if (-not $script:Tag) { $script:Tag = 'v' + $Matches[1] }
    if (-not (Test-Path -LiteralPath ($found[0].FullName + '.sha256'))) { Stop-Unreadable "$($found[0].Name).sha256 is not next to it" }
    Write-Host "package $($found[0].Name) in $Folder"
    return [pscustomobject]@{ Zip = $found[0].FullName; Size = $null; Digest = $null }
}

# A tag is a name, never anything a command line could read otherwise (CI passes the release event's).
if ($Tag -and $Tag -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$') { Stop-Unreadable "not a release tag: $Tag" }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$work = Join-Path ([IO.Path]::GetTempPath()) ('evr-verify-' + [Guid]::NewGuid().ToString('N').Substring(0, 12))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    $files = if ($Folder) { Get-FolderFiles } else { Get-ReleaseFiles $work }
    if (-not $files) { Write-Host "RESULT: FAILED ($script:failures check(s)) for $Tag"; exit 1 }
    $zipPath = $files.Zip
    $zipName = Split-Path -Leaf $zipPath

    # The zip against its .sha256 and GitHub's digest.
    $zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host "  zip sha256 $zipHash"
    if ($null -ne $files.Size) { Write-Check ((Get-Item -LiteralPath $zipPath).Length -eq $files.Size) "the download is the uploaded size ($($files.Size) bytes)" }
    $sumsLine = ([IO.File]::ReadAllText($zipPath + '.sha256')).Trim()
    $listed = if ($sumsLine -match '^([0-9a-fA-F]{64}) [ *](.+)$') { @{ Hash = $Matches[1].ToLowerInvariant(); Name = $Matches[2].Trim() } } else { $null }
    Write-Check ($null -ne $listed) "$zipName.sha256 is one '<sha256>  <file>' line"
    if ($listed) {
        Write-Check ($listed.Name -eq $zipName) "$zipName.sha256 names the zip ($($listed.Name))"
        Write-Check ($listed.Hash -eq $zipHash) "the zip matches $zipName.sha256"
    }
    if ($files.Digest -match '^sha256:([0-9a-f]{64})$') { Write-Check ($Matches[1] -eq $zipHash) 'the zip matches the digest GitHub lists for the upload' }
    elseif ($files.Digest) { Write-Check $false "GitHub's digest for the upload is not sha256:<64 hex> ($($files.Digest))" }
    elseif (-not $Folder) { Write-Host '  note GitHub lists no digest for this upload' }

    # Every file in the zip against SHA256SUMS.txt.
    $top = [IO.Path]::GetFileNameWithoutExtension($zipName)
    $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $entries = @{}
        $outside = @()
        foreach ($e in $zip.Entries) {
            if ($e.FullName.EndsWith('/')) { continue }
            if ($e.FullName.StartsWith("$top/")) { $entries[$e.FullName.Substring($top.Length + 1)] = $e } else { $outside += $e.FullName }
        }
        Write-Check ($outside.Count -eq 0) "every file is in the top folder $top ($($outside.Count) outside)"
        $sumsEntry = $entries['SHA256SUMS.txt']
        Write-Check ($null -ne $sumsEntry) 'SHA256SUMS.txt is in the zip'
        if ($sumsEntry) {
            $sums = @{}
            $bad = @()
            $twice = @()
            foreach ($line in ((Read-EntryText $sumsEntry) -split "`r?`n" | Where-Object { $_ })) {
                if ($line -notmatch '^([0-9a-f]{64})  (.+)$') { $bad += $line; continue }
                if ($sums.ContainsKey($Matches[2])) { $twice += $Matches[2] }
                $sums[$Matches[2]] = $Matches[1]
            }
            Write-Check ($bad.Count -eq 0) "every SHA256SUMS.txt line is '<sha256>  <path>' ($($bad.Count) not)"
            Write-Check ($twice.Count -eq 0) "no file is listed twice$(if ($twice.Count) { ' (' + ($twice -join ', ') + ')' })"
            $unlisted = @($entries.Keys | Where-Object { $_ -ne 'SHA256SUMS.txt' -and -not $sums.ContainsKey($_) })
            Write-Check ($unlisted.Count -eq 0) "every file in the zip is listed$(if ($unlisted.Count) { ' (not: ' + ($unlisted -join ', ') + ')' })"
            $missing = @($sums.Keys | Where-Object { -not $entries.ContainsKey($_) })
            Write-Check ($missing.Count -eq 0) "every listed file is in the zip$(if ($missing.Count) { ' (not: ' + ($missing -join ', ') + ')' })"
            $changed = @()
            foreach ($name in $sums.Keys) {
                if (-not $entries.ContainsKey($name)) { continue }
                $s = $entries[$name].Open()
                try { $h = Get-StreamSha256 $s } finally { $s.Dispose() }
                if ($h -ne $sums[$name]) { $changed += $name }
            }
            Write-Check ($changed.Count -eq 0) "$($sums.Count - $changed.Count) of $($sums.Count) listed files match SHA256SUMS.txt$(if ($changed.Count) { ' (not: ' + ($changed -join ', ') + ')' })"
        }

        # BUILD-INFO.txt: the version, the commit and the supported game builds.
        $infoEntry = $entries['BUILD-INFO.txt']
        Write-Check ($null -ne $infoEntry) 'BUILD-INFO.txt is in the zip'
        if ($infoEntry) {
            $info = @((Read-EntryText $infoEntry) -split "`r?`n")
            $version = $Tag.TrimStart('v')
            Write-Check ($info[0] -match "^EternalVR \S+ $([regex]::Escape($version))$") "BUILD-INFO.txt is for $version ($($info[0]))"
            Write-Check ([bool]($info | Where-Object { $_ -match '^commit: [0-9a-f]{40}$' })) 'BUILD-INFO.txt names its commit'
            $steam = @($info | Where-Object { $_ -match '^\s+[0-9a-f]{64} \| \S+' })
            $store = @($info | Where-Object { $_ -match '^\s+gamepass \| \S+ \| \S+' })
            Write-Check ($steam.Count -ge 1) "BUILD-INFO.txt lists the supported Steam builds with their exe's SHA-256 ($($steam.Count))"
            Write-Check ($store.Count -ge 1) "BUILD-INFO.txt lists the supported Game Pass versions ($($store.Count))"
            foreach ($b in $steam + $store) { Write-Host "       $($b.Trim())" }
        }
    } finally { $zip.Dispose() }
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

if ($script:failures -gt 0) { Write-Host "RESULT: FAILED ($script:failures check(s)) for $Tag"; exit 1 }
Write-Host "RESULT: $Tag matches its checksums"
exit 0
