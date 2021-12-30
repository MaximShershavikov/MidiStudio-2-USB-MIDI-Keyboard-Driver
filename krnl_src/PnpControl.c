/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.
**********************************************************************************/

#include "MidiStudio2.h"

NTSTATUS MidiBulkPnpControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION IoStackLocation;
    NTSTATUS status;
    PMIDI_DEVICE MididDevice;

    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    MididDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    if (MididDevice->DeviceState == Removed)
    {
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_DELETE_PENDING;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DELETE_PENDING;
    }
    UsbIoIncrement(MididDevice);
    if (IoStackLocation->MinorFunction == IRP_MN_START_DEVICE)
    {
        ASSERT(MididDevice->IdleReqPend == 0);
    }
    else
    {
        if (MididDevice->SSEnable)
        {
            UsbIoCancelSelectSuspend(MididDevice);
        }
    }

    switch(IoStackLocation->MinorFunction)
    {
    case IRP_MN_STOP_DEVICE:
        status = StopMidiBulkDevice(DeviceObject, Irp);
        UsbIoDecrement(MididDevice);
        return status;

    case IRP_MN_START_DEVICE:
        status = StartMidiBulkDevice(DeviceObject, Irp);
        break;

    case IRP_MN_QUERY_REMOVE_DEVICE:
        return QueryRemoveMidiBulkDevice(DeviceObject, Irp);

    case IRP_MN_REMOVE_DEVICE:
        return RemoveMidiBulkDevice(DeviceObject, Irp);

    case IRP_MN_CANCEL_REMOVE_DEVICE:
        status = CanselRemoveMidiBulkDevice(DeviceObject, Irp);
        break;

    case IRP_MN_CANCEL_STOP_DEVICE:
        status = CancelStopMidiBulkDevice(DeviceObject, Irp);
        break;

    case IRP_MN_QUERY_CAPABILITIES:
        status = QueryCapabilitiesMidiBulkDevice(DeviceObject, Irp);
        break;

    case IRP_MN_SURPRISE_REMOVAL:
        status = SurpriseRemovalMidiBulkDevice(DeviceObject, Irp);
        UsbIoDecrement(MididDevice);
        return status;

    case IRP_MN_QUERY_STOP_DEVICE:
        return QueryStopMidiBulkDevice(DeviceObject, Irp);

    default:
        IoSkipCurrentIrpStackLocation(Irp);
        status = IofCallDriver(MididDevice->NextLowerDeviceObject, Irp);
        UsbIoDecrement(MididDevice);
        return status;
    }
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = status;
    IofCompleteRequest(Irp, IO_NO_INCREMENT);
    UsbIoDecrement(MididDevice);
    return status;
}

NTSTATUS StartMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    NTSTATUS status;
    KIRQL Irql;
    KEVENT Event;
    PMIDI_DEVICE MidiDevice;
    LARGE_INTEGER DueTime;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    MidiDevice->UsbConfigDescriptorWithPipes = NULL;
    MidiDevice->UsbdInterfaceInformation = NULL;
    MidiDevice->PipeContext = NULL;
    
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, UsbIrpCompletionRoutine, &Event, TRUE, TRUE, TRUE);

    status = IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        status = Irp->IoStatus.Status;
    }
    if (NT_SUCCESS(status))
    {
        status = DeviceDescriptorRequest(DeviceObject);
        if (NT_SUCCESS(status))
        {
            status = IoSetDeviceInterfaceState(&MidiDevice->UnicodeString, TRUE);
            if (NT_SUCCESS(status))
            {
                Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->DeviceStateSpinLock);
                MidiDevice->PrevDevState = MidiDevice->DeviceState;
                MidiDevice->DeviceState = Working;

                MidiDevice->QueueState = AllowRequests;
                KeReleaseSpinLock(&MidiDevice->DeviceStateSpinLock, Irql);
                MidiDevice->FlagWWOutstanding = 0;
                MidiDevice->FlagWWCancel = 0;
                MidiDevice->WaitWakeIrp = NULL;
                if (MidiDevice->WaitWakeEnable)
                {
                    IrpIssueWaitWake(MidiDevice);
                }
                RemoveEntriesAndProcessListQueue(MidiDevice);
                if (MidiDevice->WdmVerWinXpOrBetter == TRUE)
                {
                    MidiDevice->SSEnable = MidiDevice->SSRegistryEnable;
                    if (MidiDevice->SSEnable)
                    {
                        DueTime.QuadPart = -10000 * 5000;
                        KeSetTimerEx(&MidiDevice->Ktimer, DueTime, 5000, &MidiDevice->Dpc);
                        MidiDevice->FreeIdleIrpCount = 0;
                    }
                }
            }
        }
    }
    return status;
}

