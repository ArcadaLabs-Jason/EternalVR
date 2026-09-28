<#
.SYNOPSIS
Exports the current commit's tree into a fresh repository with a single commit, and sweeps it.

.DESCRIPTION
Makes the first commit of a public repository from this checkout without its history:

  1. refuses a dirty checkout (only committed files are exported);
  2. creates <Target> (it must not exist or must be empty) with `git init`, branch main;
  3. exports HEAD's tree with `git archive`, adds every file and restores executable bits;
  4. checks that the new tree is byte-identical to HEAD's tree (same tree ID);
  5. sweeps the tree and refuses (exit 2, nothing committed) if it finds:
       - a file under reference/ other than reference/MANIFEST.md;
       - any term or path pattern from the private deny list (see -DenyList);
  6. commits once, as -AuthorName <-AuthorEmail>, with the message "EternalVR alpha".

It never adds a remote and never pushes; it prints the commands for that.

.PARAMETER Target
The folder for the new repository. Must not exist, or be empty, and must be outside this checkout.

.PARAMETER DenyList
A text file kept outside the repository (it lists the private strings the tree must not contain, so it
cannot be published itself). Default: publish-denylist.txt in the workspace (tools\rig\workspace.ps1).
One entry per line; '#' starts a comment:
  <text>            refuse any file whose content contains <text> (case-insensitive, also in binaries)
  path:<regex>      refuse any file whose repository path matches <regex> (case-insensitive)
  exclude:<pattern> write <pattern> to the new repository's .git\info\exclude (local only)
The sweep refuses to run without it.

.PARAMETER Message
The commit message. Default: "EternalVR alpha".

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File tools\release\make-public-tree.ps1 -Target D:\src\EternalVR-public

Exit codes: 0 committed, 1 error, 2 refused (dirty checkout, target not empty, sweep findings).
#>
param(
    [Parameter(Mandatory = $true)][string]$Target,
    [string]$DenyList,
    [string]$Message = 'EternalVR alpha',
    [string]$AuthorName = 'Jason Crawford',
    [string]$AuthorEmail = 'jason@arcadalabs.com'
)

$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'rig\workspace.ps1')

function Stop-Refused([string]$why) { Write-Host "REFUSED: $why"; exit 2 }

# Invoke-Git <dir> <git arguments...>; a plain function, so arguments such as -o reach git unchanged.
function Invoke-Git {
    $Dir = $args[0]
    $GitArgs = @($args | Select-Object -Skip 1)
    $out = & git -C $Dir @GitArgs
    if ($LASTEXITCODE -ne 0) { throw "git $($GitArgs -join ' ') failed ($LASTEXITCODE) in $Dir" }
    return $out
}

$repo = [IO.Path]::GetFullPath((Invoke-Git (Split-Path -Parent $PSScriptRoot) rev-parse --show-toplevel))
$Target = [IO.Path]::GetFullPath($Target)
if (-not $DenyList) { $DenyList = Join-Path (Get-EvrWorkspace) 'publish-denylist.txt' }
$DenyList = [IO.Path]::GetFullPath($DenyList)

# ---------------------------------------------------------------------------------------------------
# Preconditions

