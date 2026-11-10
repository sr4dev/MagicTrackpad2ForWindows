# Build the first-click drivers

This branch adds adjustable first-click pressure to upstream commit `e558ba2255e362143516fdbee63c82bfe835789f`.
It keeps the source version used for the hardware validation. Later upstream haptic-control changes have not been merged into this branch.

## Requirements

- Windows 11 x64.
- Visual Studio 2026 with the C++ build tools and Windows Driver Kit components.
- .NET Framework 4.7.2 targeting pack for the Windows Forms control panel.
- NuGet CLI on the command path.

## Build

Restore the SDK and WDK packages, then run the build script from the repository root.

```powershell
nuget.exe restore AmtPtpDeviceUsbUm\packages.config -PackagesDirectory packages
nuget.exe restore AmtPtpHidFilter\packages.config -PackagesDirectory packages
.\tools\Build-FirstClick.ps1
```

The script uses 64-bit MSBuild. The default path is for Visual Studio 2026 Professional.
For a different installation, pass its 64-bit executable with `-MSBuild`.

```powershell
.\tools\Build-FirstClick.ps1 -MSBuild 'C:\Path\To\MSBuild\Current\Bin\amd64\MSBuild.exe'
```

The output directories are `build\first-click-usb` for the USB package, `build\first-click-bluetooth` for the Bluetooth package, and `build\first-click-panel` for the shared control panel.
The Bluetooth binary is `AmtPtpHidFilter.sys`; the USB binary is `AmtPtpDeviceUsbUm.dll`.
The script builds and generates a catalog. It does not sign or install the package.

## Signing and installation

The modified binaries and catalogs need their own valid signatures. The upstream Microsoft signature does not cover modified binaries.
Use your Windows driver signing process before installing the generated package.
Keep a copy of the original signed driver package for recovery.

The USB INF targets the Lightning Magic Trackpad 2, `USB\VID_05AC&PID_0265&MI_01`, on Windows 11 x64.
It does not replace the standard HID driver for the separate haptic interface.

The Bluetooth INF targets the original Lightning and USB-C Magic Trackpad 2 models, PIDs `0265` and `0324`.
The Bluetooth component is a kernel driver. Local certificate trust alone does not make a newly modified kernel driver load under the current Secure Boot signing policy.
A Microsoft release signature is required for normal installation on that configuration. See [Kernel-mode code signing requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/kernel-mode-code-signing-requirements--windows-vista-and-later-).

Do not distribute a development private key or machine-specific certificate trust configuration with the source.
