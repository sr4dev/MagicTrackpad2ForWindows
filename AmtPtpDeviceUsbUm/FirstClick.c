#include <driver.h>
#include <cfgmgr32.h>
#undef USAGE
#include <hidsdi.h>
#include <stdlib.h>
#include <wchar.h>

typedef struct _ACTUATOR_CONTEXT {
    PDEVICE_CONTEXT DeviceContext;
} ACTUATOR_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(ACTUATOR_CONTEXT, ActuatorGetContext)

static EVT_WDF_IO_TARGET_QUERY_REMOVE AmtPtpActuatorQueryRemove;
static EVT_WDF_IO_TARGET_REMOVE_CANCELED AmtPtpActuatorRemoveCanceled;
static EVT_WDF_IO_TARGET_REMOVE_COMPLETE AmtPtpActuatorRemoveComplete;

static NTSTATUS
AmtPtpSetHostClick(PDEVICE_CONTEXT Context, BOOL Enabled)
{
    UCHAR report[] = { 0x21, (UCHAR)(Enabled ? 1 : 0) };
    WDF_USB_CONTROL_SETUP_PACKET setup;
    WDF_MEMORY_DESCRIPTOR memory;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG transferred = 0;
    WDF_USB_CONTROL_SETUP_PACKET_INIT(&setup, BmRequestHostToDevice,
        BmRequestToInterface, 9, 0x0321, 2);
    setup.Packet.bm.Request.Type = BmRequestClass;
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memory, report, sizeof(report));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_MS(30));
    NTSTATUS status = WdfUsbTargetDeviceSendControlTransferSynchronously(
        Context->UsbDevice, NULL, &options, &setup, &memory, &transferred);
    if (NT_SUCCESS(status) && transferred != sizeof(report)) status = STATUS_DEVICE_DATA_ERROR;
    return status;
}

static VOID
AmtPtpCloseActuator(PDEVICE_CONTEXT Context)
{
    InterlockedExchange(&Context->ActuatorAvailable, 0);
    if (Context->ActuatorTarget != NULL) {
        WdfIoTargetClose(Context->ActuatorTarget);
        WdfObjectDelete(Context->ActuatorTarget);
        Context->ActuatorTarget = NULL;
    }
}

static NTSTATUS
AmtPtpOpenActuator(PDEVICE_CONTEXT Context)
{
    GUID hidGuid;
    ULONG length = 0;
    WCHAR* paths = NULL;
    WCHAR* selected = NULL;
    ULONG matches = 0;
    WCHAR identity[40];
    NTSTATUS status = STATUS_DEVICE_NOT_CONNECTED;
    WDFDEVICE device = (WDFDEVICE)WdfObjectContextGetObject(Context);
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_TARGET_OPEN_PARAMS open;
    UNICODE_STRING name;

    HidD_GetHidGuid(&hidGuid);
    if (CM_Get_Device_Interface_List_SizeW(&length, &hidGuid, NULL,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || length < 2) return status;
    paths = (WCHAR*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, length * sizeof(WCHAR));
    if (paths == NULL) return STATUS_INSUFFICIENT_RESOURCES;
    if (CM_Get_Device_Interface_ListW(&hidGuid, NULL, paths, length,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS) goto done;
    swprintf_s(identity, ARRAYSIZE(identity), L"vid_%04x&pid_%04x&mi_02",
        Context->DeviceDescriptor.idVendor, Context->DeviceDescriptor.idProduct);
    for (WCHAR* path = paths; *path; path += wcslen(path) + 1) {
        WCHAR* lower = _wcsdup(path);
        if (lower == NULL) { status = STATUS_INSUFFICIENT_RESOURCES; goto done; }
        _wcslwr_s(lower, wcslen(lower) + 1);
        BOOL identityMatches = wcsstr(lower, identity) != NULL;
        free(lower);
        if (!identityMatches) continue;
        matches++;
        HANDLE metadata = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING, 0, NULL);
        if (metadata == INVALID_HANDLE_VALUE) continue;
        PHIDP_PREPARSED_DATA preparsed = NULL;
        HIDP_CAPS caps;
        if (HidD_GetPreparsedData(metadata, &preparsed)) {
            if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS &&
                caps.UsagePage == 0xFF00 && caps.Usage == 0x0D &&
                caps.OutputReportByteLength == 64) {
                selected = path;
            }
            HidD_FreePreparsedData(preparsed);
        }
        CloseHandle(metadata);
    }
    if (matches != 1 || selected == NULL) { status = STATUS_OBJECT_NAME_COLLISION; goto done; }
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ACTUATOR_CONTEXT);
    status = WdfIoTargetCreate(device, &attributes, &Context->ActuatorTarget);
    if (!NT_SUCCESS(status)) goto done;
    ActuatorGetContext(Context->ActuatorTarget)->DeviceContext = Context;
    name.Buffer = selected;
    name.Length = (USHORT)(wcslen(selected) * sizeof(WCHAR));
    name.MaximumLength = name.Length + sizeof(WCHAR);
    WDF_IO_TARGET_OPEN_PARAMS_INIT_OPEN_BY_NAME(&open, &name, GENERIC_WRITE);
    open.ShareAccess = FILE_SHARE_READ | FILE_SHARE_WRITE;
    open.EvtIoTargetQueryRemove = AmtPtpActuatorQueryRemove;
    open.EvtIoTargetRemoveCanceled = AmtPtpActuatorRemoveCanceled;
    open.EvtIoTargetRemoveComplete = AmtPtpActuatorRemoveComplete;
    status = WdfIoTargetOpen(Context->ActuatorTarget, &open);
    if (!NT_SUCCESS(status)) AmtPtpCloseActuator(Context);
    else InterlockedExchange(&Context->ActuatorAvailable, 1);
