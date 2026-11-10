# First-click pressure

The custom drivers let you change how firmly you press a Magic Trackpad 2 to trigger its first click over USB or Bluetooth.
It sends the button transition and the matching haptic feedback together.
It does not implement the second, deeper [Force Click](https://support.apple.com/en-us/102309).

## Control panel

Under **Click pressure**, select **Custom click pressure** and enter a value.
Select **Apply**, then lift all fingers once before clicking.
Lower values require less pressure. Clear the checkbox and apply to restore the device's default click behavior.

The existing Light, Medium, and Firm options set haptic strength separately.
Existing pointer-stop, button-finger, hover, and palm settings retain their behavior.
Disabling clicks takes precedence over pressure adjustment.

## Setting

The control panel writes the DWORD `FirstClickPressure` to both driver settings paths:

```text
HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\WUDF\Services\AmtPtpDeviceUsbUm\Parameters
HKLM\SYSTEM\CurrentControlSet\Services\AmtPtpHidFilter\Parameters
```

| Value | Behavior |
| --- | --- |
| `0` or missing | Use the device's default click detection. |
| `2` to `255` | Press when the highest valid finger pressure reaches this value. |
| Any other value | Use the device's default click detection. |

The values are raw sensor units, not grams or another calibrated force measurement.
The release threshold is two thirds of the press threshold, rounded down.
For example, `24` presses at `24` and releases at `16`.
There is no added hold delay. Removing all fingers releases the button immediately.
Pressure is not summed across fingers. Hovering contacts and contacts marked as palms are excluded.
After applying settings or resuming power, the driver waits until all fingers have been lifted before accepting a press.

## USB and haptic control

`AmtPtpDeviceUsbUm/FirstClick.c` manages USB settings, click state, and the output target under a dedicated wait lock.
`Common/FirstClick.h` supplies the phase transition used by both drivers: `Native`, `WaitingForLift`, `Released`, `Pressed`, and `Fault`.

Before enabling adjustment, the driver restores autonomous click detection and configures the haptic profile.
It opens the matching MI02 HID collection only when there is one candidate with usage page `0xFF00`, usage `0x0D`, and a 64-byte output report.
Multiple connected devices of the same product are rejected to avoid sending feedback to the wrong trackpad.

Feature report `{ 0x21, 1 }` on USB interface 2 enables host-click mode.
At each button transition, the driver sends report `0x53`, padded to 64 bytes, through a WDF remote I/O target.
It commits the button state only after the haptic write succeeds.
Control transfers and output writes have 30 ms timeouts.

If preparation or output fails, the driver sends `{ 0x21, 0 }` to restore autonomous clicks and uses the device's native button state.
If restoration also fails, it suppresses synthetic clicks and displays the error status.
The driver also restores autonomous clicks and closes the target when leaving the working power state.
An output-target removal marks it unavailable. The next input callback attempts restoration.

The host-click protocol is based on the [nexustar Linux driver](https://github.com/nexustar/linux-hid-magicmouse/blob/master/linux/drivers/hid/hid-magicmouse.c).

## Bluetooth

The Bluetooth filter uses the existing HID transport for feature `F2 21 01` to enable host-click mode and `F2 21 00` to restore native clicks.
It sends output report `F2 53 ...` for each press or release transition and commits the button state after the output succeeds.
Actuator strength bytes use the limits from the upstream output implementation.

Completed Bluetooth input reports are processed in order by a passive work item, because transport completion can run at an interrupt level where synchronous HID output is not allowed.
Buffered reports remain part of the read-depth accounting. Suspending or removing the device stops new work, drains queued reports, and restores native clicks.
Synchronous feature and output requests have a 500 ms timeout. A haptic transfer can delay the input reports queued behind it; Bluetooth latency needs hardware validation.

## Diagnostic response

The existing named pipe `AmtPtpControlPanelUsbUmInterface` keeps commands `0` for settings reload and `1` for battery level.
Both retain their two-byte responses. A failed settings application returns failure.

Command `2` returns the following 12-byte version 2 response.

| Bytes | Contents |
| --- | --- |
| `0` | Response status. |
| `1` | Protocol version `2`. |
| `2` to `5` | Little-endian pressure and state snapshot. |
| `6` to `7` | Reserved, zero. |
| `8` to `11` | Last NTSTATUS as a little-endian DWORD. |

| Snapshot bits | Contents |
| --- | --- |
| `0` to `7` | Highest valid finger pressure. |
| `8` | Pressure adjustment configured. |
| `9` | Button pressed. |
| `10` | Waiting for all fingers to lift. |
| `11` | Valid contact present. |
| `12` | Host haptic control ready. |
| `13` | Fault. |
| `14` | Autonomous click restoration still pending. |
| `16` to `23` | Active press threshold. |

## Validation and limits

The native driver, Windows Forms control panel, and USB catalog were built successfully.
On a Lightning Magic Trackpad 2 with PID `0265`, threshold `24` produced the expected first click and haptic feedback.
The user confirmed the desired sensitivity. Device diagnostics reported haptic readiness and no output fault during use.

The supplied packages target Windows 11 x64. USB-C, Bluetooth, sleep/resume, and multiple-device behavior have not been validated on hardware.
Bluetooth support has not been tested on hardware. The modified kernel driver has not been installed on the development PC, which retains its existing driver and security settings.
See [Build the first-click drivers](build-first-click.md) for build and signing requirements.
