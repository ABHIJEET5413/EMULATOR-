param()

$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "[FAIL] $Name" }
    Write-Output "[PASS] $Name"
}

# Static audit only: it does not claim an installer was run, a VM booted, or a
# clean PC was tested.
foreach ($file in @(
    'docs\phase-guides\phase-12-release-readiness.md',
    'docs\V3-RELEASE-ACCEPTANCE.md',
    'docs\V3-PERFORMANCE-MATRIX.md',
    'host-app\include\runtime_recovery.h',
    'host-app\src\runtime_recovery.cpp',
    'host-app\include\performance_monitor.h',
    'host-app\src\performance_monitor.cpp',
    'installer\sabi.iss',
    'installer\BUILD.md'
)) {
    Check (Test-Path -LiteralPath (Join-Path $root $file) -PathType Leaf) "present: $file"
}

$acceptance = Get-Content -LiteralPath (Join-Path $root 'docs\V3-RELEASE-ACCEPTANCE.md') -Raw
Check ($acceptance -match 'NOT RUN') 'release evidence keeps unperformed manual gates visible'
Check ($acceptance -match 'clean') 'clean-PC gate is documented'
Check ($acceptance -match 'WHPX') 'WHPX gate is documented'
$source = Get-Content -LiteralPath (Join-Path $root 'host-app\src\main.cpp') -Raw
Check ($source -match '--safe-mode') 'safe-mode command line route is present'
$recovery = Get-Content -LiteralPath (Join-Path $root 'host-app\src\runtime_recovery.cpp') -Raw
Check ($recovery -match 'QSaveFile') 'recovery marker writes atomically'
Check ($recovery -notmatch 'QNetwork') 'recovery has no network transport'
Write-Output 'Static release-readiness audit passed. Hardware, installer, and clean-PC gates remain manual.'
