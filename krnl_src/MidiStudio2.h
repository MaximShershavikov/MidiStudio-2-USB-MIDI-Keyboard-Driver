/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.
**********************************************************************************/

#ifndef _MIDI_STUDIO_02_
#define _MIDI_STUDIO_02_

#include "ntddk.h"
#include <minwindef.h>
#include <string.h>
#include "usb.h"
#include "usbdlib.h"
#include "wmilib.h"
#include "wdm.h"
#include "usbioctl.h"
#include "wmistr.h"
#include <InitGuid.h>

#define REMOTE_WAKEUP_MASK 0x20
#define INTUSB_IOCTL_INDEX 0x0000
#define MIDISTUDIO_02_TAG (ULONG) 'MSo2'

#define IOCTL_INTUSB_GET_CONFIG_DESCRIPTOR CTL_CODE(FILE_DEVICE_UNKNOWN, INTUSB_IOCTL_INDEX, METHOD_BUFFERED, FILE_ANY_ACCESS)     
#define IOCTL_INTUSB_RESET_DEVICE CTL_CODE(FILE_DEVICE_UNKNOWN, INTUSB_IOCTL_INDEX + 1, METHOD_BUFFERED, FILE_ANY_ACCESS) 
#define IOCTL_INTUSB_RESET_PIPE CTL_CODE(FILE_DEVICE_UNKNOWN, INTUSB_IOCTL_INDEX + 2, METHOD_BUFFERED, FILE_ANY_ACCESS)

DEFINE_GUID(GUID_INTERFACE_MIDI, 0x00873FDF, 0x61A8, 0x11D1, 0xAA, 0x5E, 0x00, 0xC0, 0x4F, 0xB1, 0x72, 0x8B);
DEFINE_GUID(MIDI_USB_WMI_STD_DATA_GUID, 0xBBA21300, 0x6DD3, 0x11d2, 0xB8, 0x44, 0x00, 0xC0, 0x4F, 0xAD, 0x51, 0x71);

typedef enum _DEVSTATE 
{
    NotStarted,
    Stopped,
    Working,
    PendingStop,
    PendingRemove,
    SurpriseRemoved,
    Removed
} DEVSTATE;

typedef enum _QUEUE_STATE 
{
    HoldRequests,
    AllowRequests,
    FailRequests
} QUEUE_STATE;

typedef struct _INTERFACE_INFO
{
    USBD_INTERFACE_INFORMATION UsbdInterfaseInfo;
    USBD_PIPE_INFORMATION PipeInfoNext[3];
}INTERFACE_INFO, *PINTERFACE_INFO;

typedef struct _CONFIG_DEVICE
{
    USB_CONFIGURATION_DESCRIPTOR UsbConfigurationDesc;
    USB_INTERFACE_DESCRIPTOR UsbInterfaceDesc;
    USB_ENDPOINT_DESCRIPTOR Pipe01;
    USB_ENDPOINT_DESCRIPTOR Pipe02;
    USB_ENDPOINT_DESCRIPTOR Pipe03;
    USB_ENDPOINT_DESCRIPTOR Pipe04;
}CONFIG_DEVICE, *PCONFIG_DEVICE;

typedef struct _MIDI_USB_PIPE_CONTEXT
{
    BOOLEAN PipeOpen;
}MIDI_USB_PIPE_CONTEXT, *PMIDI_USB_PIPE_CONTEXT;

typedef struct _WORKER_THREAD_CONTEXT 
{
    PDEVICE_OBJECT DeviceObject;
    PIRP Irp;
    PIO_WORKITEM WorkItem;
} WORKER_THREAD_CONTEXT, *PWORKER_THREAD_CONTEXT;

typedef struct _POWER_COMPLETION_CONTEXT 
{
    PDEVICE_OBJECT DeviceObject;
    PIRP Irp;
} POWER_COMPLETION_CONTEXT, *PPOWER_COMPLETION_CONTEXT;

