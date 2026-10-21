param()
$ErrorActionPreference='Stop'
$repository=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$checkScript=Join-Path $repository 'installer\check_whpx.ps1'
$temporary=Join-Path ([IO.Path]::GetTempPath()) ('droidbox-whpx-tests-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $temporary | Out-Null
function Check([bool]$Success,[string]$Message) {
    if(-not $Success){throw "[FAIL] $Message"}
    Write-Output "[PASS] $Message"
}
function Run-Child([string]$Arguments) {
    $child=[Diagnostics.Process]::new()
    try {
        $child.StartInfo.FileName=Join-Path $PSHOME 'powershell.exe'
        $child.StartInfo.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass '+$Arguments
        $child.StartInfo.UseShellExecute=$false
        $child.StartInfo.CreateNoWindow=$true
        $child.StartInfo.RedirectStandardOutput=$true
        $child.StartInfo.RedirectStandardError=$true
        if(-not $child.Start()){throw 'Could not start the WHPX test process.'}
        $output=$child.StandardOutput.ReadToEndAsync()
        $errors=$child.StandardError.ReadToEndAsync()
        if(-not $child.WaitForExit(30000)){$child.Kill();$child.WaitForExit();throw 'WHPX test process timed out.'}
        return [pscustomobject]@{Code=$child.ExitCode;Output=$output.Result;Errors=$errors.Result}
    } finally {$child.Dispose()}
}
try {
    # This exact -File invocation reproduced the Windows PowerShell 5.1 failure:
    # a PSScriptRoot-based parameter default failed before error reporting began.
    # No -Enable/-Probe switches: this reads status without changing features.
    $report=Join-Path $temporary 'native.json'
    $native=Run-Child ('-File "'+$checkScript+'" -ResultPath "'+$report+'"')
    Check (Test-Path -LiteralPath $report) ('default-path invocation produces JSON: '+$native.Errors)
    $result=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    Check (@(0,10,11,12,20) -contains $native.Code) 'default-path invocation returns a defined status code'
    Check ([bool]$result.status -and [bool]$result.message) 'status is reported even without elevation'

    # Child-local mocks cover firmware/feature states without modifying Windows.
    $driver=Join-Path $temporary 'mock-driver.ps1'
    $driverText=@'
param([string]$CheckScript,[string]$Report,[string]$Scenario)
$global:testWhpxFeature=if($Scenario -like 'Enable*' -or $Scenario -eq 'Disabled'){'Disabled'}elseif($Scenario -eq 'Pending'){'EnablePending'}else{'Enabled'}
function Get-CimInstance {
    param([string]$ClassName)
    if($ClassName -eq 'Win32_ComputerSystem'){return [pscustomobject]@{HypervisorPresent=($Scenario -eq 'HypervisorActive')}}
    return [pscustomobject]@{VirtualizationFirmwareEnabled=($Scenario -ne 'FirmwareDisabled' -and $Scenario -ne 'HypervisorActive')}
}
function Get-WindowsOptionalFeature {
    param([switch]$Online,[string]$FeatureName)
    if($Scenario -eq 'QueryError'){throw 'Simulated feature query failure.'}
    return [pscustomobject]@{State=$global:testWhpxFeature}
}
function Enable-WindowsOptionalFeature {
    param([switch]$Online,[string]$FeatureName,[switch]$All,[switch]$NoRestart,[switch]$LimitAccess)
    if(-not $All -or -not $NoRestart -or -not $LimitAccess){throw 'Enablement must be offline and must not restart Windows.'}
    if($Scenario -eq 'EnableError'){throw 'Simulated missing local component files.'}
    $global:testWhpxFeature='Enabled'
    return [pscustomobject]@{RestartNeeded=($Scenario -eq 'EnableRestart')}
}
$options=@{ResultPath=$Report}
if($Scenario -like 'Enable*'){$options.Enable=$true}
if($Scenario -eq 'MissingQemu'){$options.Probe=$true;$options.QemuPath=Join-Path (Split-Path -Parent $Report) 'missing.exe'}
& $CheckScript @options
exit $LASTEXITCODE
'@
    [IO.File]::WriteAllText($driver,$driverText)
    $cases=@(
        @('Ready','Ready',0), @('HypervisorActive','Ready',0),
        @('Disabled','FeatureDisabled',10), @('Pending','RestartRequired',11),
        @('FirmwareDisabled','FirmwareDisabled',12), @('QueryError','Error',20),
        @('EnableRestart','RestartRequired',11), @('EnableReady','Ready',0),
        @('EnableError','Error',20), @('MissingQemu','Error',20)
    )
    foreach($case in $cases) {
        $report=Join-Path $temporary ($case[0]+'.json')
        $child=Run-Child ('-File "'+$driver+'" -CheckScript "'+$checkScript+'" -Report "'+$report+'" -Scenario '+$case[0])
        Check (Test-Path -LiteralPath $report) ($case[0]+' writes a report: '+$child.Errors)
        $result=Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
        Check ($child.Code -eq $case[2] -and $result.status -eq $case[1]) ($case[0]+' returns '+$case[1]+'/'+$case[2]+'; actual '+$result.status+'/'+$child.Code+' '+$result.message)
        Check (Test-Path -LiteralPath ($report+'.txt')) ($case[0]+' writes the installer-readable message')
    }
    Write-Output 'WHPX script regression checks passed; actual hardware probe is separate.'
} finally {
    $resolved=[IO.Path]::GetFullPath($temporary)
    $parent=[IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')+'\'
    if($resolved.StartsWith($parent,[StringComparison]::OrdinalIgnoreCase) -and (Split-Path -Leaf $resolved) -like 'droidbox-whpx-tests-*') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
