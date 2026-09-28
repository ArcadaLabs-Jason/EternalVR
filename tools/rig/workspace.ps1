# The workspace folder (dot-sourced; Windows PowerShell 5.1 compatible): where runs, backups, the lab
# copy, staged builds and release output live by default. EVR_WORKSPACE when set, otherwise the folder
# that contains the main checkout (for a linked worktree, the checkout its .git folder belongs to).

function Get-EvrWorkspace {
    $ws = [Environment]::GetEnvironmentVariable('EVR_WORKSPACE')
    if (-not [string]::IsNullOrWhiteSpace($ws)) { return [IO.Path]::GetFullPath($ws) }
    $repo = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # tools\rig -> the checkout
    try {
        $common = & git -C $repo rev-parse --git-common-dir 2>$null
        if ($LASTEXITCODE -eq 0 -and $common) {
            $common = [string]$common
            if (-not [IO.Path]::IsPathRooted($common)) { $common = Join-Path $repo $common }
            $repo = Split-Path -Parent ([IO.Path]::GetFullPath($common))
        }
    } catch { }
    return (Split-Path -Parent ([IO.Path]::GetFullPath($repo)))
}