typedef struct _MIDI_DEVICE
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT NextLowerDeviceObject;
    PDEVICE_OBJECT PhysicalDeviceObject;
    UNICODE_STRING UnicodeString;
    DEVICE_CAPABILITIES DeviceCapabilities;
    PUSB_DEVICE_DESCRIPTOR UsbDeviseDescriptor;
    PUSB_CONFIGURATION_DESCRIPTOR UsbConfigDescriptorWithPipes;
    PCONFIG_DEVICE ConfigDevicePtr;
    PUSBD_INTERFACE_INFORMATION UsbdInterfaceInformation;
    PINTERFACE_INFO PtrInterfaceInfo;
    PMIDI_USB_PIPE_CONTEXT PipeContext;
    DEVSTATE DeviceState;
    DEVSTATE PrevDevState;
    KSPIN_LOCK DeviceStateSpinLock;
    SYSTEM_POWER_STATE SystemPower;
    DEVICE_POWER_STATE DevicePower;
    QUEUE_STATE QueueState;
    LIST_ENTRY ListOfNewRequestsQueue;
    KSPIN_LOCK IoQueueSpinLock;
    KEVENT RemoveEvent;
    KEVENT StopEvent;
    ULONG OutStandingIO;
    KSPIN_LOCK IoCountSpinLock;
    LONG SSEnable;
    LONG SSRegistryEnable;
    PUSB_IDLE_CALLBACK_INFO UsbIdleCallbackInfo;
    PIRP PendingIdleIrp;
    LONG IdleReqPend;
    LONG FreeIdleIrpCount;
    KSPIN_LOCK IdleReqSateSpinLock;
    KEVENT NoIdleRequestPendingEvent;
    ULONG PowerDownLevel;
    PIRP WaitWakeIrp;
    LONG FlagWWCancel;
    LONG FlagWWOutstanding;
    LONG WaitWakeEnable;
    LONG OpenHandleCount;
    KTIMER Ktimer;
    KDPC Dpc;
    KEVENT NoDpcWorkItemPendingEvent;
    WMILIB_CONTEXT WmiLibContext;
    BOOLEAN WdmVerWinXpOrBetter;
} MIDI_DEVICE, *PMIDI_DEVICE;

typedef struct _MIDIUSB_RW_CONTEXT 
{
    PURB Urb;
    PMDL Mdl;
    ULONG Length;
    ULONG Numxfer;
    ULONG_PTR VirtualAddress;
    PMIDI_DEVICE MidiDevice;
} MIDIUSB_RW_CONTEXT, *PMIDIUSB_RW_CONTEXT;

typedef struct _IRP_COMPLETION_CONTEXT 
{
    PMIDI_DEVICE MidiDevice;
    PKEVENT Event;
} IRP_COMPLETION_CONTEXT, *PIRP_COMPLETION_CONTEXT;

PUNICODE_STRING MidiRegistryPath;

// MainMidiDrv.c
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath);
VOID UnloadDrvMidiBulkDevice(PDRIVER_OBJECT DriverObject);
NTSTATUS AddMidiBulkDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject);
NTSTATUS GetValueFromRegistry(PWCHAR RegPath, PWCHAR ValueName, PULONG Value); 

// WmiControl.c
NTSTATUS WmiSystemControlDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp); 
NTSTATUS WmiLibContextInit(PMIDI_DEVICE MidiDevice); 
NTSTATUS QueryWmiRegInfo(PDEVICE_OBJECT DeviceObject, PULONG RegFlags, PUNICODE_STRING InstanceName, PUNICODE_STRING* RegistryPath, PUNICODE_STRING MofResourceName, PDEVICE_OBJECT* Pdo);
NTSTATUS QueryWmiDataBlock(PDEVICE_OBJECT DeviceObject, PIRP Irp, ULONG GuidIndex, ULONG InstanceIndex, ULONG InstanceCount, PULONG InstanceLengthArray, ULONG BufferAvail, PUCHAR Buffer);
NTSTATUS SetWmiDataBlock(PDEVICE_OBJECT DeviceObject, PIRP Irp, ULONG GuidIndex, ULONG InstanceIndex, ULONG BufferSize, PUCHAR Buffer);
NTSTATUS SetWmiDataItem(PDEVICE_OBJECT DeviceObject, PIRP Irp, ULONG GuidIndex, ULONG InstanceIndex, ULONG DataItemId, ULONG BufferSize, PUCHAR Buffer);
NTSTATUS IoWmiDeRegCtrl(PMIDI_DEVICE MidiDevice);

// MidiMessageRead.c
NTSTATUS WaitingIoDataAndCompletion(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);
NTSTATUS ReadMidiData(PDEVICE_OBJECT DeviceObject, PIRP Irp);

// FileCreateClose.c
NTSTATUS CreateFileOfPipe(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS CloseFileOfPipe(PDEVICE_OBJECT DeviceObject, PIRP Irp);
PMIDI_USB_PIPE_CONTEXT GetPipeNumFromString(PDEVICE_OBJECT DeviceObject, PUNICODE_STRING FileName);

// PnpControl.c
NTSTATUS MidiBulkPnpControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS StopMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp); 
NTSTATUS StartMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS QueryRemoveMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp); 
NTSTATUS RemoveMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS CanselRemoveMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp); 
NTSTATUS CancelStopMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS QueryCapabilitiesMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS SurpriseRemovalMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS QueryStopMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS FreeMemory(PDEVICE_OBJECT DeviceObject);