done:
    HeapFree(GetProcessHeap(), 0, paths);
    return status;
}

static NTSTATUS
AmtPtpActuatorQueryRemove(WDFIOTARGET Target)
{
    InterlockedExchange(&ActuatorGetContext(Target)->DeviceContext->ActuatorAvailable, 0);
    InterlockedExchange(&ActuatorGetContext(Target)->DeviceContext->FirstClickLastStatus, STATUS_DEVICE_NOT_CONNECTED);
    WdfIoTargetCloseForQueryRemove(Target);
    return STATUS_SUCCESS;
}

static VOID
AmtPtpActuatorRemoveCanceled(WDFIOTARGET Target)
{
    InterlockedExchange(&ActuatorGetContext(Target)->DeviceContext->ActuatorAvailable, 0);
    InterlockedExchange(&ActuatorGetContext(Target)->DeviceContext->FirstClickLastStatus, STATUS_DEVICE_NOT_CONNECTED);
}

static VOID
AmtPtpActuatorRemoveComplete(WDFIOTARGET Target)
{
    InterlockedExchange(&ActuatorGetContext(Target)->DeviceContext->ActuatorAvailable, 0);
    InterlockedExchange(&ActuatorGetContext(Target)->DeviceContext->FirstClickLastStatus, STATUS_DEVICE_NOT_CONNECTED);
    WdfIoTargetClose(Target);
}

static NTSTATUS
AmtPtpPlayClick(PDEVICE_CONTEXT Context, ULONG Feedback)
{
    UCHAR report[64] = { 0x53, 0x01, 0, 0x78, 0x02, 0, 0x24,
        0x30, 0x06, 0x01, 0, 0x18, 0x48, 0x12 };
    WDF_MEMORY_DESCRIPTOR memory;
    WDF_REQUEST_SEND_OPTIONS options;
    ULONG_PTR transferred = 0;
    if (!InterlockedCompareExchange(&Context->ActuatorAvailable, 0, 0))
        return STATUS_DEVICE_NOT_CONNECTED;
    report[2] = (UCHAR)Feedback;
    report[5] = (UCHAR)(Feedback >> 8);
    report[10] = (UCHAR)(Feedback >> 16);
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memory, report, sizeof(report));
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_TIMEOUT);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_MS(30));
    NTSTATUS status = WdfIoTargetSendWriteSynchronously(Context->ActuatorTarget,
        NULL, &memory, NULL, &options, &transferred);
    if (NT_SUCCESS(status) && transferred != sizeof(report)) status = STATUS_DEVICE_DATA_ERROR;
    return status;
}

static NTSTATUS
AmtPtpRestoreNativeClick(PDEVICE_CONTEXT Context)
{
    if (!Context->HostClick) return STATUS_SUCCESS;
    NTSTATUS status = AmtPtpSetHostClick(Context, FALSE);
    if (NT_SUCCESS(status)) Context->HostClick = FALSE;
    return status;
}

NTSTATUS
AmtPtpFirstClickInitialize(PDEVICE_CONTEXT Context)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = WdfObjectContextGetObject(Context);
    return WdfWaitLockCreate(&attributes, &Context->FirstClickLock);
}

static VOID
AmtPtpPublishFirstClick(PDEVICE_CONTEXT Context, UCHAR Pressure, BOOL HasContact)
{
    BOOL ready = Context->HostClick &&
        InterlockedCompareExchange(&Context->ActuatorAvailable, 0, 0) &&
        Context->FirstClickPhase != FirstClickFault;
    ULONG snapshot = Pressure |
        ((ULONG)(Context->FirstClickPressure != 0) << 8) |
        ((ULONG)(ready && Context->FirstClickPhase == FirstClickPressed) << 9) |
        ((ULONG)(ready && Context->FirstClickPhase == FirstClickWaitingForLift) << 10) |
        ((ULONG)HasContact << 11) | ((ULONG)ready << 12) |
        ((ULONG)(Context->FirstClickPhase == FirstClickFault) << 13) |
        ((ULONG)(Context->FirstClickPhase == FirstClickFault && Context->HostClick) << 14) |
        ((ULONG)Context->FirstClickPressure << 16);
    InterlockedExchange(&Context->FirstClickStatus, (LONG)snapshot);
}

