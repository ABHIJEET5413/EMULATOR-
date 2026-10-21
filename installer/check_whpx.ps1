[CmdletBinding()]
param(
    [switch]$Enable,
    [switch]$Probe,
    [string]$QemuPath = (Join-Path $PSScriptRoot 'vm\qemu\qemu-system-x86_64.exe'),
    [string]$ResultPath
)
$ErrorActionPreference = 'Stop'
# Explicit exit codes: 0 ready, 10 disabled, 11 restart, 12 firmware, 20 unknown/error.
$result = [ordered]@{ status='Unknown'; message=''; firmware='Unknown'; feature='Unknown'; restartRequired=$false; probePassed=$false }
$code = 20
try {
    $machine = Get-CimInstance Win32_ComputerSystem
    $processors = @(Get-CimInstance Win32_Processor)
    if ($machine.HypervisorPresent) { $result.firmware = 'HypervisorActive' }
    elseif (@($processors | Where-Object { $_.VirtualizationFirmwareEnabled -eq $true }).Count) { $result.firmware = 'Enabled' }
    elseif ($processors.Count -and @($processors | Where-Object { $_.VirtualizationFirmwareEnabled -eq $false }).Count -eq $processors.Count) { $result.firmware = 'Disabled' }
    $feature = Get-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform
    $result.feature = [string]$feature.State
    if ($Enable -and $result.feature -eq 'Disabled') {
        # Stay offline: missing local component files must produce an error.
        $change = Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform -All -NoRestart -LimitAccess
        $result.restartRequired = [bool]$change.RestartNeeded
        $result.feature = [string](Get-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform).State
    }
    if ($result.feature -match 'Pending' -or $result.restartRequired) {
        $result.status='RestartRequired'; $result.restartRequired=$true; $code=11
        $result.message='Restart Windows before starting Android. The installer will not force a restart.'
    } elseif ($result.feature -ne 'Enabled') {
        $result.status='FeatureDisabled'; $code=10
        $result.message='Windows Hypervisor Platform is disabled. Enable it and restart Windows.'
    } elseif ($result.firmware -eq 'Disabled') {
        $result.status='FirmwareDisabled'; $code=12
        $result.message='Enable Intel VT-x or AMD SVM in BIOS/UEFI, then restart Windows.'
    } else {
        $result.status='Ready'; $code=0
        $result.message='Windows Hypervisor Platform is enabled.'
        if ($Probe) {
            if (-not (Test-Path -LiteralPath $QemuPath -PathType Leaf)) { throw 'Bundled QEMU is missing.' }
            $process = [Diagnostics.Process]::new()
            $started = $false
            try {
                $process.StartInfo.FileName = $QemuPath
                $process.StartInfo.WorkingDirectory = Split-Path -Parent $QemuPath
                $process.StartInfo.Arguments = '-accel whpx,kernel-irqchip=off -m 512 -smp 1 -cpu qemu64 -display none -S -qmp stdio'
                $process.StartInfo.UseShellExecute=$false; $process.StartInfo.CreateNoWindow=$true
                $process.StartInfo.RedirectStandardInput=$true; $process.StartInfo.RedirectStandardOutput=$true; $process.StartInfo.RedirectStandardError=$true
                if (-not $process.Start()) { throw 'QEMU could not start.' }
                $started = $true
                $errors = $process.StandardError.ReadToEndAsync()
                $greeting = $process.StandardOutput.ReadLineAsync()
                if (-not $greeting.Wait(10000) -or $greeting.Result -notmatch '"QMP"') { throw 'QEMU could not initialize WHPX. Check firmware virtualization and restart Windows.' }
                $result.probePassed=$true; $result.message='QEMU successfully initialized WHPX.'
            } finally {
                if ($started -and -not $process.HasExited) { $process.Kill(); $process.WaitForExit(5000) | Out-Null }
                $process.Dispose()
            }
        }
    }
} catch { $result.status='Error'; $result.message=$_.Exception.Message; $code=20 }
$json = $result | ConvertTo-Json -Compress
if ($ResultPath) {
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($ResultPath), $json)
    [IO.File]::WriteAllText([IO.Path]::GetFullPath($ResultPath)+'.txt', $result.message, [Text.UTF8Encoding]::new($true))
}
Write-Output $json
exit $code
