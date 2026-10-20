<#
.SYNOPSIS
    Checks if the system supports WHPX (Windows Hypervisor Platform)
    for running DroidBox's Android VM.

.DESCRIPTION
    Performs three checks:
    1. CPU virtualization extensions (VT-x / AMD-V) enabled in BIOS
    2. Windows Hypervisor Platform feature installed and enabled
    3. No conflicting hypervisors (VirtualBox, etc.)

    Outputs human-readable results and exits with code 0 (pass) or 1 (fail).

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File check-whpx.ps1
#>

$ErrorActionPreference = "Stop"
$allPassed = $true

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  DroidBox WHPX Compatibility Check" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# ─── Check 1: CPU Virtualization Extensions ───────────────────────
Write-Host "[Check 1] CPU Virtualization (VT-x / AMD-V)..." -NoNewline

try {
    $processor = Get-CimInstance -ClassName Win32_Processor -ErrorAction Stop
    $virtEnabled = $processor | Select-Object -ExpandProperty VirtualizationFirmwareEnabled -ErrorAction SilentlyContinue

    if ($virtEnabled -eq $true) {
        Write-Host " PASS" -ForegroundColor Green
        Write-Host "         CPU virtualization is enabled in BIOS." -ForegroundColor Gray
    }
    elseif ($virtEnabled -eq $false) {
        Write-Host " UNKNOWN" -ForegroundColor Yellow
        Write-Host "         Windows did not report firmware virtualization." -ForegroundColor Yellow
        Write-Host "         The QEMU WHPX startup probe will verify actual availability." -ForegroundColor Gray
    }
    else {
        Write-Host " UNKNOWN" -ForegroundColor Yellow
        Write-Host "         Could not determine virtualization status." -ForegroundColor Yellow
        Write-Host "         (This is normal on some AMD systems. Try running DroidBox anyway.)" -ForegroundColor Yellow
    }
}
catch {
    Write-Host " ERROR" -ForegroundColor Yellow
    Write-Host "         Could not query CPU info: $($_.Exception.Message)" -ForegroundColor Yellow
}

# ─── Check 2: Windows Hypervisor Platform Feature ─────────────────
Write-Host ""
Write-Host "[Check 2] Windows Hypervisor Platform feature..." -NoNewline

try {
    $feature = Get-WindowsOptionalFeature -Online -FeatureName "HypervisorPlatform" -ErrorAction Stop

    if ($feature.State -eq "Enabled") {
        Write-Host " PASS" -ForegroundColor Green
        Write-Host "         Windows Hypervisor Platform is enabled." -ForegroundColor Gray
    }
    else {
        Write-Host " FAIL" -ForegroundColor Red
        Write-Host ""
        Write-Host "  Windows Hypervisor Platform is NOT enabled." -ForegroundColor Red
        Write-Host ""
        Write-Host "  HOW TO FIX (run as Administrator):" -ForegroundColor Yellow
        Write-Host '  Enable-WindowsOptionalFeature -Online -FeatureName HypervisorPlatform' -ForegroundColor Yellow
        Write-Host ""
        Write-Host "  Or via GUI:" -ForegroundColor Yellow
        Write-Host "  1. Open 'Turn Windows features on or off'" -ForegroundColor Yellow
        Write-Host "  2. Check 'Windows Hypervisor Platform'" -ForegroundColor Yellow
        Write-Host "  3. Click OK and reboot" -ForegroundColor Yellow
        Write-Host ""
        $allPassed = $false
    }
}
catch {
    Write-Host " UNKNOWN" -ForegroundColor Yellow
    Write-Host "         Could not check feature status: $($_.Exception.Message)" -ForegroundColor Yellow
    Write-Host "         You may need to run this as Administrator." -ForegroundColor Yellow
}

# ─── Check 3: Conflicting Software ───────────────────────────────
Write-Host ""
Write-Host "[Check 3] Conflicting hypervisors..." -NoNewline

$conflicts = @()

# Check for VirtualBox running
$vboxProcesses = Get-Process -Name "VBoxSVC","VirtualBoxVM" -ErrorAction SilentlyContinue
if ($vboxProcesses) {
    $conflicts += "VirtualBox is currently running"
}

