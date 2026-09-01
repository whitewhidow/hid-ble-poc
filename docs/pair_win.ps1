param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$MAC,

    [int]$TimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'

# ============================================================
# Configuration
# ============================================================

$TargetMAC = ($MAC -replace '[:-]', '').ToUpperInvariant()

if ($TargetMAC -notmatch '^[0-9A-F]{12}$') {
    throw "Invalid Bluetooth MAC address: $MAC"
}

$PairTool = Join-Path $env:SystemRoot 'System32\PairTool.exe'

Write-Host "PowerShell: $($PSVersionTable.PSVersion)"
Write-Host "Edition:    $($PSVersionTable.PSEdition)"
Write-Host "Target MAC: $TargetMAC"
Write-Host ""

# ============================================================
# Check PairTool
# ============================================================

if (-not (Test-Path -LiteralPath $PairTool)) {
    throw @"
PairTool.exe was not found at:

$PairTool

This script requires the Windows 11 PairTool utility.
"@
}

Write-Host "PairTool:  $PairTool"
Write-Host ""

# ============================================================
# Helpers
# ============================================================

# Await a WinRT IAsyncOperation without System.WindowsRuntimeSystemExtensions
# (not loaded in Windows PowerShell 5.1). GetResults() throws while the op is still
# running, so just retry it, time-bounded so it can never hang.
function Await($op) {
    $deadline = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $deadline) {
        try { return $op.GetResults() }
        catch { Start-Sleep -Milliseconds 100 }
    }
    throw "WinRT async did not complete in time"
}

# Find the BLE association-endpoint for a MAC. FindAllAsync actively DISCOVERS BLE
# devices (unlike PairTool /enum-endpoints which only lists already-known ones, and
# unlike DeviceWatcher whose events PS 5.1 cannot subscribe to). Returns the AEP Id,
# which is the same "Bluetooth#Bluetooth..." endpoint PairTool /associate expects.
function Find-EndpointByMac {
    param([Parameter(Mandatory = $true)][string]$WantedMAC)
    try {
        $null = [Windows.Devices.Enumeration.DeviceInformation, Windows.Devices.Enumeration, ContentType = WindowsRuntime]
        $bleAqs = 'System.Devices.Aep.ProtocolId:="{bb7bb05e-5972-42b5-94fc-76eaa7084d49}"'
        $devices = Await ([Windows.Devices.Enumeration.DeviceInformation]::FindAllAsync(
                $bleAqs,
                [string[]]@('System.Devices.Aep.DeviceAddress'),
                [Windows.Devices.Enumeration.DeviceInformationKind]::AssociationEndpoint))

        foreach ($d in $devices) {
            $addr = [string]$d.Properties['System.Devices.Aep.DeviceAddress']
            if ((($addr -replace '[:-]', '').ToUpperInvariant()).Contains($WantedMAC)) {
                return $d.Id
            }
        }
    }
    catch {
        Write-Host "BLE enumeration error: $($_.Exception.Message)"
    }
    return $null
}

# ============================================================
# Remove existing association
# ============================================================

Write-Host "Checking for an existing Bluetooth association..."

$existing = Find-EndpointByMac -WantedMAC $TargetMAC

if ($null -ne $existing) {

    Write-Host "Existing endpoint found:"
    Write-Host "  $existing"
    Write-Host ""

    Write-Host "Removing existing association..."

    & $PairTool /disassociate $existing 2>&1 | ForEach-Object {
        Write-Host "  $_"
    }

    if ($LASTEXITCODE -eq 0) {
        Write-Host "Existing association removed."
    }
    else {
        Write-Host "No removable existing association, continuing..."
    }

    Write-Host ""
}

# ============================================================
# Discovery
# ============================================================

Write-Host "Scanning for $MAC ..."
Write-Host ""

$endpoint = $null

# Keep trying until the board appears (matches the Linux pair.sh behaviour).
while ($null -eq $endpoint) {

    $endpoint = Find-EndpointByMac -WantedMAC $TargetMAC

    if ($null -ne $endpoint) {
        break
    }

    Write-Host "." -NoNewline
    Start-Sleep -Seconds 2
}

Write-Host ""

Write-Host "FOUND"
Write-Host "Endpoint: $endpoint"
Write-Host ""

# ============================================================
# Pair
#
# /just-works corresponds to the Bluetooth Just Works
# ceremony used by a NoInputNoOutput device.
#
# /protection none requests no additional protection level.
# ============================================================

Write-Host "Pairing device..."
Write-Host "Method:      Just Works"
Write-Host "Protection:  None"
Write-Host ""

& $PairTool `
    /associate $endpoint `
    /protection none `
    /just-works 2>&1 | ForEach-Object {
        Write-Host $_
    }

$pairExitCode = $LASTEXITCODE

Write-Host ""

if ($pairExitCode -ne 0) {
    throw "Bluetooth pairing failed. PairTool exit code: $pairExitCode"
}

# ============================================================
# Verify
# ============================================================

Write-Host "Verifying association..."

Start-Sleep -Seconds 2

$verified = Find-EndpointByMac -WantedMAC $TargetMAC

if ($null -ne $verified) {

    Write-Host ""
    Write-Host "SUCCESS"
    Write-Host "Bluetooth device paired:"
    Write-Host "  $verified"
    Write-Host ""

    exit 0
}

Write-Host ""
Write-Host "PairTool reported success, but the endpoint could not be"
Write-Host "found during verification."
Write-Host ""

exit 1
