param([string]$BundlePath)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$installer=Join-Path $root 'installer'
function Check([bool]$Condition,[string]$Name){if(-not $Condition){throw "[FAIL] $Name"};Write-Output "[PASS] $Name"}
# Static packaging preflight only: no feature changes, installation or user-data deletion.
foreach($file in @('droidbox.iss','check_whpx.ps1','scripts\post-install.ps1','scripts\uninstall-data.ps1','scripts\prepare-bundle.ps1','scripts\build-installer.ps1','licenses\LICENSE.txt','licenses\QEMU-LICENSE.txt','licenses\QT-LICENSE.txt','licenses\BLISS-LICENSE.txt','licenses\FDROID-LICENSE.txt','licenses\SOURCE-OFFER.txt','resources\droidbox.ico','resources\installer-banner.bmp')){Check (Test-Path -LiteralPath (Join-Path $installer $file) -PathType Leaf) "present: $file"}
foreach($file in Get-ChildItem -LiteralPath $installer -Filter '*.ps1' -File -Recurse | Where-Object {$_.FullName -notlike '*\bundle\*' -and $_.FullName -notlike '*\Output\*'}){
    $tokens=$null;$errors=$null;[Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$tokens,[ref]$errors) | Out-Null
    Check ($errors.Count -eq 0) "PowerShell parses: $($file.Name)"
}
$script=Get-Content (Join-Path $installer 'droidbox.iss') -Raw
Check ($script -match 'DiskSpanning=no') 'single executable configured'
Check ($script -match 'ExecAsOriginalUser') 'original-user initialization configured'
Check ($script -notmatch '(?m)^\[UninstallDelete\]') 'uninstall does not erase kept data unconditionally'
Check ($script -match '""%1""') 'APK argument quoted'
foreach($license in @('QEMU-LICENSE.txt','QT-LICENSE.txt','BLISS-LICENSE.txt','FDROID-LICENSE.txt')){Check ((Get-Item (Join-Path $installer ('licenses\'+$license))).Length -gt 7000) "full license text: $license"}
if($BundlePath){
    $bundle=[IO.Path]::GetFullPath($BundlePath);$manifest=Get-Content (Join-Path $bundle 'manifest.json') -Raw | ConvertFrom-Json
    foreach($record in $manifest.files){$path=[IO.Path]::GetFullPath((Join-Path $bundle $record.path));if(-not $path.StartsWith($bundle+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe payload path.'};if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $record.sha256){throw "Payload hash mismatch: $($record.path)"}}
    Check $true "$($manifest.files.Count) payload paths and hashes verified"
}
Write-Output 'Packaging preflight passed. Installer/guest acceptance remains manual.'
