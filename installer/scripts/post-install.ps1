[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$InstallRoot)
$ErrorActionPreference='Stop'
try {
    $identity=[Security.Principal.WindowsIdentity]::GetCurrent()
    if ($identity.IsSystem) { throw 'Launch Sabi once as the intended Windows user; SYSTEM cannot initialize personal instance data.' }
    $executable=Join-Path ([IO.Path]::GetFullPath($InstallRoot)) 'Sabi.exe'
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw 'Installed Sabi.exe is missing.' }
    # Inno invokes this script with ExecAsOriginalUser. The host uses its actual
    # QStandardPaths location, shared locking, port allocation, and atomic writes.
    $process=Start-Process -FilePath $executable -ArgumentList '--initialize-only' -WorkingDirectory (Split-Path -Parent $executable) -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw 'Default instance initialization failed. Check the Sabi log in LocalAppData and free disk space.' }
    exit 0
} catch { Write-Error $_.Exception.Message -ErrorAction Continue; exit 1 }