// DeviceControlCode.c
NTSTATUS MidiBulkDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS MidiBulkResetDevice(PDEVICE_OBJECT DeviceObject, PULONG PortStatus);
NTSTATUS MidiBulkResetPipe(PDEVICE_OBJECT DeviceObject, PUSBD_PIPE_INFORMATION UsbdPipeInformation);

// ListProcess.c
VOID RemoveEntriesAndProcessListQueue(PMIDI_DEVICE MidiDevice); 
NTSTATUS CleanListEntryIrps(PDEVICE_OBJECT DeviceObject, PIRP Irp); 

// SetupInterfase.c
NTSTATUS ConfigurationDescriptorWithPipeRequest(PDEVICE_OBJECT DeviceObject);
NTSTATUS DeviceDescriptorRequest(PDEVICE_OBJECT DeviceObject);
NTSTATUS SelectInterfecesOfMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor);
NTSTATUS CansellPipesOfMidiBulkDevice(PDEVICE_OBJECT DeviceObject);
NTSTATUS DeconfigureMidiBulkDevice(PDEVICE_OBJECT DeviceObject);

// UsbRequests.c
NTSTATUS UsbSubmitUrbRequest(PDEVICE_OBJECT DeviceObject, PURB Urb);
NTSTATUS UsbGetPortStatusRequest(PDEVICE_OBJECT DeviceObject, PULONG PortStatus);
NTSTATUS UsbResetPortRequest(PDEVICE_OBJECT DeviceObject);

// EventLock.c
LONG UsbIoDecrement(PMIDI_DEVICE MidiDevice);
LONG UsbIoIncrement(PMIDI_DEVICE MidiDevice);
VOID UsbIoCancelSelectSuspend(PMIDI_DEVICE MidiDevice);
NTSTATUS UsbIrpCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);

// PowerDevice.c
NTSTATUS MidiBulkPower(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS IrpWaitWakeCompletionRoutineMidiUsb(PDEVICE_OBJECT DeviceObject, PIRP Irp, PMIDI_DEVICE MidiDevice);
NTSTATUS IrpDeviceQueryPowerMidiUsb(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS IrpDeviceSetPowerMidiUsb(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS IrpSystemQueryPowerMidiUsb(PDEVICE_OBJECT DeviceObject, PIRP Irp); 
NTSTATUS IrpSystemSetPowerMidiUsb(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS IrpSystemPowerCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PMIDI_DEVICE MidiDevice);
NTSTATUS IrpIssueWaitWake(PMIDI_DEVICE MidiDevice);
VOID IrpCancelWaitWake(PMIDI_DEVICE MidiDevice);
NTSTATUS UsbIoHoldRequests(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS FinishUsbDevicePoUpIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp, PMIDI_DEVICE MidiDevice);
NTSTATUS SetupUsbDeviceFunctional(PDEVICE_OBJECT DeviceObject, PIRP Irp, PMIDI_DEVICE MidiDevice);
VOID HoldUsbDevIoRequestsWorkerRoutine(PDEVICE_OBJECT DeviceObject, PVOID Context);
VOID WaitWakeIrpCallback(PDEVICE_OBJECT DeviceObject, UCHAR MinorFunction, POWER_STATE PowerState, PVOID Context, PIO_STATUS_BLOCK IoStatus);
VOID SendUsbDeviceIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS FinishUsbDevicePoDnIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp, PMIDI_DEVICE MidiDevice);
VOID UsbDevicePoCompletionRoutine(PDEVICE_OBJECT DeviceObject, UCHAR MinorFunction, POWER_STATE PowerState, PVOID Context, PIO_STATUS_BLOCK IoStatus); 

// DeferredRoutine.c
VOID DeferredRoutineDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);
VOID IdleUsbRequestWorkerRoutine(PDEVICE_OBJECT DeviceObject, PVOID Context); 
BOOLEAN CanUsbDeviceSuspend(PMIDI_DEVICE MidiDevice);
NTSTATUS IdleRequestIrpSubmit(PMIDI_DEVICE MidiDevice);
VOID IdleNotificationCallback(PMIDI_DEVICE MidiDevice);
VOID PoIrpAsyncOrWWIrpCompletionFunction(PDEVICE_OBJECT DeviceObject, UCHAR MinorFunction, POWER_STATE PowerState, PVOID Context, PIO_STATUS_BLOCK IoStatus);
NTSTATUS IdleNotificationRequestComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PMIDI_DEVICE MidiDevice); 
VOID PoIrpCompletionFunction(PDEVICE_OBJECT DeviceObject, UCHAR MinorFunction, POWER_STATE PowerState, PVOID Context, PIO_STATUS_BLOCK IoStatus);

#endif // !_MIDI_STUDIO_02_
