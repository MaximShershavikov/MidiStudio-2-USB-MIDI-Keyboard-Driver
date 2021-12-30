/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.
**********************************************************************************/

#include "MidiStudio2.h"

NTSTATUS UsbSubmitUrbRequest(PDEVICE_OBJECT DeviceObject, PURB Urb)
{
    PIRP Irp;
    NTSTATUS status;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION stack;
    PMIDI_DEVICE MidiDevice;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB, MidiDevice->NextLowerDeviceObject, NULL, 0, NULL, 0, TRUE, &Event, &IoStatusBlock);

    if (!Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    stack = IoGetNextIrpStackLocation(Irp);
    ASSERT(stack != NULL);
    stack->Parameters.Others.Argument1 = (PVOID)Urb;

    UsbIoIncrement(MidiDevice);
    status = IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        status = IoStatusBlock.Status;
    }
    UsbIoDecrement(MidiDevice);
    return status;
}

NTSTATUS UsbGetPortStatusRequest(PDEVICE_OBJECT DeviceObject, PULONG PortStatus)
{
    PIRP Irp;
    NTSTATUS status;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PMIDI_DEVICE MidiDevice;
    PIO_STACK_LOCATION stack;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    *PortStatus = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_GET_PORT_STATUS, MidiDevice->NextLowerDeviceObject, NULL, 0, NULL, 0, TRUE, &Event, &IoStatusBlock);
    if (!Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    stack = IoGetNextIrpStackLocation(Irp);
    ASSERT(stack != NULL);
    stack->Parameters.Others.Argument1 = PortStatus;

    status = IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }
    else
    {
        IoStatusBlock.Status = status;
    }
    status = IoStatusBlock.Status;
    return status;
}

NTSTATUS UsbResetPortRequest(PDEVICE_OBJECT DeviceObject) 
{
    PIRP Irp;
    NTSTATUS status;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION stack;
    PMIDI_DEVICE MidiDevice;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    KeInitializeEvent(&Event, NotificationEvent, 0);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_RESET_PORT, MidiDevice->NextLowerDeviceObject, NULL, 0, NULL, 0, TRUE, &Event, &IoStatusBlock);
    if (!Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
    if (status != STATUS_PENDING)
    {
        return status;
    }
    KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    return IoStatusBlock.Status;
}
