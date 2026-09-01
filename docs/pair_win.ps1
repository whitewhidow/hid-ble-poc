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

$p = $dev.DeviceInformation.Pairing
if ($p.IsPaired) { Write-Host "Already paired."; exit 0 }
if (-not $p.CanPair) { throw "Device reports it cannot be paired (CanPair = false)." }

Write-Host "Pairing (Just Works)..."
$r = Await ($p.PairAsync()) ([Windows.Devices.Enumeration.DevicePairingResult])
Write-Host ("Result: {0}" -f $r.Status)

if ("$($r.Status)" -eq 'Paired' -or "$($r.Status)" -eq 'AlreadyPaired') {
    Write-Host "SUCCESS"
    exit 0
}
throw "Pairing failed: $($r.Status)"
