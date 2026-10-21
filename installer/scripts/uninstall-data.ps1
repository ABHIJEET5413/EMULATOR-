[CmdletBinding()]
param([switch]$Delete)
$ErrorActionPreference='Stop'
try {
    # Only this account's fixed application directory is eligible. Other users'
    # data is retained; no path is accepted from the command line or registry.
    $local=[Environment]::GetFolderPath('LocalApplicationData')
    $target=[IO.Path]::GetFullPath((Join-Path $local 'DroidBox\DroidBox'))
    $expected=[IO.Path]::GetFullPath($local).TrimEnd('\')+'\DroidBox\DroidBox'
    if ($target -ne $expected -or -not $Delete) { throw 'Explicit deletion of this account''s DroidBox data is required.' }
    if (-not (Test-Path -LiteralPath $target)) { exit 0 }
    if (Get-Process -Name DroidBox -ErrorAction SilentlyContinue) { throw 'Close all DroidBox windows before deleting instance data.' }
    $pending=[Collections.Generic.Stack[string]]::new()
    foreach($path in @((Join-Path $local 'DroidBox'),$target)) {
        if ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'DroidBox data root must not be a junction or link.' }
    }
    $pending.Push($target)
    while($pending.Count) {
        foreach($entry in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'DroidBox data contains a junction or link. Remove it manually first.' }
            if ($entry.PSIsContainer) { $pending.Push($entry.FullName) }
        }
    }
    $instances=Join-Path $target 'instances'
    $handles=@()
    try {
        if(Test-Path -LiteralPath $instances) {
            foreach($disk in Get-ChildItem -LiteralPath $instances -Filter '*.qcow2' -Recurse -File) {
                # Hold every disk without write sharing until all checks pass.
                $handles+=[IO.File]::Open($disk.FullName,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read -bor [IO.FileShare]::Delete)
            }
        }
        Remove-Item -LiteralPath $target -Recurse -Force
    } finally { foreach($handle in $handles) { $handle.Dispose() } }
    exit 0
} catch { Write-Error $_.Exception.Message -ErrorAction Continue; exit 1 }
