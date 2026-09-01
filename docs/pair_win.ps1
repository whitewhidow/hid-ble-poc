param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$MAC
)

# PoC-KBD Windows pairing helper (pure WinRT). Uses FromBluetoothAddressAsync
# (address in, no scanning selector) instead of FindAllAsync, then pairs
# Just-Works. Run in an -MTA PowerShell. ASCII only.

$ErrorActionPreference = 'Stop'

$hex = ($MAC -replace '[:-]', '').ToUpperInvariant()
if ($hex -notmatch '^[0-9A-F]{12}$') { throw "Invalid Bluetooth MAC address: $MAC" }
$addr = [Convert]::ToUInt64($hex, 16)

Write-Host ("PowerShell {0} ({1})" -f $PSVersionTable.PSVersion, $PSVersionTable.PSEdition)
Write-Host ("Apartment : {0}" -f [System.Threading.Thread]::CurrentThread.GetApartmentState())
Write-Host ("Target    : {0}  (0x{1})" -f $hex, $addr.ToString('X12'))
Write-Host ""

Add-Type -AssemblyName System.Runtime.WindowsRuntime -ErrorAction SilentlyContinue
$null = [Windows.Devices.Bluetooth.BluetoothLEDevice, Windows.Devices.Bluetooth, ContentType = WindowsRuntime]
$null = [Windows.Devices.Enumeration.DevicePairingResult, Windows.Devices.Enumeration, ContentType = WindowsRuntime]

function Await($op, $rt) {
    $m = [System.WindowsRuntimeSystemExtensions].GetMethods() |
        Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } |
        Select-Object -First 1
    $t = $m.MakeGenericMethod($rt).Invoke($null, @($op))
    if (-not $t.Wait(15000)) { throw "WinRT operation timed out" }
    return $t.Result
}

Write-Host "Connecting to the board (keeps trying until it is in range)..."
$dev = $null
$n = 0
while ($null -eq $dev) {
    $n++
    try {
        $dev = Await ([Windows.Devices.Bluetooth.BluetoothLEDevice]::FromBluetoothAddressAsync($addr)) ([Windows.Devices.Bluetooth.BluetoothLEDevice])
    }
    catch {
        Write-Host ("  attempt {0}: {1}" -f $n, $_.Exception.Message)
    }
    if ($null -eq $dev) { Write-Host ("  not in cache yet ({0})..." -f $n); Start-Sleep -Seconds 2 }
}

Write-Host ""
Write-Host ("FOUND: {0}" -f $dev.Name)

$null = [Windows.Devices.Enumeration.DevicePairingKinds,       Windows.Devices.Enumeration, ContentType = WindowsRuntime]
$null = [Windows.Devices.Enumeration.DevicePairingProtectionLevel, Windows.Devices.Enumeration, ContentType = WindowsRuntime]

$p = $dev.DeviceInformation.Pairing
if ($p.IsPaired) { Write-Host "Already paired."; exit 0 }
if (-not $p.CanPair) { throw "Device reports it cannot be paired (CanPair = false)." }

# Just Works = Custom pairing, ConfirmOnly, auto-accept the request. Fall back to
# the basic PairAsync if the (WinRT) event handler can't be attached.
$r = $null
try {
    $custom = $p.Custom
    $handler = [Windows.Foundation.TypedEventHandler[Windows.Devices.Enumeration.DeviceInformationCustomPairing, Windows.Devices.Enumeration.DevicePairingRequestedEventArgs]] {
        param($s, $e)
        Write-Host "  pairing requested -> Accept"
        $e.Accept()
    }
    $custom.add_PairingRequested($handler)
    Write-Host "Pairing (Custom / ConfirmOnly / None)..."
    $r = Await ($custom.PairAsync([Windows.Devices.Enumeration.DevicePairingKinds]::ConfirmOnly, [Windows.Devices.Enumeration.DevicePairingProtectionLevel]::None)) ([Windows.Devices.Enumeration.DevicePairingResult])
}
catch {
    Write-Host "  custom pairing unavailable ($($_.Exception.Message))"
    Write-Host "Pairing (basic / protection None)..."
    $r = Await ($p.PairAsync([Windows.Devices.Enumeration.DevicePairingProtectionLevel]::None)) ([Windows.Devices.Enumeration.DevicePairingResult])
}

Write-Host ("Result: {0}" -f $r.Status)
if ("$($r.Status)" -eq 'Paired' -or "$($r.Status)" -eq 'AlreadyPaired') {
    Write-Host "SUCCESS"
    exit 0
}
throw "Pairing failed: $($r.Status)"
