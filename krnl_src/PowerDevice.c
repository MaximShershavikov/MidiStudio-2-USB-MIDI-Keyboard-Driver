/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.
**********************************************************************************/

#include "MidiStudio2.h"

NTSTATUS MidiBulkPower(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION IoStackLocation;
    NTSTATUS status;
    PMIDI_DEVICE MidiDevice;

    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;

    if (MidiDevice->DeviceState == Removed)
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_DELETE_PENDING;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DELETE_PENDING;
    }
    if (MidiDevice->DeviceState == NotStarted)
    {
        PoStartNextPowerIrp(Irp);
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    }
    UsbIoIncrement(MidiDevice);
    switch (IoStackLocation->MinorFunction)
    {
    case IRP_MN_WAIT_WAKE:
        IoMarkIrpPending(Irp);
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)IrpWaitWakeCompletionRoutineMidiUsb, MidiDevice, TRUE, TRUE, TRUE);
        PoStartNextPowerIrp(Irp);
        PoCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
        status = STATUS_PENDING;
        UsbIoDecrement(MidiDevice);
        break;
    case IRP_MN_SET_POWER:
        IoMarkIrpPending(Irp);
        switch (IoStackLocation->Parameters.Power.Type)
        {
        case SystemPowerState:
            IrpSystemSetPowerMidiUsb(DeviceObject, Irp);
            status = STATUS_PENDING;
            break;
        case DevicePowerState:
            IrpDeviceSetPowerMidiUsb(DeviceObject, Irp);
            status = STATUS_PENDING;
            break;
        }
        break;
    case IRP_MN_QUERY_POWER:
        IoMarkIrpPending(Irp);
        switch (IoStackLocation->Parameters.Power.Type)
        {
        case SystemPowerState:
            IrpSystemQueryPowerMidiUsb(DeviceObject, Irp);
            status = STATUS_PENDING;
            break;
        case DevicePowerState:
            IrpDeviceQueryPowerMidiUsb(DeviceObject, Irp);
            status = STATUS_PENDING;
            break;
        }
        break;
    default:
        PoStartNextPowerIrp(Irp);
        IoSkipCurrentIrpStackLocation(Irp);
        status = PoCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
        UsbIoDecrement(MidiDevice);
    }
    return status;
}

NTSTATUS IrpSystemSetPowerMidiUsb(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PMIDI_DEVICE MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)IrpSystemPowerCompletionRoutine, MidiDevice, TRUE, TRUE, TRUE);
    PoCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    return STATUS_PENDING;
}

NTSTATUS IrpDeviceSetPowerMidiUsb(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    POWER_STATE newState;
    DEVICE_POWER_STATE oldDevState;
    DEVICE_POWER_STATE newDevState;
    NTSTATUS status;
    KIRQL Irql;
    PIO_STACK_LOCATION IoStackLocation;
    PMIDI_DEVICE MidiDevice;

    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    newState = IoStackLocation->Parameters.Power.State;
    newDevState = newState.DeviceState;
    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    oldDevState = MidiDevice->DevicePower;
    if (newDevState < oldDevState)
    {
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)FinishUsbDevicePoUpIrp, MidiDevice, TRUE, TRUE, TRUE);
        PoCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
        return STATUS_PENDING;
    }
    else if (oldDevState == PowerDeviceD0 && newDevState > oldDevState)
    {
        status = UsbIoHoldRequests(DeviceObject, Irp);
        if (!NT_SUCCESS(status))
        {
            PoStartNextPowerIrp(Irp);
            Irp->IoStatus.Information = 0;
            Irp->IoStatus.Status = status;
            IofCompleteRequest(Irp, IO_NO_INCREMENT);
            UsbIoDecrement(MidiDevice);
            return status;
        }
        return STATUS_PENDING;
    }
    else if (oldDevState == PowerDeviceD0 && newDevState == PowerDeviceD0)
    {
        Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->DeviceStateSpinLock);
        MidiDevice->QueueState = AllowRequests;
        KeReleaseSpinLock(&MidiDevice->DeviceStateSpinLock, Irql);
        RemoveEntriesAndProcessListQueue(MidiDevice);
    }
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)FinishUsbDevicePoDnIrp, MidiDevice, TRUE, TRUE, TRUE);
    PoCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    return STATUS_PENDING;
}

