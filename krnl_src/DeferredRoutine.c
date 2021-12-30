/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.
**********************************************************************************/

#include "MidiStudio2.h"

VOID DeferredRoutineDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2) 
{
    PIO_WORKITEM item;
    PDEVICE_OBJECT DeviceObject;
    PMIDI_DEVICE MidiDevice;

    DeviceObject = (PDEVICE_OBJECT)DeferredContext;
    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    KeClearEvent(&MidiDevice->NoDpcWorkItemPendingEvent);

    if (CanUsbDeviceSuspend(MidiDevice) && (item = IoAllocateWorkItem((PDEVICE_OBJECT)DeferredContext)) != 0)
    {
        IoQueueWorkItem(item, IdleUsbRequestWorkerRoutine, DelayedWorkQueue, item);
    }
    else
    {
        KeSetEvent(&MidiDevice->NoDpcWorkItemPendingEvent, IO_NO_INCREMENT, FALSE);
    }
}

VOID PoIrpCompletionFunction(PDEVICE_OBJECT DeviceObject, UCHAR MinorFunction, POWER_STATE PowerState, PVOID Context, PIO_STATUS_BLOCK IoStatus)
{
    PIRP_COMPLETION_CONTEXT IrpCompletionContext;
    IrpCompletionContext = NULL;

    if (Context)
    {
        IrpCompletionContext = (PIRP_COMPLETION_CONTEXT)Context;
    }

    if (IrpCompletionContext)
    {
        KeSetEvent(IrpCompletionContext->Event, 0, FALSE);
        UsbIoDecrement(IrpCompletionContext->MidiDevice);
        ExFreePool(IrpCompletionContext);
    }
}

VOID IdleUsbRequestWorkerRoutine(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
    PIO_WORKITEM item = (PIO_WORKITEM)Context;
    PMIDI_DEVICE MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;

    if (CanUsbDeviceSuspend(MidiDevice))
    {
        IdleRequestIrpSubmit(MidiDevice);
    }

    IoFreeWorkItem(item);
    KeSetEvent(&MidiDevice->NoDpcWorkItemPendingEvent, IO_NO_INCREMENT, FALSE);
}

