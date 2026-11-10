param(
    [string]$MSBuild = 'C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe'
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$packageVersion = '10.0.28000.2526'
$driverProject = Join-Path $repo 'AmtPtpDeviceUsbUm\MagicTrackpad2PtpDevice.vcxproj'
$bluetoothProject = Join-Path $repo 'AmtPtpHidFilter\AmtPtpHidFilter.vcxproj'
$panelProject = Join-Path $repo 'AmtPtpControlPanel\AmtPtpControlPanel.csproj'
$packageDirectory = Join-Path $repo 'build\first-click-usb'
$bluetoothDirectory = Join-Path $repo 'build\first-click-bluetooth'
$panelDirectory = Join-Path $repo 'build\first-click-panel'
foreach ($directory in @($packageDirectory, $bluetoothDirectory, $panelDirectory)) {
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
}

& $MSBuild $driverProject /t:Build /p:Configuration=Release /p:Platform=x64 /p:SignMode=Off /p:DriverSign=false /p:WindowsTargetPlatformVersion=10.0.28000.0 /verbosity:minimal
if ($LASTEXITCODE -ne 0) { throw 'UMDF driver build failed.' }
& $MSBuild $bluetoothProject /t:Build /p:Configuration=Release /p:Platform=x64 /p:SignMode=Off /p:DriverSign=false /p:WindowsTargetPlatformVersion=10.0.28000.0 /verbosity:minimal
if ($LASTEXITCODE -ne 0) { throw 'Bluetooth driver build failed.' }
& $MSBuild $panelProject /t:Build /p:Configuration=Release /p:Platform=AnyCPU "/p:OutDir=$panelDirectory\" /verbosity:minimal
if ($LASTEXITCODE -ne 0) { throw 'Control panel build failed.' }

$driver = Join-Path $repo 'AmtPtpDeviceUsbUm\build\AmtPtpDeviceUsbUm\x64\Release\AmtPtpDeviceUsbUm.dll'
Copy-Item -LiteralPath $driver -Destination $packageDirectory
Copy-Item -LiteralPath (Join-Path $repo 'packaging\FirstClickUsb.inf') -Destination $packageDirectory
Copy-Item -LiteralPath (Join-Path $repo 'AmtPtpHidFilter\build\AmtPtpHidFilter\x64\Release\AmtPtpHidFilter.sys') -Destination $bluetoothDirectory
Copy-Item -LiteralPath (Join-Path $repo 'packaging\FirstClickBluetooth.inf') -Destination $bluetoothDirectory
$inf2cat = Join-Path $repo "packages\Microsoft.Windows.WDK.x64.$packageVersion\c\bin\10.0.28000.0\x86\Inf2Cat.exe"
foreach ($directory in @($packageDirectory, $bluetoothDirectory)) {
    & $inf2cat "/driver:$directory" /os:10_X64 /uselocaltime
    if ($LASTEXITCODE -ne 0) { throw "Driver catalog generation failed: $directory" }
}
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $packageDirectory 'AmtPtpDeviceUsbUm.dll'),(Join-Path $packageDirectory 'FirstClickUsb.inf'),(Join-Path $packageDirectory 'FirstClickUsb.cat'),(Join-Path $bluetoothDirectory 'AmtPtpHidFilter.sys'),(Join-Path $bluetoothDirectory 'FirstClickBluetooth.inf'),(Join-Path $bluetoothDirectory 'FirstClickBluetooth.cat'),(Join-Path $panelDirectory 'AmtPtpControlPanel.exe')