NTSTATUS
AmtPtpFirstClickApply(PDEVICE_CONTEXT Context)
{
    NTSTATUS status;
    ULONG pressure = ReadSettingValue(L"FirstClickPressure", 0);
    WdfWaitLockAcquire(Context->FirstClickLock, NULL);
    if (!AmtPtpIsMagicTrackpad2(Context)) {
        WdfWaitLockRelease(Context->FirstClickLock);
        return STATUS_NOT_SUPPORTED;
    }
    // A previous host crash may have left the device in host-click mode.
    Context->HostClick = TRUE;
    status = AmtPtpRestoreNativeClick(Context);
    AmtPtpCloseActuator(Context);
    Context->FirstClickPressure = (UCHAR)(pressure >= 2 && pressure <= 255 ? pressure : 0);
    Context->FirstClickPhase = FirstClickNative;
    Context->FirstClickButtonDisabled = ReadSettingValue(L"ButtonDisabled", 0) != 0;
    Context->FirstClickFeedback = ReadSettingValue(L"FeedbackClick", 0x08081E);
    Context->FirstClickReleaseFeedback = ReadSettingValue(L"FeedbackRelease", 0x020218);
    if (NT_SUCCESS(status)) status = AmtPtpSetHapticFeedback(Context,
        Context->FirstClickFeedback, Context->FirstClickReleaseFeedback);
    if (NT_SUCCESS(status) && Context->FirstClickPressure && !Context->FirstClickButtonDisabled) {
        status = AmtPtpOpenActuator(Context);
        if (NT_SUCCESS(status)) {
            // A failed transfer may still have enabled host-click mode on the device.
            Context->HostClick = TRUE;
            status = AmtPtpSetHostClick(Context, TRUE);
        }
        if (NT_SUCCESS(status)) Context->FirstClickPhase = FirstClickWaitingForLift;
    }
    if (!NT_SUCCESS(status)) {
        NTSTATUS restoreStatus = AmtPtpRestoreNativeClick(Context);
        if (!NT_SUCCESS(restoreStatus)) status = restoreStatus;
        AmtPtpCloseActuator(Context);
        Context->FirstClickPhase = FirstClickFault;
    }
    InterlockedExchange(&Context->FirstClickLastStatus, status);
    AmtPtpPublishFirstClick(Context, 0, FALSE);
    InterlockedIncrement(&Context->SettingsGeneration);
    WdfWaitLockRelease(Context->FirstClickLock);
    return status;
}

VOID
AmtPtpFirstClickStop(PDEVICE_CONTEXT Context)
{
    WdfWaitLockAcquire(Context->FirstClickLock, NULL);
    AmtPtpRestoreNativeClick(Context);
    AmtPtpCloseActuator(Context);
    Context->FirstClickPhase = FirstClickNative;
    InterlockedExchange(&Context->FirstClickStatus, 0);
    WdfWaitLockRelease(Context->FirstClickLock);
}

UCHAR
AmtPtpFirstClickUpdate(PDEVICE_CONTEXT Context, UCHAR Pressure, BOOL HasContact,
    UCHAR NativeButton)
{
    UCHAR clicked = NativeButton;
    WdfWaitLockAcquire(Context->FirstClickLock, NULL);
    FIRST_CLICK_PHASE next = Context->FirstClickPhase;
    if (Context->HostClick && (Context->FirstClickButtonDisabled ||
        !InterlockedCompareExchange(&Context->ActuatorAvailable, 0, 0))) {
        NTSTATUS status = AmtPtpRestoreNativeClick(Context);
        InterlockedExchange(&Context->FirstClickLastStatus,
            NT_SUCCESS(status) ? STATUS_DEVICE_NOT_CONNECTED : status);
        Context->FirstClickPhase = FirstClickFault;
    }
    if (Context->HostClick && Context->FirstClickPhase != FirstClickFault) {
        next = FirstClickNextPhase(next, Context->FirstClickPressure, Pressure, HasContact);
        BOOL wasPressed = Context->FirstClickPhase == FirstClickPressed;
        BOOL isPressed = next == FirstClickPressed;
        NTSTATUS status = wasPressed == isPressed ? STATUS_SUCCESS : AmtPtpPlayClick(Context,
            isPressed ? Context->FirstClickFeedback : Context->FirstClickReleaseFeedback);
        if (!NT_SUCCESS(status)) {
            NTSTATUS restoreStatus = AmtPtpRestoreNativeClick(Context);
            InterlockedExchange(&Context->FirstClickLastStatus, NT_SUCCESS(restoreStatus) ? status : restoreStatus);
            Context->FirstClickPhase = FirstClickFault;
        }
        else {
            Context->FirstClickPhase = next;
            clicked = (UCHAR)isPressed;
        }
    }
    if (Context->HostClick && Context->FirstClickPhase == FirstClickFault) clicked = 0;
    if (Context->FirstClickButtonDisabled) clicked = 0;
    AmtPtpPublishFirstClick(Context, Pressure, HasContact);
    WdfWaitLockRelease(Context->FirstClickLock);
    return clicked;
}
