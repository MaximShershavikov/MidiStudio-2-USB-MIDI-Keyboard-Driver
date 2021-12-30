/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.
**********************************************************************************/

#include "MidiStudio2.h"

NTSTATUS ReadMidiData(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION IoStackLocation;
    PIO_STACK_LOCATION NextIoStackLocation;
    BOOLEAN read;
    NTSTATUS status;
    ULONG MaxLength = 0;
    PURB urb = NULL;
    ULONG_PTR VirtualAddress;
    ULONG UrbFlag;
    PMDL Mdl = NULL;
    PUSBD_PIPE_INFORMATION UsbPipeInformation;
    PFILE_OBJECT FileObject;
    ULONG StageLength;
    PMIDI_DEVICE MidiDevice;
    PMIDIUSB_RW_CONTEXT MidiUsbRwContext = NULL;
    ULONG PortStatus;

    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    FileObject = IoStackLocation->FileObject;
    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;

    if (MidiDevice->DeviceState != Working)
    {
        status = STATUS_INVALID_DEVICE_STATE;
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }
    if (MidiDevice->SSEnable)
    {
        KeWaitForSingleObject(&MidiDevice->NoIdleRequestPendingEvent, Executive, KernelMode, FALSE, NULL);
    }
    if (FileObject && FileObject->FsContext)
    {
        UsbPipeInformation = FileObject->FsContext;
        if ((UsbPipeInformation->PipeType != UsbdPipeTypeBulk) && (UsbPipeInformation->PipeType != UsbdPipeTypeInterrupt))
        {
            status = STATUS_INVALID_HANDLE;
            Irp->IoStatus.Status = status;
            Irp->IoStatus.Information = 0;
            IofCompleteRequest(Irp, IO_NO_INCREMENT);
            return status;
        }
    }
    else
    {
        status = STATUS_INVALID_HANDLE;
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }
    MidiUsbRwContext = (PMIDIUSB_RW_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(MIDIUSB_RW_CONTEXT), MIDISTUDIO_02_TAG);
    if (!MidiUsbRwContext)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }
    if (Irp->MdlAddress)
    {
        MaxLength = MmGetMdlByteCount(Irp->MdlAddress);
    }
    if (MaxLength > (64 * 1024))
    {
        status = STATUS_INVALID_PARAMETER;
        ExFreePool(MidiUsbRwContext);
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }
    if (!MaxLength)
    {
        status = STATUS_SUCCESS;
        ExFreePool(MidiUsbRwContext);
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }
    UrbFlag = USBD_SHORT_TRANSFER_OK;
    VirtualAddress = (ULONG_PTR)MmGetMdlVirtualAddress(Irp->MdlAddress);
    UrbFlag |= USBD_TRANSFER_DIRECTION_IN;
    if (MaxLength > 256)
    {
        StageLength = 256;
    }
    else
    {
        StageLength = MaxLength;
    }
    Mdl = IoAllocateMdl((PVOID)VirtualAddress, MaxLength, FALSE, FALSE, NULL);
    if (!Mdl)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        ExFreePool(MidiUsbRwContext);
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }
    IoBuildPartialMdl(Irp->MdlAddress, Mdl, (PVOID)VirtualAddress, StageLength);
    urb = (PURB)ExAllocatePoolWithTag(NonPagedPool, sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER), MIDISTUDIO_02_TAG);
    if (!urb)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        ExFreePool(MidiUsbRwContext);
        IoFreeMdl(Mdl);
        Irp->IoStatus.Status = status;
        Irp->IoStatus.Information = 0;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
        return status;
    }
    UsbBuildInterruptOrBulkTransferRequest(
        urb,
        sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
        UsbPipeInformation->PipeHandle,
        NULL,
        Mdl,
        StageLength,
        UrbFlag,
        NULL);

    MidiUsbRwContext->Urb = urb;
    MidiUsbRwContext->Mdl = Mdl;
    MidiUsbRwContext->Length = MaxLength - StageLength;
    MidiUsbRwContext->Numxfer = 0;
    MidiUsbRwContext->VirtualAddress = VirtualAddress + StageLength;
    MidiUsbRwContext->MidiDevice = MidiDevice;

    NextIoStackLocation = IoGetNextIrpStackLocation(Irp);
    NextIoStackLocation->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    NextIoStackLocation->Parameters.Others.Argument1 = (PVOID)urb;
    NextIoStackLocation->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    IoSetCompletionRoutine(Irp, (PIO_COMPLETION_ROUTINE)WaitingIoDataAndCompletion, MidiUsbRwContext, TRUE, TRUE, TRUE);

    IoMarkIrpPending(Irp);
    UsbIoIncrement(MidiDevice);
    status = IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);

    if (!NT_SUCCESS(status))
    {
        if (status != STATUS_CANCELLED && status != STATUS_DEVICE_NOT_CONNECTED)
        {
            status = MidiBulkResetPipe(DeviceObject, UsbPipeInformation);
            if (!NT_SUCCESS(status))
            {
                MidiBulkResetDevice(DeviceObject, &PortStatus);
            }
        }
    }
    return STATUS_PENDING;
}

