#include <Driver.h>
#include "FirstClick.tmh"

static NTSTATUS
PtpFilterSetHostClick(PDEVICE_CONTEXT Context, BOOLEAN Enabled)
{
    UCHAR report[] = { 0x21, (UCHAR)(Enabled ? 1 : 0) };
    return PtpFilterSendHidFeatureReport(Context->Device, 0xF2, report, sizeof(report));
}

static NTSTATUS
PtpFilterRestoreNativeClick(PDEVICE_CONTEXT Context)
{
    if (!Context->HostClick) return STATUS_SUCCESS;
    NTSTATUS status = PtpFilterSetHostClick(Context, FALSE);
    if (NT_SUCCESS(status)) Context->HostClick = FALSE;
    return status;
}

static NTSTATUS
PtpFilterPlayClick(PDEVICE_CONTEXT Context, ULONG Feedback)
{
    UCHAR report[] = { 0x53, 0x01, 0, 0x78, 0x02, 0, 0x24,
        0x30, 0x06, 0x01, 0, 0x18, 0x48, 0x12 };
    report[2] = (UCHAR)Feedback;
    report[5] = (UCHAR)min((Feedback >> 8) & 0xFF, 0x20UL);
    report[10] = (UCHAR)min((Feedback >> 16) & 0xFF, 0x40UL);
    return PtpFilterSendHidOutputReport(Context->Device, 0xF2, report, sizeof(report));
}

_Use_decl_annotations_
NTSTATUS
PtpFilterFirstClickInitialize(PDEVICE_CONTEXT Context)
{
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Context->Device;
    return WdfWaitLockCreate(&attributes, &Context->FirstClickLock);
}

_Use_decl_annotations_
NTSTATUS
PtpFilterFirstClickApply(PDEVICE_CONTEXT Context)
{
    PDRIVER_CONTEXT settings = PtpFilterDriverGetContext(WdfDeviceGetDriver(Context->Device));
    ULONG pressure = settings->FirstClickPressure;
    NTSTATUS status;

    if (Context->VendorID != HID_VID_APPLE_BT) return STATUS_NOT_SUPPORTED;
    status = PtpFilterSetHostClick(Context, FALSE);
    if (NT_SUCCESS(status)) Context->HostClick = FALSE;
    Context->FirstClickPhase = FirstClickNative;
    Context->FirstClickPressure = (UCHAR)(pressure >= 2 && pressure <= 255 ? pressure : 0);
    Context->FirstClickButtonDisabled = settings->ButtonDisabled;
    Context->FirstClickFeedback = settings->FeedbackClick;
    Context->FirstClickReleaseFeedback = settings->FeedbackRelease;
    Context->PrevIsButtonClicked = 0;
    Context->PrevPtpReportAux1.Id = (UINT32)-1;
    Context->PrevPtpReportAux2.Id = (UINT32)-1;
    Context->PrevPtpReportAux1.TipSwitch = 0;
    Context->PrevPtpReportAux2.TipSwitch = 0;
    if (NT_SUCCESS(status)) status = PtpFilterSetHapticFeedback(Context->Device,
        Context->FirstClickFeedback, Context->FirstClickReleaseFeedback);
    if (NT_SUCCESS(status) && Context->FirstClickPressure && !Context->FirstClickButtonDisabled) {
        Context->HostClick = TRUE;
        status = PtpFilterSetHostClick(Context, TRUE);
        if (NT_SUCCESS(status)) Context->FirstClickPhase = FirstClickWaitingForLift;
    }
    if (!NT_SUCCESS(status)) {
        NTSTATUS restore = PtpFilterRestoreNativeClick(Context);
        if (!NT_SUCCESS(restore)) status = restore;
        Context->FirstClickPhase = FirstClickFault;
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_DEVICE, "%!FUNC! Click pressure unavailable: %!STATUS!", status);
    }
    return status;
}

_Use_decl_annotations_
VOID
PtpFilterFirstClickStop(PDEVICE_CONTEXT Context)
{
    PtpFilterRestoreNativeClick(Context);
    Context->FirstClickPhase = FirstClickNative;
}

_Use_decl_annotations_
UCHAR
PtpFilterFirstClickUpdate(PDEVICE_CONTEXT Context, UCHAR Pressure,
    BOOLEAN HasContact, UCHAR NativeButton)
{
    UCHAR clicked = NativeButton;
    if (Context->HostClick && Context->FirstClickPhase != FirstClickFault) {
        FIRST_CLICK_PHASE next = FirstClickNextPhase(Context->FirstClickPhase,
            Context->FirstClickPressure, Pressure, HasContact);
        BOOLEAN wasPressed = Context->FirstClickPhase == FirstClickPressed;
        BOOLEAN isPressed = next == FirstClickPressed;
        NTSTATUS status = wasPressed == isPressed ? STATUS_SUCCESS : PtpFilterPlayClick(Context,
            isPressed ? Context->FirstClickFeedback : Context->FirstClickReleaseFeedback);
        if (NT_SUCCESS(status)) {
            Context->FirstClickPhase = next;
            clicked = (UCHAR)isPressed;
        }
        else {
            NTSTATUS restore = PtpFilterRestoreNativeClick(Context);
            Context->FirstClickPhase = FirstClickFault;
            TraceEvents(TRACE_LEVEL_WARNING, TRACE_INPUT, "%!FUNC! Haptic output failed: %!STATUS!, restore: %!STATUS!", status, restore);
        }
    }
    if (Context->HostClick && Context->FirstClickPhase == FirstClickFault) clicked = 0;
    if (Context->FirstClickButtonDisabled) clicked = 0;
    return clicked;
}
