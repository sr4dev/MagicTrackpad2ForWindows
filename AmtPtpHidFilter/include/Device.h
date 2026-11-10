// Device.h: Device-specific struct and routines

#pragma once

#include "../../Common/FirstClick.h"

EXTERN_C_START

// {FF969022-3111-4441-8F88-875440172C2E} for the device interface
DEFINE_GUID(GUID_DEVICEINTERFACE_AmtPtpHidFilter,
    0xff969022, 0x3111, 0x4441, 0x8f, 0x88, 0x87, 0x54, 0x40, 0x17, 0x2c, 0x2e);

// Definition
#define HID_XFER_PACKET_SIZE 32

#define HID_VID_APPLE_USB 0x05ac
#define HID_VID_APPLE_BT  0x004c
#define HID_PID_MAGIC_TRACKPAD_2 0x0265
#define HID_PID_MAGIC_TRACKPAD_2_USBC 0x0324

/* device-specific parameters */
typedef struct _BCM5974_PARAM {
    int snratio;		/* signal-to-noise ratio */
    int min;			/* device minimum reading */
    int max;			/* device maximum reading */
} BCM5974_PARAM, *PBCM5974_PARAM;

// for locking the pointer:
typedef struct _PTP_REPORT_AUX {
	USHORT      X, Y;
	UINT32      Id;
	UCHAR		TipSwitch;
} PTP_REPORT_AUX, * PPTP_REPORT_AUX;

// Device Context
typedef struct _DEVICE_CONTEXT
{
    PDEVICE_OBJECT  WdmDeviceObject;
    WDFDEVICE       Device;
    WDFQUEUE        HidReadQueue;

    // Device identification
    USHORT VendorID;
    USHORT ProductID;
    USHORT VersionNumber;
    BCM5974_PARAM X;
    BCM5974_PARAM Y;

    // List of buffers
    WDFLOOKASIDE HidReadBufferLookaside;

    // System HID transport
    WDFIOTARGET HidIoTarget;
    BOOLEAN     IsHidIoDetourCompleted;
    WDFTIMER    HidTransportRecoveryTimer;
    WDFWORKITEM HidTransportRecoveryWorkItem;
    // IOCTL_HID_READ_REPORT requests in flight to the HID transport. Incremented
    // right before WdfRequestSend, decremented when the send fails or the request
    // completes. Never reset: in-flight reads survive D0 transitions and decrement
    // on completion.
    volatile LONG OutstandingTransportReads;
    volatile LONG BufferedTransportReads;
    KSPIN_LOCK CompletedReadLock;
    LIST_ENTRY CompletedReads;
    WDFWORKITEM InputWorkItem;
    BOOLEAN InputWorkItemQueued;
    volatile BOOLEAN InputStopping;
    volatile LONG PowerStopping;
    WDFWAITLOCK FirstClickLock;
    FIRST_CLICK_PHASE FirstClickPhase;
    BOOLEAN HostClick;
    BOOLEAN FirstClickButtonDisabled;
    UCHAR FirstClickPressure;
    ULONG FirstClickFeedback;
    ULONG FirstClickReleaseFeedback;

    // Device State
    BOOLEAN DeviceConfigured;

    // PTP report specific
    LARGE_INTEGER   LastReportTime;
    BOOLEAN         PtpInputOn;
    BOOLEAN         PtpReportTouch;
    BOOLEAN         PtpReportButton;
	
	// for locking the pointer
	PTP_REPORT_AUX  PrevPtpReportAux1, PrevPtpReportAux2;
	UCHAR           PrevIsButtonClicked;
	
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, PtpFilterGetContext)

// Request context
typedef struct _WORKER_REQUEST_CONTEXT {
    PDEVICE_CONTEXT DeviceContext;
    WDFMEMORY RequestMemory;
    WDFREQUEST Request;
    LIST_ENTRY CompletedEntry;
    SIZE_T Length;
    NTSTATUS Status;
} WORKER_REQUEST_CONTEXT, * PWORKER_REQUEST_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WORKER_REQUEST_CONTEXT, WorkerRequestGetContext)

// Initialization routines
NTSTATUS
PtpFilterCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit,
	_In_ WDFDRIVER Driver
);

// Power Management Handlers
EVT_WDF_DEVICE_PREPARE_HARDWARE PtpFilterPrepareHardware;
EVT_WDF_DEVICE_D0_ENTRY PtpFilterDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT PtpFilterDeviceD0Exit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_INIT PtpFilterSelfManagedIoInit;
EVT_WDF_DEVICE_SELF_MANAGED_IO_RESTART PtpFilterSelfManagedIoRestart;
EVT_WDF_DEVICE_SELF_MANAGED_IO_SUSPEND PtpFilterSelfManagedIoSuspend;
EVT_WDF_DEVICE_SELF_MANAGED_IO_CLEANUP PtpFilterSelfManagedIoCleanup;

// Device Management routines
NTSTATUS
PtpFilterConfigureMultiTouch(
    _In_ WDFDEVICE Device
);

VOID
PtpFilterRecoveryTimerCallback(
    WDFTIMER Timer
);

NTSTATUS
PtpFilterSetHapticFeedback(
    _In_ WDFDEVICE Device,
    _In_ ULONG FeedbackClick,
    _In_ ULONG FeedbackRelease
);

NTSTATUS
PtpFilterGetHidInputReport(
    _In_ WDFDEVICE Device,
    _In_ UCHAR ReportId,
    _Out_ PUCHAR pReportBuffer,
    _In_ ULONG ReportBufferSize
);

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS PtpFilterFirstClickInitialize(_In_ PDEVICE_CONTEXT Context);
// The caller holds FirstClickLock for configuration and input processing.
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS PtpFilterFirstClickApply(_In_ PDEVICE_CONTEXT Context);
_IRQL_requires_(PASSIVE_LEVEL)
VOID PtpFilterFirstClickStop(_In_ PDEVICE_CONTEXT Context);
_IRQL_requires_(PASSIVE_LEVEL)
UCHAR PtpFilterFirstClickUpdate(_In_ PDEVICE_CONTEXT Context, _In_ UCHAR Pressure,
    _In_ BOOLEAN HasContact, _In_ UCHAR NativeButton);

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS PtpFilterSendHidFeatureReport(_In_ WDFDEVICE Device, _In_ UCHAR ReportId,
    _In_reads_bytes_(ReportDataSize) PUCHAR ReportData, _In_ ULONG ReportDataSize);
_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS PtpFilterSendHidOutputReport(_In_ WDFDEVICE Device, _In_ UCHAR ReportId,
    _In_reads_bytes_(ReportDataSize) PUCHAR ReportData, _In_ ULONG ReportDataSize);

EXTERN_C_END
