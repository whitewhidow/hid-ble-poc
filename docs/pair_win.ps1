param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$MAC
)

# PoC-KBD Windows pairing helper (pure WinRT, Windows PowerShell 5.1).
# Discovers the advertising BLE board and pairs it Just-Works -- no PairTool,
# no DeviceWatcher events, no PowerShell 7. Usage: pair_win.ps1 <MAC>

$ErrorActionPreference = 'Stop'

$want = ($MAC -replace '[:-]', '').ToUpperInvariant()
if ($want -notmatch '^[0-9A-F]{12}$') { throw "Invalid Bluetooth MAC address: $MAC" }

Write-Host "PowerShell: $($PSVersionTable.PSVersion) ($($PSVersionTable.PSEdition))"
Write-Host "Target MAC: $want"

# WinRT Bluetooth-LE async never completes in an STA console (a known limitation);
# it needs a multi-threaded apartment. Launch with:  powershell -MTA ...
if ([System.Threading.Thread]::CurrentThread.GetApartmentState() -eq 'STA') {
    Write-Warning "Running in STA -- WinRT BLE async will hang. Re-run with:  powershell -MTA ..."
}
Write-Host ""

# ---- WinRT plumbing --------------------------------------------------------
# AsTask (to await IAsyncOperation) lives in System.Runtime.WindowsRuntime.
Add-Type -AssemblyName System.Runtime.WindowsRuntime -ErrorAction SilentlyContinue
$null = [Windows.Devices.Enumeration.DeviceInformation, Windows.Devices.Enumeration, ContentType = WindowsRuntime]
$null = [Windows.Devices.Bluetooth.BluetoothLEDevice,   Windows.Devices.Bluetooth,   ContentType = WindowsRuntime]

function Await($op, $resultType) {
    $asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() |
        Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } |
        Select-Object -First 1
    $task = $asTask.MakeGenericMethod($resultType).Invoke($null, @($op))
    if (-not $task.Wait(20000)) { throw "WinRT operation timed out" }
    return $task.Result
}

# ---- Discover (unpaired BLE devices) ---------------------------------------
# The SDK-provided selector is a valid AQS the system enumerates quickly; a
# hand-built ProtocolId AQS tends to make FindAllAsync hang.
$selector = [Windows.Devices.Bluetooth.BluetoothLEDevice]::GetDeviceSelectorFromPairingState($false)

Write-Host "Scanning for $MAC (keeps trying until found)..."
$dev = $null
while ($null -eq $dev) {
    $found = Await ([Windows.Devices.Enumeration.DeviceInformation]::FindAllAsync($selector)) ([Windows.Devices.Enumeration.DeviceInformationCollection])
    foreach ($d in $found) {
        # BLE DeviceInformation.Id embeds the device MAC.
        if ((($d.Id -replace '[:-]', '').ToUpperInvariant()).Contains($want)) { $dev = $d; break }
    }
    if ($null -eq $dev) { Write-Host "." -NoNewline; Start-Sleep -Seconds 2 }
}

Write-Host ""
Write-Host "FOUND: $($dev.Name)"
Write-Host "  $($dev.Id)"
Write-Host ""

# ---- Pair (Just Works) -----------------------------------------------------
$pairing = $dev.Pairing

if ($pairing.IsPaired) {
    Write-Host "Already paired."
    exit 0
}
if (-not $pairing.CanPair) {
    throw "Device reports it cannot be paired (CanPair = false)."
}

Write-Host "Pairing (Just Works)..."
$result = Await ($pairing.PairAsync()) ([Windows.Devices.Enumeration.DevicePairingResult])

$status = "$($result.Status)"
Write-Host "Result: $status"
Write-Host ""

if ($status -eq 'Paired' -or $status -eq 'AlreadyPaired') {
    Write-Host "SUCCESS"
    exit 0
}

throw "Pairing failed: $status"