BOOLEAN CanUsbDeviceSuspend(PMIDI_DEVICE MidiDevice) 
{
    if (MidiDevice->OpenHandleCount == 0 && MidiDevice->OutStandingIO == 1)
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

VOID PoIrpAsyncOrWWIrpCompletionFunction(PDEVICE_OBJECT DeviceObject, UCHAR MinorFunction, POWER_STATE PowerState, PVOID Context, PIO_STATUS_BLOCK IoStatus)
{
    PMIDI_DEVICE MidiDevice = (PMIDI_DEVICE)Context;
    UsbIoDecrement(MidiDevice);
}

VOID IdleNotificationCallback(PMIDI_DEVICE MidiDevice) 
{
    PIRP_COMPLETION_CONTEXT IrpCompletionContext;
    NTSTATUS status;
    KEVENT Event;
    POWER_STATE PowerState;

    if (MidiDevice->DeviceState == Working)
    {
        if (MidiDevice->WaitWakeEnable)
        {
            IrpIssueWaitWake(MidiDevice);
        }
        IrpCompletionContext = (PIRP_COMPLETION_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(IRP_COMPLETION_CONTEXT), MIDISTUDIO_02_TAG);
        if (IrpCompletionContext)
        {
            UsbIoIncrement(MidiDevice);
            PowerState.DeviceState = MidiDevice->PowerDownLevel;
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IrpCompletionContext->MidiDevice = MidiDevice;
            IrpCompletionContext->Event = &Event;
            status = PoRequestPowerIrp(MidiDevice->PhysicalDeviceObject, IRP_MN_SET_POWER, PowerState, (PREQUEST_POWER_COMPLETE)PoIrpCompletionFunction, IrpCompletionContext, NULL);
            if (status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            }
            if (!NT_SUCCESS(status))
            {
                ExFreePool(IrpCompletionContext);
            }
        }
    }
}

NTSTATUS IdleNotificationRequestComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PMIDI_DEVICE MidiDevice) 
{
    NTSTATUS status;
    PUSB_IDLE_CALLBACK_INFO UsbIdleCallbackInfo;
    PIRP IdleIrp = NULL;
    KIRQL Irql;
    POWER_STATE PowerState;
    LARGE_INTEGER DueTime;

    status = Irp->IoStatus.Status;
    if (!NT_SUCCESS(status) && status != STATUS_NOT_SUPPORTED && status != STATUS_POWER_STATE_INVALID && MidiDevice->DevicePower != PowerDeviceD0)
    {
        UsbIoIncrement(MidiDevice);
        PowerState.DeviceState = PowerDeviceD0;
        PoRequestPowerIrp(MidiDevice->PhysicalDeviceObject, IRP_MN_SET_POWER, PowerState, (PREQUEST_POWER_COMPLETE)PoIrpAsyncOrWWIrpCompletionFunction, MidiDevice, NULL);
    }
    Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->IdleReqSateSpinLock);
    UsbIdleCallbackInfo = MidiDevice->UsbIdleCallbackInfo;
    MidiDevice->UsbIdleCallbackInfo = NULL;

    IdleIrp = (PIRP)InterlockedExchangePointer(&MidiDevice->PendingIdleIrp, NULL);
    InterlockedExchange(&MidiDevice->IdleReqPend, 0);
    KeReleaseSpinLock(&MidiDevice->IdleReqSateSpinLock, Irql);
    if (UsbIdleCallbackInfo)
    {
        ExFreePool(UsbIdleCallbackInfo);
    }
    if (IdleIrp || (InterlockedDecrement(&MidiDevice->FreeIdleIrpCount)) == 0)
    {
        IoFreeIrp(Irp);
        KeSetEvent(&MidiDevice->NoIdleRequestPendingEvent, IO_NO_INCREMENT, FALSE);
    }
    if (MidiDevice->SSEnable)
    {
        DueTime.QuadPart = -10000 * 5000;
        KeSetTimerEx(&MidiDevice->Ktimer, DueTime, 5000, &MidiDevice->Dpc);
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS IdleRequestIrpSubmit(PMIDI_DEVICE MidiDevice) 
{
    NTSTATUS status;
    PUSB_IDLE_CALLBACK_INFO UsbIdleCallbackInfo = NULL;
    PIRP Irp = NULL;
    PIO_STACK_LOCATION IoStackLocation;
    KIRQL Irql;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (MidiDevice->DevicePower != PowerDeviceD0)
    {
        return STATUS_POWER_STATE_INVALID;
    }
    Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->IdleReqSateSpinLock);
    if (InterlockedExchange(&MidiDevice->IdleReqPend, 1))
    {
        KeReleaseSpinLock(&MidiDevice->IdleReqSateSpinLock, Irql);
        return STATUS_DEVICE_BUSY;
    }
    KeClearEvent(&MidiDevice->NoIdleRequestPendingEvent);
    UsbIdleCallbackInfo = (PUSB_IDLE_CALLBACK_INFO)ExAllocatePoolWithTag(NonPagedPool, sizeof(struct _USB_IDLE_CALLBACK_INFO), MIDISTUDIO_02_TAG);

    if (!UsbIdleCallbackInfo)
    {
        KeSetEvent(&MidiDevice->NoIdleRequestPendingEvent, IO_NO_INCREMENT, FALSE);
        InterlockedExchange(&MidiDevice->IdleReqPend, 0);
        KeReleaseSpinLock(&MidiDevice->IdleReqSateSpinLock, Irql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    UsbIdleCallbackInfo->IdleCallback = IdleNotificationCallback;
    UsbIdleCallbackInfo->IdleContext = (PVOID)MidiDevice;
    ASSERT(MidiDevice->UsbIdleCallbackInfo == NULL);
    MidiDevice->UsbIdleCallbackInfo = UsbIdleCallbackInfo;
    
    Irp = (PIRP)IoAllocateIrp(MidiDevice->NextLowerDeviceObject->StackSize, FALSE);
    if (!Irp)
    {
        KeSetEvent(&MidiDevice->NoIdleRequestPendingEvent, IO_NO_INCREMENT, FALSE);
        InterlockedExchange(&MidiDevice->IdleReqPend, 0);
        KeReleaseSpinLock(&MidiDevice->IdleReqSateSpinLock, Irql);
        ExFreePool(UsbIdleCallbackInfo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    IoStackLocation = IoGetNextIrpStackLocation(Irp);
    IoStackLocation->Parameters.DeviceIoControl.Type3InputBuffer = UsbIdleCallbackInfo;
    IoStackLocation->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStackLocation->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION;
    IoStackLocation->Parameters.DeviceIoControl.InputBufferLength = sizeof(struct _USB_IDLE_CALLBACK_INFO);
    
    IoSetCompletionRoutine(Irp, IdleNotificationRequestComplete, MidiDevice, TRUE, TRUE, TRUE);
    MidiDevice->PendingIdleIrp = Irp;
    MidiDevice->FreeIdleIrpCount = 2;
    KeReleaseSpinLock(&MidiDevice->IdleReqSateSpinLock, Irql);
    
    if (!CanUsbDeviceSuspend(MidiDevice) || MidiDevice->DevicePower != PowerDeviceD0)
    {
        Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->IdleReqSateSpinLock);
        MidiDevice->UsbIdleCallbackInfo = NULL;
        MidiDevice->PendingIdleIrp = NULL;
        KeSetEvent(&MidiDevice->NoIdleRequestPendingEvent, IO_NO_INCREMENT, FALSE);
        InterlockedExchange(&MidiDevice->IdleReqPend, 0);
        KeReleaseSpinLock(&MidiDevice->IdleReqSateSpinLock, Irql);
        if (UsbIdleCallbackInfo)
        {
            ExFreePool(UsbIdleCallbackInfo);
        }
        if (Irp)
        {
            IoFreeIrp(Irp);
        }
        status = STATUS_UNSUCCESSFUL;
    }
    else
    {
        KeCancelTimer(&MidiDevice->Ktimer);
        status = IoCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    }
    return status;
}