# Check for VMware running
$vmwareProcesses = Get-Process -Name "vmware","vmware-vmx" -ErrorAction SilentlyContinue
if ($vmwareProcesses) {
    $conflicts += "VMware is currently running"
}

if ($conflicts.Count -eq 0) {
    Write-Host " PASS" -ForegroundColor Green
    Write-Host "         No conflicting hypervisors detected." -ForegroundColor Gray
}
else {
    Write-Host " WARNING" -ForegroundColor Yellow
    foreach ($conflict in $conflicts) {
        Write-Host "         $conflict" -ForegroundColor Yellow
    }
    Write-Host "         These may interfere with WHPX. Close them before running DroidBox." -ForegroundColor Yellow
}

# Verify that QEMU can initialize its explicit WHPX accelerator.
Write-Host ""
Write-Host "[Check 4] QEMU WHPX startup..." -NoNewline

$qemuPath = Join-Path $PSScriptRoot "..\qemu\qemu-system-x86_64.exe"
if (-not (Test-Path -LiteralPath $qemuPath -PathType Leaf)) {
    $qemuCommand = Get-Command "qemu-system-x86_64.exe" -ErrorAction SilentlyContinue
    if ($qemuCommand) {
        $qemuPath = $qemuCommand.Source
    }
}

if (-not (Test-Path -LiteralPath $qemuPath -PathType Leaf)) {
    Write-Host " FAIL" -ForegroundColor Red
    Write-Host "         qemu-system-x86_64.exe was not found." -ForegroundColor Red
    $allPassed = $false
}
else {
    $probe = $null
    $stderrTask = $null
    try {
        $startInfo = New-Object System.Diagnostics.ProcessStartInfo
        $startInfo.FileName = $qemuPath
        $startInfo.WorkingDirectory = Split-Path -Parent $qemuPath
        $startInfo.Arguments = "-accel whpx,kernel-irqchip=off -m 512 -smp 1 -cpu qemu64 -display none -S -qmp stdio"
        $startInfo.UseShellExecute = $false
        $startInfo.CreateNoWindow = $true
        $startInfo.RedirectStandardOutput = $true
        $startInfo.RedirectStandardError = $true
        $startInfo.RedirectStandardInput = $true

        $probe = New-Object System.Diagnostics.Process
        $probe.StartInfo = $startInfo
        if (-not $probe.Start()) {
            throw "QEMU could not be started."
        }

        $stderrTask = $probe.StandardError.ReadToEndAsync()
        $greetingTask = $probe.StandardOutput.ReadLineAsync()
        if (-not $greetingTask.Wait(10000)) {
            throw "QEMU did not produce a QMP greeting within 10 seconds."
        }

        $greeting = $greetingTask.Result
        if ($greeting -notmatch '"QMP"') {
            throw "QEMU started but did not produce a QMP greeting."
        }

        if ($probe.HasExited -and $probe.ExitCode -ne 0) {
            throw "QEMU exited with code $($probe.ExitCode)."
        }

        Write-Host " PASS" -ForegroundColor Green
        Write-Host "         QEMU initialized WHPX and returned a QMP greeting." -ForegroundColor Gray
    }
    catch {
        Write-Host " FAIL" -ForegroundColor Red
        Write-Host "         $($_.Exception.Message)" -ForegroundColor Red
        $allPassed = $false
    }
    finally {
        if ($probe) {
            if (-not $probe.HasExited) {
                $probe.Kill()
                $probe.WaitForExit(5000) | Out-Null
            }
            $probe.Dispose()
        }
    }
}

# ─── Summary ──────────────────────────────────────────────────────
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
if ($allPassed) {
    Write-Host "  RESULT: ALL CHECKS PASSED" -ForegroundColor Green
    Write-Host "  DroidBox should work on this system." -ForegroundColor Green
    Write-Host "========================================" -ForegroundColor Cyan
    exit 0
}
else {
    Write-Host "  RESULT: ISSUES FOUND" -ForegroundColor Red
    Write-Host "  Please fix the issues above before" -ForegroundColor Red
    Write-Host "  running DroidBox." -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Cyan
    exit 1
}
