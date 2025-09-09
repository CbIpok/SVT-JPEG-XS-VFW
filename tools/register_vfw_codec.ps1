param(
    [string]$DllPath = "$PSScriptRoot\..\Bin\Release\SvtJpegxsVfwCodec.dll",
    [string]$CoreLibPath = "$PSScriptRoot\..\Bin\Release\SvtJpegxs.dll",
    [switch]$Uninstall,
    [switch]$AlsoWow6432
)

function Assert-Admin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    if (-not $p.IsInRole([Security.Principal.WindowsBuiltinRole]::Administrator)) {
        Write-Error "Run this script in an elevated (Administrator) PowerShell."; exit 1
    }
}

Assert-Admin

if ($Uninstall) {
    Write-Host "Unregistering SVT JPEG XS VFW (x64) from HKLM..."
    Remove-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32' -Name 'vidc.SJXS' -ErrorAction SilentlyContinue
    Remove-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers.desc' -Name 'SvtJpegxsVfwCodec.dll' -ErrorAction SilentlyContinue
    if ($AlsoWow6432) {
        Write-Host "Unregistering WOW6432Node entries..."
        Remove-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\Drivers32' -Name 'vidc.SJXS' -ErrorAction SilentlyContinue
        Remove-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\Drivers.desc' -Name 'SvtJpegxsVfwCodec.dll' -ErrorAction SilentlyContinue
    }
    Write-Host "Done."
    exit 0
}

if (-not (Test-Path $DllPath)) {
    Write-Error "DLL not found: $DllPath"; exit 1
}
$dllFull = (Resolve-Path $DllPath).Path
if (-not (Test-Path $CoreLibPath)) {
    Write-Warning "Core library not found: $CoreLibPath (will skip copying)"
} else {
    $coreFull = (Resolve-Path $CoreLibPath).Path
}

# Copy to System32 for 64-bit VFW
$sys32 = Join-Path $env:WINDIR 'System32'
$dst = Join-Path $sys32 'SvtJpegxsVfwCodec.dll'
$srcHash = (Get-FileHash $dllFull).Hash
$dstHash = ''
if (Test-Path $dst) { $dstHash = (Get-FileHash $dst).Hash }
if ($srcHash -ne $dstHash) {
    Copy-Item $dllFull $dst -Force
    Write-Host "Copied DLL to $dst"
} else {
    Write-Host "DLL in System32 already up-to-date"
}

# Copy core dependency SvtJpegxs.dll (if provided)
if ($coreFull) {
    $dstCore = Join-Path $sys32 'SvtJpegxs.dll'
    $srcCoreHash = (Get-FileHash $coreFull).Hash
    $dstCoreHash = ''
    if (Test-Path $dstCore) { $dstCoreHash = (Get-FileHash $dstCore).Hash }
    if ($srcCoreHash -ne $dstCoreHash) {
        Copy-Item $coreFull $dstCore -Force
        Write-Host "Copied core DLL to $dstCore"
    } else {
        Write-Host "Core DLL in System32 already up-to-date"
    }
}

# Register in HKLM (x64)
New-Item -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32' -Force | Out-Null
New-Item -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers.desc' -Force | Out-Null
Set-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers32' -Name 'vidc.SJXS' -Value 'SvtJpegxsVfwCodec.dll'
Set-ItemProperty -Path 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Drivers.desc' -Name 'SvtJpegxsVfwCodec.dll' -Value 'SVT JPEG XS VFW Codec (x64)'
Write-Host "Registered x64: vidc.SJXS -> SvtJpegxsVfwCodec.dll"

if ($AlsoWow6432) {
    # Optional WOW6432Node registration (requires a 32-bit build of the DLL; path same name in SysWOW64)
    New-Item -Path 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\Drivers32' -Force | Out-Null
    New-Item -Path 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\Drivers.desc' -Force | Out-Null
    Set-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\Drivers32' -Name 'vidc.SJXS' -Value 'SvtJpegxsVfwCodec.dll'
    Set-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows NT\CurrentVersion\Drivers.desc' -Name 'SvtJpegxsVfwCodec.dll' -Value 'SVT JPEG XS VFW Codec (x86)'
    Write-Host "Registered WOW6432Node: vidc.SJXS -> SvtJpegxsVfwCodec.dll (requires 32-bit DLL in SysWOW64)"
}

Write-Host "Done. Restart any VFW clients (e.g., VirtualDub)."
