[CmdletBinding()]
param(
    [string]$AppDirectory,
    [string]$QtRoot='C:\Qt\6.12.0\msvc2022_64',
    [string]$QemuRoot,
    [string]$AdbRoot,
    [string]$BaseImage,
    [string]$RuntimeInstaller,
    [string]$SourceDirectory,
    [switch]$CopyImage
)
$ErrorActionPreference='Stop'; $ProgressPreference='SilentlyContinue'
$installer=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$repository=[IO.Path]::GetFullPath((Join-Path $installer '..'))
if(-not $AppDirectory){$AppDirectory=Join-Path $repository 'cmake-build-phase6-fixes\Release'}
if(-not $QemuRoot){$QemuRoot=Join-Path $repository 'vm\qemu'}
if(-not $AdbRoot){$AdbRoot=Join-Path $repository 'vm\adb'}
if(-not $BaseImage){$BaseImage=Join-Path $repository 'vm\images\bliss-distribution-v2.qcow2'}
if(-not $RuntimeInstaller){$RuntimeInstaller=Join-Path $repository '_downloads\phase6\vc_redist.x64.exe'}
if(-not $SourceDirectory){$SourceDirectory=Join-Path $repository '_downloads\phase6\sources'}
$lock=Get-Content (Join-Path $installer 'source-lock.json') -Raw | ConvertFrom-Json
function Assert-File([string]$Path){ if(-not (Test-Path -LiteralPath $Path -PathType Leaf)){throw "Required file missing: $Path"} }
function Assert-Hash([string]$Path,[string]$Expected){Assert-File $Path;if((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $Expected){throw "SHA-256 mismatch: $Path"}}
foreach($path in @((Join-Path $AppDirectory 'Sabi.exe'),(Join-Path $QtRoot 'bin\windeployqt.exe'),(Join-Path $QemuRoot 'qemu-system-x86_64.exe'),(Join-Path $QemuRoot 'qemu-img.exe'),(Join-Path $AdbRoot 'adb.exe'),$RuntimeInstaller)){Assert-File $path}
Assert-Hash $BaseImage $lock.baseImageSha256
foreach($source in $lock.sources){Assert-Hash (Join-Path $SourceDirectory $source.file) $source.sha256}
if((Get-AuthenticodeSignature -LiteralPath $RuntimeInstaller).Status -ne 'Valid'){throw 'Microsoft runtime package must have a valid signature.'}
$qemuVersion=(& (Join-Path $QemuRoot 'qemu-system-x86_64.exe') --version | Out-String).Trim()
if($LASTEXITCODE -ne 0 -or -not $qemuVersion.Contains($lock.qemuVersionContains)){throw 'QEMU binary does not match source-lock.json.'}
$adbVersion=(& (Join-Path $AdbRoot 'adb.exe') version | Out-String).Trim()
if($LASTEXITCODE -ne 0 -or -not $adbVersion.Contains($lock.adbVersionContains)){throw 'ADB binary does not match source-lock.json.'}
$qtVersion=(Get-Item (Join-Path $QtRoot 'bin\Qt6Core.dll')).VersionInfo.ProductVersion
if(-not $qtVersion.StartsWith($lock.qtVersion)){throw 'Qt kit does not match the collected sources. Update the reviewed source lock first.'}
$info=& (Join-Path $QemuRoot 'qemu-img.exe') info --output=json $BaseImage
if($LASTEXITCODE -ne 0){throw 'Cannot inspect the clean base image.'}
$image=$info | ConvertFrom-Json
if($image.format -ne 'qcow2' -or $image.'backing-filename'){throw 'The distribution base must be a standalone qcow2.'}
$bundle=Join-Path $installer 'bundle'
# Rebuild only the generated bundle. Never delete a caller-supplied path.
if(Test-Path -LiteralPath $bundle){
    if([IO.Path]::GetFullPath($bundle) -ne ($installer+'\bundle')){throw 'Invalid bundle cleanup path.'}
    $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($bundle)
    while($pending.Count){$dir=$pending.Pop();if((Get-Item -LiteralPath $dir).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Bundle cleanup refuses junctions/links.'};foreach($entry in Get-ChildItem -LiteralPath $dir -Force){if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Bundle cleanup refuses junctions/links.'};if($entry.PSIsContainer){$pending.Push($entry.FullName)}}}
    Remove-Item -LiteralPath $bundle -Recurse -Force
}
foreach($folder in @('app','qemu','adb','images','configs','vcredist','sources','notices')){New-Item -ItemType Directory -Path (Join-Path $bundle $folder) -Force | Out-Null}
Copy-Item -LiteralPath (Join-Path $AppDirectory 'Sabi.exe') -Destination (Join-Path $bundle 'app')
& (Join-Path $QtRoot 'bin\windeployqt.exe') --release --no-translations --no-opengl-sw --no-compiler-runtime --dir (Join-Path $bundle 'app') (Join-Path $bundle 'app\Sabi.exe')
if($LASTEXITCODE -ne 0){throw 'windeployqt failed.'}
Copy-Item -LiteralPath (Join-Path $repository 'host-app\resources') -Destination (Join-Path $bundle 'app') -Recurse
foreach($name in @('qemu-system-x86_64.exe','qemu-img.exe','COPYING','COPYING.LIB','VERSION','README.rst')){Copy-Item -LiteralPath (Join-Path $QemuRoot $name) -Destination (Join-Path $bundle 'qemu')}
Copy-Item -Path (Join-Path $QemuRoot '*.dll') -Destination (Join-Path $bundle 'qemu')
foreach($folder in @('share','lib')){if(Test-Path -LiteralPath (Join-Path $QemuRoot $folder)){Copy-Item -LiteralPath (Join-Path $QemuRoot $folder) -Destination (Join-Path $bundle 'qemu') -Recurse}}
foreach($name in @('adb.exe','AdbWinApi.dll','AdbWinUsbApi.dll','NOTICE.txt','source.properties')){Assert-File (Join-Path $AdbRoot $name);Copy-Item -LiteralPath (Join-Path $AdbRoot $name) -Destination (Join-Path $bundle 'adb')}
$stagedBase=Join-Path $bundle 'images\bliss-provisioned.qcow2'
if(-not $CopyImage){try{New-Item -ItemType HardLink -Path $stagedBase -Value (Resolve-Path -LiteralPath $BaseImage).Path -ErrorAction Stop | Out-Null}catch{Copy-Item -LiteralPath $BaseImage -Destination $stagedBase}}
else{Copy-Item -LiteralPath $BaseImage -Destination $stagedBase}
# A staged hard link is only read by the compiler. Never boot or edit it.
Copy-Item -LiteralPath $RuntimeInstaller -Destination (Join-Path $bundle 'vcredist\vc_redist.x64.exe')
$config=Get-Content (Join-Path $repository 'vm\configs\default-instance.json') -Raw | ConvertFrom-Json
$config.baseImage='vm/images/bliss-provisioned.qcow2'
$config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $bundle 'configs\default-instance.json') -Encoding UTF8
@{schemaVersion=1;baseImageSha256=$lock.baseImageSha256;baseImageBytes=(Get-Item -LiteralPath $BaseImage).Length} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $bundle 'app\installed-layout.json') -Encoding UTF8
foreach($source in $lock.sources){Copy-Item -LiteralPath (Join-Path $SourceDirectory $source.file) -Destination (Join-Path $bundle 'sources')}
Copy-Item -LiteralPath (Join-Path $installer 'source-lock.json') -Destination (Join-Path $bundle 'sources')
$projectFiles=@(& git -C $repository ls-files --cached --others --exclude-standard -- host-app image-build vm/configs vm/scripts)
if($LASTEXITCODE -ne 0){throw 'Cannot collect the current Sabi source files.'}
foreach($relative in $projectFiles){$destination=Join-Path $bundle ('sources\sabi\'+$relative);New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null;Copy-Item -LiteralPath (Join-Path $repository $relative) -Destination $destination}
Copy-Item -LiteralPath (Join-Path $AdbRoot 'NOTICE.txt') -Destination (Join-Path $bundle 'notices\ADB-NOTICE.txt')
Copy-Item -LiteralPath (Join-Path $QemuRoot 'COPYING'),(Join-Path $QemuRoot 'COPYING.LIB') -Destination (Join-Path $bundle 'notices')
foreach($module in @('qtbase','qtsvg')){foreach($suffix in @('cdx.json','spdx.json')){$sbom=Join-Path $QtRoot ('sbom\'+$module+'-'+$lock.qtVersion+'.'+$suffix);if(Test-Path -LiteralPath $sbom){Copy-Item -LiteralPath $sbom -Destination (Join-Path $bundle 'notices')}}}
$records=@(Get-ChildItem -LiteralPath $bundle -File -Recurse | ForEach-Object {@{path=$_.FullName.Substring($bundle.Length+1).Replace('\','/');bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}})
@{schemaVersion=1;createdUtc=[DateTime]::UtcNow.ToString('o');qtVersion=$qtVersion;qemuVersion=$qemuVersion;adbVersion=$adbVersion;releaseReady=$false;remainingReleaseInventory=$lock.remainingReleaseInventory;files=$records} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $bundle 'manifest.json') -Encoding UTF8
Write-Output "Prepared offline bundle: $bundle"
Write-Output 'The complete runtime and collected source inputs are staged. Public distribution requires the release inventory described in BUILD.md.'