NTSTATUS IrpSystemQueryPowerMidiUsb(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PMIDI_DEVICE MidiDevice;
    PIO_STACK_LOCATION IoStackLocation;
    SYSTEM_POWER_STATE SystemState;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    SystemState = IoStackLocation->Parameters.Power.State.SystemState;
    if (SystemState > MidiDevice->SystemPower && MidiDevice->WaitWakeEnable)
    {
        IrpIssueWaitWake(MidiDevice);
    }
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)IrpSystemPowerCompletionRoutine, MidiDevice, TRUE, TRUE, TRUE);
    PoCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    return STATUS_PENDING;
}

NTSTATUS IrpDeviceQueryPowerMidiUsb(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    NTSTATUS status;
    PMIDI_DEVICE MidiDevice;
    PIO_STACK_LOCATION IoStackLocation;
    DEVICE_POWER_STATE DeviceState;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    DeviceState = IoStackLocation->Parameters.Power.State.DeviceState;

    if (DeviceState >= MidiDevice->DevicePower)
    {
        status = UsbIoHoldRequests(DeviceObject, Irp);
        if (status == STATUS_PENDING)
        {
            return status;
        }
    }
    else
    {
        status = STATUS_SUCCESS;
    }
    PoStartNextPowerIrp(Irp);
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = status;
    if (NT_SUCCESS(status))
    {
        IoSkipCurrentIrpStackLocation(Irp);
        status = PoCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    }
    else
    {
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
    }
    UsbIoDecrement(MidiDevice);
    return status;
}

NTSTATUS FinishUsbDevicePoUpIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp, PMIDI_DEVICE MidiDevice)
{
    NTSTATUS status;

    status = Irp->IoStatus.Status;
    if (Irp->PendingReturned)
    {
        IoMarkIrpPending(Irp);
    }
    if (NT_SUCCESS(status))
    {
        SetupUsbDeviceFunctional(DeviceObject, Irp, MidiDevice);
        status = STATUS_MORE_PROCESSING_REQUIRED;
    }
    else
    {
        PoStartNextPowerIrp(Irp);
        UsbIoDecrement(MidiDevice);
        status = STATUS_SUCCESS;
    }
    return status;
}

NTSTATUS SetupUsbDeviceFunctional(PDEVICE_OBJECT DeviceObject, PIRP Irp, PMIDI_DEVICE MidiDevice)
{
    NTSTATUS status;
    POWER_STATE NewState;
    KIRQL Irql;
    DEVICE_POWER_STATE NewDeviceState;
    DEVICE_POWER_STATE OldDeviceState;
    PIO_STACK_LOCATION IoStackLocation;

    status = Irp->IoStatus.Status;
    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    NewState = IoStackLocation->Parameters.Power.State;
    NewDeviceState = NewState.DeviceState;
    OldDeviceState = MidiDevice->DevicePower;
    MidiDevice->DevicePower = NewDeviceState;
    PoSetPowerState(DeviceObject, DevicePowerState, NewState);
    if (NewDeviceState == PowerDeviceD0)
    {
        Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->DeviceStateSpinLock);
        MidiDevice->QueueState = AllowRequests;
        KeReleaseSpinLock(&MidiDevice->DeviceStateSpinLock, Irql);
        RemoveEntriesAndProcessListQueue(MidiDevice);
    }
    PoStartNextPowerIrp(Irp);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IofCompleteRequest(Irp, IO_NO_INCREMENT);
    UsbIoDecrement(MidiDevice);
    return STATUS_SUCCESS;
}