NTSTATUS WaitingIoDataAndCompletion(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
    NTSTATUS status;
    PIO_STACK_LOCATION NextIoStackLocation;
    ULONG StageLength;
    PMIDIUSB_RW_CONTEXT MidiUsbRwContext;

    MidiUsbRwContext = (PMIDIUSB_RW_CONTEXT)Context;
    status = Irp->IoStatus.Status;
    UNREFERENCED_PARAMETER(DeviceObject);
    if (NT_SUCCESS(status))
    {
        if (MidiUsbRwContext)
        {
            MidiUsbRwContext->Numxfer += MidiUsbRwContext->Urb->UrbBulkOrInterruptTransfer.TransferBufferLength;
            if (MidiUsbRwContext->Length)
            {
                if (MidiUsbRwContext->Length > 256)
                {
                    StageLength = 256;
                }
                else
                {
                    StageLength = MidiUsbRwContext->Length;
                }
                if ((MidiUsbRwContext->Mdl->MdlFlags & MDL_PARTIAL_HAS_BEEN_MAPPED) != 0)
                {
                    MmUnmapLockedPages(MidiUsbRwContext->Mdl->MappedSystemVa, MidiUsbRwContext->Mdl);
                }
                IoBuildPartialMdl(Irp->MdlAddress, MidiUsbRwContext->Mdl, (PVOID)MidiUsbRwContext->VirtualAddress, StageLength);

                MidiUsbRwContext->Urb->UrbBulkOrInterruptTransfer.TransferBufferLength = StageLength;
                MidiUsbRwContext->VirtualAddress += StageLength;
                MidiUsbRwContext->Length -= StageLength;
                NextIoStackLocation = IoGetNextIrpStackLocation(Irp);
                NextIoStackLocation->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
                NextIoStackLocation->Parameters.Others.Argument1 = MidiUsbRwContext->Urb;
                NextIoStackLocation->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;

                IoSetCompletionRoutine(Irp, WaitingIoDataAndCompletion, MidiUsbRwContext, TRUE, TRUE, TRUE);
                IofCallDriver(MidiUsbRwContext->MidiDevice->NextLowerDeviceObject, Irp);
                return STATUS_MORE_PROCESSING_REQUIRED;
            }
            else
            {
                Irp->IoStatus.Information = MidiUsbRwContext->Numxfer;
            }
        }
    }

    if (MidiUsbRwContext)
    {
        UsbIoDecrement(MidiUsbRwContext->MidiDevice);
        ExFreePool(MidiUsbRwContext->Urb);
        IoFreeMdl(MidiUsbRwContext->Mdl);
        ExFreePool(MidiUsbRwContext);
    }
    return status;
}