NTSTATUS StopMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    KIRQL Irql;
    NTSTATUS status;
    PMIDI_DEVICE MidiDevice;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    if (MidiDevice->WdmVerWinXpOrBetter == TRUE && MidiDevice->SSEnable)
    {
        KeCancelTimer(&MidiDevice->Ktimer);
        MidiDevice->SSEnable = 0;
        KeWaitForSingleObject(&MidiDevice->NoDpcWorkItemPendingEvent, Executive, KernelMode, FALSE, NULL);
        KeWaitForSingleObject(&MidiDevice->NoIdleRequestPendingEvent, Executive, KernelMode, FALSE, NULL);
    }
    if (MidiDevice->WaitWakeEnable)
    {
        IrpCancelWaitWake(MidiDevice);
    }
    Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->DeviceStateSpinLock);

    MidiDevice->PrevDevState = MidiDevice->DeviceState;
    MidiDevice->DeviceState = Stopped;

    KeReleaseSpinLock(&MidiDevice->DeviceStateSpinLock, Irql);
    FreeMemory(DeviceObject);
    status = DeconfigureMidiBulkDevice(DeviceObject);

    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(Irp);
    return IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
}

NTSTATUS QueryRemoveMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    KIRQL Irql;
    PMIDI_DEVICE MidiDevice;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->DeviceStateSpinLock);
    MidiDevice->PrevDevState = MidiDevice->DeviceState;
    MidiDevice->DeviceState = PendingRemove;

    MidiDevice->QueueState = HoldRequests;
    KeReleaseSpinLock(&MidiDevice->DeviceStateSpinLock, Irql);
    UsbIoDecrement(MidiDevice);
    KeWaitForSingleObject(&MidiDevice->StopEvent, Executive, KernelMode, FALSE, NULL);
    
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(Irp);
    return IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
}

NTSTATUS RemoveMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    KIRQL Irql;
    NTSTATUS status;
    PMIDI_DEVICE MidiDevice;
    ULONG count;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    if (MidiDevice->DeviceState != SurpriseRemoved)
    {
        Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->DeviceStateSpinLock);
        MidiDevice->QueueState = FailRequests;
        KeReleaseSpinLock(&MidiDevice->DeviceStateSpinLock, Irql);
        if (MidiDevice->WaitWakeEnable)
        {
            IrpCancelWaitWake(MidiDevice);
        }
        if (MidiDevice->WdmVerWinXpOrBetter == TRUE && MidiDevice->SSEnable)
        {
            KeCancelTimer(&MidiDevice->Ktimer);
            MidiDevice->SSEnable = 0;
            KeWaitForSingleObject(&MidiDevice->NoDpcWorkItemPendingEvent, Executive, KernelMode, FALSE, NULL);
            KeWaitForSingleObject(&MidiDevice->NoIdleRequestPendingEvent, Executive, KernelMode, FALSE, NULL);
        }
        RemoveEntriesAndProcessListQueue(MidiDevice);
        IoSetDeviceInterfaceState(&MidiDevice->UnicodeString, FALSE);
        RtlFreeUnicodeString(&MidiDevice->UnicodeString);
        CansellPipesOfMidiBulkDevice(DeviceObject);
    }
    Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->DeviceStateSpinLock);
    MidiDevice->PrevDevState = MidiDevice->DeviceState;
    MidiDevice->DeviceState = Removed;
    KeReleaseSpinLock(&MidiDevice->DeviceStateSpinLock, Irql);

    IoWmiDeRegCtrl(MidiDevice);
    
    count = UsbIoDecrement(MidiDevice);
    ASSERT(count > 0);
    count = UsbIoDecrement(MidiDevice);

    KeWaitForSingleObject(&MidiDevice->RemoveEvent, Executive, KernelMode, FALSE, NULL);
    FreeMemory(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(Irp);

    status = IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    IoDetachDevice(MidiDevice->NextLowerDeviceObject);
    IoDeleteDevice(DeviceObject);
    return status;
}

NTSTATUS CanselRemoveMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    NTSTATUS status;
    KIRQL Irql;
    KEVENT Event;
    PMIDI_DEVICE MidiDevice;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    if (MidiDevice->DeviceState != PendingRemove)
    {
        return STATUS_SUCCESS;
    }
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)UsbIrpCompletionRoutine, (PVOID)&Event, TRUE, TRUE, TRUE);
    status = IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        status = Irp->IoStatus.Status;
    }
    if (NT_SUCCESS(status))
    {
        Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->DeviceStateSpinLock);
        MidiDevice->DeviceState = MidiDevice->PrevDevState;
        MidiDevice->QueueState = AllowRequests;
        KeReleaseSpinLock(&MidiDevice->DeviceStateSpinLock, Irql);
        RemoveEntriesAndProcessListQueue(MidiDevice);
    }
    return status;
}

NTSTATUS CancelStopMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    NTSTATUS status;
    KIRQL Irql;
    KEVENT Event;
    PMIDI_DEVICE MidiDevice;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    if (MidiDevice->DeviceState != PendingStop)
    {
        return STATUS_SUCCESS;
    }
    
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)UsbIrpCompletionRoutine, (PVOID)&Event, TRUE, TRUE, TRUE);

    status = IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        status = Irp->IoStatus.Status;
    }
    if (NT_SUCCESS(status))
    {
        Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->DeviceStateSpinLock);
        MidiDevice->DeviceState = MidiDevice->PrevDevState;
        MidiDevice->QueueState = AllowRequests;
        ASSERT(MidiDevice->DeviceState == Working);
        KeReleaseSpinLock(&MidiDevice->DeviceStateSpinLock, Irql);
        RemoveEntriesAndProcessListQueue(MidiDevice);
    }
    return status;
}

