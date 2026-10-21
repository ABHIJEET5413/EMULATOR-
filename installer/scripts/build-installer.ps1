[CmdletBinding()]
param([string]$IsccPath,[switch]$ForDistribution)
$ErrorActionPreference='Stop'
$installer=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $IsccPath){foreach($candidate in @((Join-Path $env:LOCALAPPDATA 'Programs\InnoSetup6\ISCC.exe'),"${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",'C:\Program Files\Inno Setup 6\ISCC.exe')){if(Test-Path -LiteralPath $candidate){$IsccPath=$candidate;break}}}
if(-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath)){throw 'Install Inno Setup 6.x or supply -IsccPath.'}
$bundle=Join-Path $installer 'bundle'
$manifest=Get-Content (Join-Path $bundle 'manifest.json') -Raw | ConvertFrom-Json
$manifest.releaseReady=$false
if($ForDistribution){
    $inventory=Join-Path $installer 'release-inventory.json'
    if(-not (Test-Path -LiteralPath $inventory)){throw 'Public distribution requires a reviewed release-inventory.json. See BUILD.md.'}
    $approval=Get-Content $inventory -Raw | ConvertFrom-Json
    if($approval.complete -ne $true -or -not $approval.sourceOfferContact -or -not $approval.reviewedBy -or -not $approval.sourceArtifacts){throw 'Release inventory is incomplete.'}
    if($approval.sourceOfferContact -ne 'https://github.com/HEARTLESS5413/EMULATOR-/issues'){throw 'Release contact must match SOURCE-OFFER.txt. Review both together before changing it.'}
    foreach($record in $approval.sourceArtifacts){$path=[IO.Path]::GetFullPath((Join-Path $bundle $record.path));if(-not $path.StartsWith($bundle+'\sources\',[StringComparison]::OrdinalIgnoreCase) -or -not $record.sha256 -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $record.sha256){throw "Missing or mismatched release source: $path"}}
    Copy-Item -LiteralPath $inventory -Destination (Join-Path $bundle 'notices\release-inventory.json') -Force
    $manifest.releaseReady=$true
}
foreach($record in $manifest.files){
    $path=[IO.Path]::GetFullPath((Join-Path $bundle $record.path))
    if(-not $path.StartsWith($bundle+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe manifest path.'}
    if((Get-Item -LiteralPath $path).Length -ne $record.bytes -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $record.sha256){throw "Staged payload changed: $path"}
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $bundle 'manifest.json') -Encoding UTF8
& $IsccPath /Qp (Join-Path $installer 'droidbox.iss')
if($LASTEXITCODE -ne 0){throw 'Inno Setup compilation failed.'}
$output=Join-Path $installer 'Output\DroidBoxSetup.exe'
if((Get-Item -LiteralPath $output).Length -ge 4294967296){throw 'The single EXE exceeds the Windows limit. Compact the image; do not enable disk spanning.'}
Get-Item -LiteralPath $output | Format-List FullName,Length
Get-FileHash -LiteralPath $output -Algorithm SHA256 | Format-List Algorithm,Hash,Path