NTSTATUS IrpWaitWakeCompletionRoutineMidiUsb(PDEVICE_OBJECT DeviceObject, PIRP Irp, PMIDI_DEVICE MidiDevice)
{
    if (Irp->PendingReturned)
    {
        IoMarkIrpPending(Irp);
    }
    if (InterlockedExchangePointer(&MidiDevice->WaitWakeIrp, NULL))
    {
        PoStartNextPowerIrp(Irp);
        return STATUS_SUCCESS;
    }
    if (InterlockedExchange(&MidiDevice->FlagWWCancel, 1))
    {
        PoStartNextPowerIrp(Irp);
        return STATUS_CANCELLED;
    }
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS IrpSystemPowerCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PMIDI_DEVICE MidiDevice)
{
    NTSTATUS status = Irp->IoStatus.Status;
    PIO_STACK_LOCATION IoStackLocation = IoGetCurrentIrpStackLocation(Irp);

    if (NT_SUCCESS(status))
    {
        if (IoStackLocation->MinorFunction == IRP_MN_SET_POWER)
        {
            MidiDevice->SystemPower = IoStackLocation->Parameters.Power.State.SystemState;
        }
        SendUsbDeviceIrp(DeviceObject, Irp);
        status = STATUS_MORE_PROCESSING_REQUIRED;
    }
    else
    {
        PoStartNextPowerIrp(Irp);
        UsbIoDecrement(MidiDevice);
        status = STATUS_SUCCESS;
    }
    return status;
}

VOID SendUsbDeviceIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    POWER_STATE PowerState;
    NTSTATUS status;
    PMIDI_DEVICE MidiDevice;
    PIO_STACK_LOCATION IoStackLocation;
    SYSTEM_POWER_STATE SystemState;
    DEVICE_POWER_STATE DeviceState;
    PPOWER_COMPLETION_CONTEXT PowerContext;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    SystemState = IoStackLocation->Parameters.Power.State.SystemState;
    DeviceState = MidiDevice->DeviceCapabilities.DeviceState[SystemState];
    PowerState.DeviceState = DeviceState;
    PowerContext = (PPOWER_COMPLETION_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(POWER_COMPLETION_CONTEXT), MIDISTUDIO_02_TAG);

    if (!PowerContext)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    else
    {
        PowerContext->DeviceObject = DeviceObject;
        PowerContext->Irp = Irp;
        status = PoRequestPowerIrp(MidiDevice->PhysicalDeviceObject, IoStackLocation->MinorFunction, PowerState, (PREQUEST_POWER_COMPLETE)UsbDevicePoCompletionRoutine, PowerContext, NULL);
    }
    if (!NT_SUCCESS(status))
    {
        if (PowerContext)
        {
            ExFreePool(PowerContext);
        }
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = status;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
        UsbIoDecrement(MidiDevice);
    }
}

NTSTATUS UsbIoHoldRequests(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PWORKER_THREAD_CONTEXT ThreadContext = NULL;
    PIO_WORKITEM item = NULL;
    PMIDI_DEVICE MidiDevice;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    MidiDevice->QueueState = HoldRequests;
    ThreadContext = (PWORKER_THREAD_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(WORKER_THREAD_CONTEXT), MIDISTUDIO_02_TAG);
    if (ThreadContext)
    {
        item = IoAllocateWorkItem(DeviceObject);
        ThreadContext->Irp = Irp;
        ThreadContext->DeviceObject = DeviceObject;
        ThreadContext->WorkItem = item;
        if (item)
        {
            IoMarkIrpPending(Irp);
            IoQueueWorkItem(item, HoldUsbDevIoRequestsWorkerRoutine, DelayedWorkQueue, ThreadContext);
            return STATUS_PENDING;
        }
        ExFreePool(ThreadContext);
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

VOID IrpCancelWaitWake(PMIDI_DEVICE MidiDevice)
{
    PIRP Irp = (PIRP)InterlockedExchangePointer(&MidiDevice->WaitWakeIrp, NULL);
    if (Irp)
    {
        IoCancelIrp(Irp);
        if (InterlockedExchange(&MidiDevice->FlagWWCancel, 1))
        {
            PoStartNextPowerIrp(Irp);
            Irp->IoStatus.Information = 0;
            Irp->IoStatus.Status = STATUS_CANCELLED;
            IofCompleteRequest(Irp, IO_NO_INCREMENT);
        }
    }
}

NTSTATUS IrpIssueWaitWake(PMIDI_DEVICE MidiDevice) 
{
    POWER_STATE poState;
    NTSTATUS status;

    if (InterlockedExchange(&MidiDevice->FlagWWOutstanding, 1))
    {
        return STATUS_DEVICE_BUSY;
    }
    InterlockedExchange(&MidiDevice->FlagWWCancel, 0);
    poState.SystemState = MidiDevice->DeviceCapabilities.SystemWake;
    status = PoRequestPowerIrp(MidiDevice->PhysicalDeviceObject, IRP_MN_WAIT_WAKE, poState, (PREQUEST_POWER_COMPLETE)WaitWakeIrpCallback, MidiDevice, &MidiDevice->WaitWakeIrp);
    if (!NT_SUCCESS(status))
    {
        InterlockedExchange(&MidiDevice->FlagWWOutstanding, 0);
    }
    return status;
}

VOID HoldUsbDevIoRequestsWorkerRoutine(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
    PIRP Irp;
    PMIDI_DEVICE MidiDevice;
    PWORKER_THREAD_CONTEXT ThreadContext;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    ThreadContext = (PWORKER_THREAD_CONTEXT)Context;
    Irp = (PIRP)ThreadContext->Irp;

    UsbIoDecrement(MidiDevice);
    UsbIoDecrement(MidiDevice);
    KeWaitForSingleObject(&MidiDevice->StopEvent, Executive, KernelMode, FALSE, NULL);
    UsbIoIncrement(MidiDevice);
    UsbIoIncrement(MidiDevice);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)FinishUsbDevicePoDnIrp, MidiDevice, TRUE, TRUE, TRUE);
    PoCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    IoFreeWorkItem(ThreadContext->WorkItem);
    ExFreePool((PVOID)ThreadContext);
}