NTSTATUS QueryCapabilitiesMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    NTSTATUS status;
    KEVENT Event;
    PMIDI_DEVICE MidiDevice;
    PIO_STACK_LOCATION IoStackLocation;
    PDEVICE_CAPABILITIES Pdc;
    
    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    Pdc = IoStackLocation->Parameters.DeviceCapabilities.Capabilities;

    if (Pdc->Version < 1 || Pdc->Size < sizeof(DEVICE_CAPABILITIES))
    {
        return STATUS_UNSUCCESSFUL;
    }

    Pdc->SurpriseRemovalOK = TRUE;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)UsbIrpCompletionRoutine, (PVOID)&Event, TRUE, TRUE, TRUE);

    status = IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        status = Irp->IoStatus.Status;
    }
    MidiDevice->PowerDownLevel = PowerDeviceUnspecified;
    if (NT_SUCCESS(status))
    {
        MidiDevice->DeviceCapabilities = *Pdc;
        for (int i = PowerSystemSleeping1; i <= PowerSystemSleeping3; i++)
        {
            if (MidiDevice->DeviceCapabilities.DeviceState[i] < PowerDeviceD3)
            {
                MidiDevice->PowerDownLevel = MidiDevice->DeviceCapabilities.DeviceState[i];
            }
        }
        Pdc->SurpriseRemovalOK = 1;
    }
    if (MidiDevice->PowerDownLevel == PowerDeviceUnspecified || MidiDevice->PowerDownLevel <= PowerDeviceD0)
    {
        MidiDevice->PowerDownLevel = PowerDeviceD2;
    }
    return status;
}

NTSTATUS QueryStopMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    KIRQL Irql;
    PMIDI_DEVICE MidiDevice;
    
    MidiDevice  = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->DeviceStateSpinLock);

    MidiDevice->PrevDevState = MidiDevice->DeviceState;
    MidiDevice->DeviceState = PendingStop;

    MidiDevice->QueueState = HoldRequests;
    KeReleaseSpinLock(&MidiDevice->DeviceStateSpinLock, Irql);
    UsbIoDecrement(MidiDevice);
    KeWaitForSingleObject(&MidiDevice->StopEvent, Executive, KernelMode, FALSE, NULL);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(Irp);
    return IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
}

NTSTATUS SurpriseRemovalMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    KIRQL Irql;
    PMIDI_DEVICE MidiDevice;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    if (MidiDevice->WaitWakeEnable)
    {
        IrpCancelWaitWake(MidiDevice);
    }
    if (MidiDevice->WdmVerWinXpOrBetter == TRUE && MidiDevice->SSEnable)
    {
        KeCancelTimer(&MidiDevice->Ktimer);
        MidiDevice->SSEnable = 0;
        KeWaitForSingleObject(&MidiDevice->NoDpcWorkItemPendingEvent, Executive, KernelMode, FALSE, NULL);
        KeWaitForSingleObject(&MidiDevice->NoIdleRequestPendingEvent, Executive, KernelMode, FALSE, NULL);
    }
    Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->DeviceStateSpinLock);
    MidiDevice->PrevDevState = MidiDevice->DeviceState;
    MidiDevice->DeviceState = SurpriseRemoved;

    MidiDevice->QueueState = FailRequests;
    KeReleaseSpinLock(&MidiDevice->DeviceStateSpinLock, Irql);
    RemoveEntriesAndProcessListQueue(MidiDevice);
    IoSetDeviceInterfaceState(&MidiDevice->UnicodeString, FALSE);
    RtlFreeUnicodeString(&MidiDevice->UnicodeString);
    CansellPipesOfMidiBulkDevice(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoSkipCurrentIrpStackLocation(Irp);
    return IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
}

NTSTATUS FreeMemory(PDEVICE_OBJECT DeviceObject)
{
    PMIDI_DEVICE MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    if (MidiDevice->UsbConfigDescriptorWithPipes)
    {
        ExFreePool(MidiDevice->UsbConfigDescriptorWithPipes);
        MidiDevice->UsbConfigDescriptorWithPipes = NULL;
        MidiDevice->ConfigDevicePtr = NULL;
    }
    if (MidiDevice->UsbdInterfaceInformation)
    {
        ExFreePool(MidiDevice->UsbdInterfaceInformation);
        MidiDevice->UsbdInterfaceInformation = NULL;
        MidiDevice->PtrInterfaceInfo = NULL;
    }
    if (MidiDevice->PipeContext)
    {
        ExFreePool(MidiDevice->PipeContext);
        MidiDevice->PipeContext = NULL;
    }
    if (MidiDevice->UsbDeviseDescriptor)
    {
        ExFreePool(MidiDevice->UsbDeviseDescriptor);
        MidiDevice->UsbDeviseDescriptor = NULL;
    }
    return STATUS_SUCCESS;
}
