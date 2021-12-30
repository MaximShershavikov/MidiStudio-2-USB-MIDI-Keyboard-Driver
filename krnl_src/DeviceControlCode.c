/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.
**********************************************************************************/

#include "MidiStudio2.h"

NTSTATUS MidiBulkDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION stack;
    NTSTATUS status;
    PFILE_OBJECT FileObject;
    PUSB_CONFIGURATION_DESCRIPTOR UsbConfigurationDescriptor;
    ULONG length;
    PVOID lpBuffer;
    ULONG OutputBufferLength;
    ULONG InputBufferLength;
    ULONG len = 0;
    ULONG IoControlCode;
    PMIDI_DEVICE MidiDevice;
    ULONG PortStatus;

    stack = IoGetCurrentIrpStackLocation(Irp);
    IoControlCode = stack->Parameters.DeviceIoControl.IoControlCode;
    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    lpBuffer = Irp->AssociatedIrp.SystemBuffer;
    OutputBufferLength = stack->Parameters.DeviceIoControl.OutputBufferLength;
    InputBufferLength = stack->Parameters.DeviceIoControl.InputBufferLength;

    if (MidiDevice->DeviceState != Working)
    {
        Irp->IoStatus.Information = 0;
        status = STATUS_INVALID_DEVICE_STATE;
        Irp->IoStatus.Status = STATUS_INVALID_DEVICE_STATE;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }
    UsbIoIncrement(MidiDevice);
    if (MidiDevice->SSEnable)
    {
        KeWaitForSingleObject(&MidiDevice->NoIdleRequestPendingEvent, Executive, KernelMode, FALSE, NULL);
    }
    switch (IoControlCode)
    {
    case IOCTL_INTUSB_RESET_PIPE:
        FileObject = stack->FileObject;
        if (FileObject == NULL || FileObject->FsContext == NULL)
        {
            status = STATUS_INVALID_PARAMETER;
            break;
        }
        status = MidiBulkResetPipe(DeviceObject, (PUSBD_PIPE_INFORMATION)FileObject->FsContext);
        break;

    case IOCTL_INTUSB_GET_CONFIG_DESCRIPTOR:
        UsbConfigurationDescriptor = MidiDevice->UsbConfigDescriptorWithPipes;
        if (UsbConfigurationDescriptor)
        {
            length = UsbConfigurationDescriptor->wTotalLength;
            if (OutputBufferLength >= length)
            {
                memcpy(lpBuffer, UsbConfigurationDescriptor, length);
                len = length;
                status = STATUS_SUCCESS;
            }
            else status = STATUS_BUFFER_TOO_SMALL;
        }
        else status = STATUS_UNSUCCESSFUL;
        break;
    
    case IOCTL_INTUSB_RESET_DEVICE:
        status = MidiBulkResetDevice(DeviceObject, &PortStatus);
        length = 4;
        if (OutputBufferLength >= length)
        {
            memcpy(lpBuffer, &PortStatus, length);
            len = length;
            status = STATUS_SUCCESS;
        }
        else status = STATUS_BUFFER_TOO_SMALL;
        break;
    
    default: status = STATUS_INVALID_DEVICE_REQUEST;
    }
    Irp->IoStatus.Status = status;
    Irp->IoStatus.Information = len;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    UsbIoDecrement(MidiDevice);
    return status;
}

NTSTATUS MidiBulkResetDevice(PDEVICE_OBJECT DeviceObject, PULONG PortStatus)
{
    NTSTATUS status;

    *PortStatus = 0;
    status = UsbGetPortStatusRequest(DeviceObject, PortStatus);
    if ((NT_SUCCESS(status)) && (*PortStatus & USBD_PORT_ENABLED) == 0 && (*PortStatus & USBD_PORT_CONNECTED) != 0)
    {
        status = UsbResetPortRequest(DeviceObject);
    }
    return status;
}

NTSTATUS MidiBulkResetPipe(PDEVICE_OBJECT DeviceObject, PUSBD_PIPE_INFORMATION UsbdPipeInformation)
{
    NTSTATUS status;
    PURB urb;

    urb = (PURB)ExAllocatePoolWithTag(NonPagedPool, sizeof(struct _URB_PIPE_REQUEST), MIDISTUDIO_02_TAG);
    if (urb)
    {
        urb->UrbHeader.Length = (USHORT)sizeof(struct _URB_PIPE_REQUEST);
        urb->UrbHeader.Function = URB_FUNCTION_RESET_PIPE;
        urb->UrbPipeRequest.PipeHandle = UsbdPipeInformation->PipeHandle;
        status = UsbSubmitUrbRequest(DeviceObject, urb);
        ExFreePool(urb);
    }
    else
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (NT_SUCCESS(status))
    {
        status = STATUS_SUCCESS;
    }
    return status;
}