VOID UsbDevicePoCompletionRoutine(PDEVICE_OBJECT DeviceObject, UCHAR MinorFunction, POWER_STATE PowerState, PVOID Context, PIO_STATUS_BLOCK IoStatus)
{
    PIRP Irp;
    PPOWER_COMPLETION_CONTEXT PowerContext;
    PMIDI_DEVICE MidiDevice;

    PowerContext = (PPOWER_COMPLETION_CONTEXT)Context;
    Irp = PowerContext->Irp;
    MidiDevice = PowerContext->DeviceObject->DeviceExtension;

    Irp->IoStatus.Status = IoStatus->Status;
    PoStartNextPowerIrp(Irp);
    Irp->IoStatus.Information = 0;
    IofCompleteRequest(Irp, IO_NO_INCREMENT);
    UsbIoDecrement(MidiDevice);
    ExFreePool(PowerContext);
}

VOID WaitWakeIrpCallback(PDEVICE_OBJECT DeviceObject, UCHAR MinorFunction, POWER_STATE PowerState, PVOID Context, PIO_STATUS_BLOCK IoStatus)
{
    POWER_STATE powerState;
    PMIDI_DEVICE MidiDevice;

    MidiDevice = (PMIDI_DEVICE)Context;
    InterlockedExchange(&MidiDevice->FlagWWOutstanding, 0);
    if (NT_SUCCESS(IoStatus->Status) && MidiDevice->DevicePower != PowerDeviceD0)
    {
        UsbIoIncrement(MidiDevice);
        powerState.DeviceState = PowerDeviceD0;
        PoRequestPowerIrp(MidiDevice->PhysicalDeviceObject, IRP_MN_SET_POWER, powerState, (PREQUEST_POWER_COMPLETE)PoIrpAsyncOrWWIrpCompletionFunction, MidiDevice, NULL);
        if (MidiDevice->WaitWakeEnable)
        {
            IrpIssueWaitWake(MidiDevice);
        }
    }
}

NTSTATUS FinishUsbDevicePoDnIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp, PMIDI_DEVICE MidiDevice)
{
    PIO_STACK_LOCATION IoStackLocation;
    POWER_STATE NewState;
    NTSTATUS status;

    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    status = Irp->IoStatus.Status;
    NewState = IoStackLocation->Parameters.Power.State;
    if (NT_SUCCESS(status) && IoStackLocation->MinorFunction == IRP_MN_SET_POWER)
    {
        MidiDevice->DevicePower = NewState.DeviceState;
        PoSetPowerState(DeviceObject, DevicePowerState, NewState);
    }
    PoStartNextPowerIrp(Irp);
    UsbIoDecrement(MidiDevice);
    return STATUS_SUCCESS;
}