if ($Target.StartsWith($repo.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or $Target -ieq $repo) {
    Stop-Refused "the target must be outside this checkout ($repo)"
}
if ((Test-Path -LiteralPath $Target) -and @(Get-ChildItem -LiteralPath $Target -Force).Count -gt 0) {
    Stop-Refused "the target is not empty: $Target"
}
if (-not (Test-Path -LiteralPath $DenyList)) {
    Stop-Refused "no deny list at $DenyList (pass -DenyList; the sweep does not run without it)"
}
if ($DenyList.StartsWith($repo.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    Stop-Refused "the deny list must be outside the repository: $DenyList"
}
$dirty = Invoke-Git $repo status --porcelain --untracked-files=no
if ($dirty) { Stop-Refused "the checkout has uncommitted changes; commit or stash them first" }

$terms = New-Object System.Collections.Generic.List[string]
$pathRules = New-Object System.Collections.Generic.List[string]
$excludes = New-Object System.Collections.Generic.List[string]
foreach ($line in [IO.File]::ReadAllLines($DenyList)) {
    $l = $line.Trim()
    if (-not $l -or $l.StartsWith('#')) { continue }
    if ($l.StartsWith('path:')) { $pathRules.Add($l.Substring(5).Trim()) }
    elseif ($l.StartsWith('exclude:')) { $excludes.Add($l.Substring(8).Trim()) }
    else { $terms.Add($l.ToLowerInvariant()) }
}
if ($terms.Count -eq 0) { Stop-Refused "the deny list has no content terms: $DenyList" }

$branch = (Invoke-Git $repo rev-parse --abbrev-ref HEAD)
$sourceSha = (Invoke-Git $repo rev-parse HEAD)
$sourceTree = (Invoke-Git $repo rev-parse 'HEAD^{tree}')
Write-Host "source: $repo, $branch at $sourceSha (tree $sourceTree)"
Write-Host "target: $Target"

# ---------------------------------------------------------------------------------------------------
# Export into a fresh repository

New-Item -ItemType Directory -Force -Path $Target | Out-Null
Invoke-Git $Target init --quiet | Out-Null
Invoke-Git $Target symbolic-ref HEAD refs/heads/main | Out-Null
Invoke-Git $Target config core.autocrlf false | Out-Null
Invoke-Git $Target config core.safecrlf false | Out-Null
if ($excludes.Count -gt 0) {
    $excludeFile = Join-Path $Target '.git\info\exclude'
    [IO.File]::AppendAllText($excludeFile, "`n" + (($excludes | ForEach-Object { $_ }) -join "`n") + "`n")
}

$archive = Join-Path $Target '.git\export.tar'
Invoke-Git $repo archive --format=tar -o $archive HEAD | Out-Null
& tar -xf $archive -C $Target
if ($LASTEXITCODE -ne 0) { throw "tar failed ($LASTEXITCODE)" }
Remove-Item -LiteralPath $archive

Invoke-Git $Target add --all | Out-Null
foreach ($entry in (Invoke-Git $repo ls-tree -r HEAD)) {
    if ($entry -match '^100755 blob [0-9a-f]+\t(.+)$') { Invoke-Git $Target update-index --chmod=+x -- $Matches[1] | Out-Null }
}
$newTree = (Invoke-Git $Target write-tree)
if ($newTree -ne $sourceTree) {
    throw "the exported tree ($newTree) differs from the source tree ($sourceTree); nothing committed"
}
Write-Host "export: tree $newTree matches the source"

# ---------------------------------------------------------------------------------------------------
# Sweep

$findings = New-Object System.Collections.Generic.List[string]
$files = @(Invoke-Git $Target ls-files)
$latin1 = [Text.Encoding]::GetEncoding(28591)
foreach ($path in $files) {
    if ($path -like 'reference/*' -and $path -ne 'reference/MANIFEST.md') { $findings.Add("path: $path (reference/ keeps only MANIFEST.md)") }
    foreach ($rule in $pathRules) {
        if ($path -match "(?i)$rule") { $findings.Add("path: $path (deny-list path rule)") }
    }
    $text = $latin1.GetString([IO.File]::ReadAllBytes((Join-Path $Target $path))).ToLowerInvariant()
    for ($i = 0; $i -lt $terms.Count; $i++) {
        if ($text.Contains($terms[$i])) { $findings.Add("content: $path (deny-list term $($i + 1))") }
    }
}
Write-Host "sweep: $($files.Count) files, $($terms.Count) content terms, $($pathRules.Count) path rules, reference/ rule"
if ($findings.Count -gt 0) {
    $findings | ForEach-Object { Write-Host "  $_" }
    Stop-Refused "$($findings.Count) sweep finding(s); nothing committed. Fix them on the source branch and run again with an empty target."
}
Write-Host 'sweep: clean'

# ---------------------------------------------------------------------------------------------------
# Commit

$env:GIT_AUTHOR_NAME = $AuthorName; $env:GIT_AUTHOR_EMAIL = $AuthorEmail
$env:GIT_COMMITTER_NAME = $AuthorName; $env:GIT_COMMITTER_EMAIL = $AuthorEmail
Invoke-Git $Target -c "user.name=$AuthorName" -c "user.email=$AuthorEmail" commit --quiet -m $Message | Out-Null
$sha = (Invoke-Git $Target rev-parse HEAD)
Write-Host "commit: $sha on main, author $AuthorName <$AuthorEmail>, message '$Message'"
Write-Host ''
Write-Host 'Nothing was pushed. When ready (after creating the empty public repository on GitHub):'
Write-Host "  git -C `"$Target`" remote add origin https://github.com/<owner>/<repo>.git"
Write-Host "  git -C `"$Target`" push -u origin main"
exit 0
