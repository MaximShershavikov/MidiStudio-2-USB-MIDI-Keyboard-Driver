/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.
**********************************************************************************/

#include "MidiStudio2.h"

VOID UsbIoCancelSelectSuspend(PMIDI_DEVICE MidiDevice) 
{
    PIRP Irp = NULL;
    KIRQL Irql;

    Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->IdleReqSateSpinLock);
    if (!CanUsbDeviceSuspend(MidiDevice))
    {
        Irp = (PIRP)InterlockedExchangePointer(&MidiDevice->PendingIdleIrp, NULL);
    }
    KeReleaseSpinLock(&MidiDevice->IdleReqSateSpinLock, Irql);
    if (Irp)
    {
        IoCancelIrp(Irp);
        if (!InterlockedDecrement(&MidiDevice->FreeIdleIrpCount))
        {
            IoFreeIrp(Irp);
            KeSetEvent(&MidiDevice->NoIdleRequestPendingEvent, IO_NO_INCREMENT, FALSE);
        }
    }
}

LONG UsbIoDecrement(PMIDI_DEVICE MidiDevice) 
{
    LONG res = 0;
    KIRQL Irql;

    Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->IoCountSpinLock);
    res = InterlockedDecrement(&MidiDevice->OutStandingIO);
    if (res == 1)
    {
        KeSetEvent(&MidiDevice->StopEvent, IO_NO_INCREMENT, FALSE);
    }
    if (res == 0)
    {
        ASSERT(MidiDevice->DeviceState == Removed);
        KeSetEvent(&MidiDevice->RemoveEvent, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&MidiDevice->IoCountSpinLock, Irql);
    return res;
}

LONG UsbIoIncrement(PMIDI_DEVICE MidiDevice) 
{
    KIRQL Irql;
    LONG res = 0;

    Irql = KeAcquireSpinLockRaiseToDpc(&MidiDevice->IoCountSpinLock);
    res = InterlockedIncrement(&MidiDevice->OutStandingIO);
    if (res == 2)
    {
        KeClearEvent(&MidiDevice->StopEvent);
    }
    KeReleaseSpinLock(&MidiDevice->IoCountSpinLock, Irql);
    return res;
}

NTSTATUS UsbIrpCompletionRoutine(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
    KeSetEvent((PRKEVENT)Context, 0, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}
