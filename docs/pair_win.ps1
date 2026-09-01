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

function Get-BluetoothEndpoints {
    $output = & $PairTool /enum-endpoints /protocol Bluetooth 2>&1

    if ($LASTEXITCODE -ne 0) {
        return @()
    }

    return @($output)
}

function Find-EndpointByMac {
    param(
        [Parameter(Mandatory = $true)]
        [string]$WantedMAC
    )

    $lines = Get-BluetoothEndpoints

    foreach ($line in $lines) {

        $text = [string]$line

        # PairTool endpoint IDs begin with Bluetooth#Bluetooth.
        if ($text -notmatch '^\s*Bluetooth#Bluetooth\S*') {
            continue
        }

        $endpoint = ($text.Trim() -split '\s+')[0]

        $normalizedEndpoint =
            ($endpoint -replace '[:-]', '').ToUpperInvariant()

        if ($normalizedEndpoint.Contains($WantedMAC)) {
            return $endpoint
        }
    }

    return $null
}

# Actively discover BLE devices. PairTool /enum-endpoints only lists endpoints
# Windows has already discovered, so without this the board (advertising fine) is
# never seen. A DeviceWatcher for the BLE AssociationEndpoint protocol makes
# Windows scan; keep it running while we poll PairTool.
function Start-BleDiscovery {
    try {
        $null = [Windows.Devices.Enumeration.DeviceInformation, Windows.Devices.Enumeration, ContentType = WindowsRuntime]
        $bleAqs = 'System.Devices.Aep.ProtocolId:="{bb7bb05e-5972-42b5-94fc-76eaa7084d49}"'
        $watcher = [Windows.Devices.Enumeration.DeviceInformation]::CreateWatcher(
            $bleAqs,
            [string[]]@('System.Devices.Aep.DeviceAddress'),
            [Windows.Devices.Enumeration.DeviceInformationKind]::AssociationEndpoint)
        # A DeviceWatcher needs an Added/Updated/Removed handler registered BEFORE
        # Start(). We don't need the data — the running watcher is what makes Windows
        # actively discover BLE devices, which is then reflected in PairTool.
        $null = Register-ObjectEvent -InputObject $watcher -EventName Added   -Action { }
        $null = Register-ObjectEvent -InputObject $watcher -EventName Updated -Action { }
        $null = Register-ObjectEvent -InputObject $watcher -EventName Removed -Action { }
        $watcher.Start()
        return $watcher
    }
    catch {
        Write-Host "BLE discovery watcher unavailable ($($_.Exception.Message)); relying on PairTool alone."
        return $null
    }
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

$bleWatcher = Start-BleDiscovery

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

if ($null -ne $bleWatcher) { try { $bleWatcher.Stop() } catch { } }

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
