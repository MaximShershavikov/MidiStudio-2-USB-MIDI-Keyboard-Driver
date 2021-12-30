/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.
**********************************************************************************/

#include "MidiStudio2.h"

VOID RemoveEntriesAndProcessListQueue(PMIDI_DEVICE MidiDevice)
{
    KIRQL Irql;
    PLIST_ENTRY ListMain;
    PLIST_ENTRY ListCancel;
    PIRP Irp;
    PVOID CancelRoutine = NULL;
    LIST_ENTRY CancelIrpList;
    
    InitializeListHead(&CancelIrpList);
    Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->IoQueueSpinLock);
    while (!IsListEmpty(&MidiDevice->ListOfNewRequestsQueue))
    {
        ListMain = RemoveHeadList(&MidiDevice->ListOfNewRequestsQueue);
        Irp = CONTAINING_RECORD(ListMain, IRP, Tail.Overlay.ListEntry);
        CancelRoutine = IoSetCancelRoutine(Irp, NULL);
        
        if (Irp->Cancel)
        {
            if (CancelRoutine)
            {
                InsertTailList(&CancelIrpList, ListMain);
            }
            else
            {
                InitializeListHead(ListMain);
            }
            KeReleaseSpinLock(&MidiDevice->IoQueueSpinLock, Irql);
        }
        else
        {
            KeReleaseSpinLock(&MidiDevice->IoQueueSpinLock, Irql);
            if (MidiDevice->QueueState == FailRequests)
            {
                Irp->IoStatus.Information = 0;
                Irp->IoStatus.Status = STATUS_DELETE_PENDING;
                IofCompleteRequest(Irp, IO_NO_INCREMENT);
            }
            else
            {
                UsbIoIncrement(MidiDevice);
                IoSkipCurrentIrpStackLocation(Irp);
                IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
                UsbIoDecrement(MidiDevice);
            }
        }
        Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->IoQueueSpinLock);
    }
    KeReleaseSpinLock(&MidiDevice->IoQueueSpinLock, Irql);
    
    while (!IsListEmpty(&CancelIrpList))
    {
        ListCancel = RemoveHeadList(&CancelIrpList);

        Irp = CONTAINING_RECORD(ListCancel, IRP, Tail.Overlay.ListEntry);
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_CANCELLED;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
    }
}

NTSTATUS CleanListEntryIrps(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    KIRQL Irql;
    PLIST_ENTRY ListMain;
    PLIST_ENTRY next;
    LIST_ENTRY cleanuplist;
    PIO_STACK_LOCATION IoStackLocation;
    PLIST_ENTRY current;
    PIRP CurrentIrp;
    PIO_STACK_LOCATION CurrentIoStackLocation;
    PMIDI_DEVICE MidiDevice;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);

    InitializeListHead(&cleanuplist);

    UsbIoIncrement(MidiDevice);
    Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->IoQueueSpinLock);

    ListMain = &MidiDevice->ListOfNewRequestsQueue;

    for (current = ListMain->Flink, next = current->Flink; current != ListMain; current = next, next = current->Flink)
    {
        CurrentIrp = CONTAINING_RECORD(current, IRP, Tail.Overlay.ListEntry);
        CurrentIoStackLocation = IoGetCurrentIrpStackLocation(CurrentIrp);

        if (IoStackLocation->FileObject == CurrentIoStackLocation->FileObject)
        {
            RemoveEntryList(current);
            if (IoSetCancelRoutine(CurrentIrp, NULL))
            {
                InsertTailList(&cleanuplist, current);
            }
            else
            {
                InitializeListHead(current);
            }
        }
    }
    KeReleaseSpinLock(&MidiDevice->IoQueueSpinLock, Irql);
    while (!IsListEmpty(&cleanuplist))
    {
        current = RemoveHeadList(&cleanuplist);
        CurrentIrp = CONTAINING_RECORD(current, IRP, Tail.Overlay.ListEntry);
        CurrentIrp->IoStatus.Information = 0;
        CurrentIrp->IoStatus.Status = STATUS_CANCELLED;
        IofCompleteRequest(CurrentIrp, IO_NO_INCREMENT);
    }
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IofCompleteRequest(Irp, IO_NO_INCREMENT);
    UsbIoDecrement(MidiDevice);
    return STATUS_SUCCESS;
}
