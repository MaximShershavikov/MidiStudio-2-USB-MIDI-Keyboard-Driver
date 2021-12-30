/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.
**********************************************************************************/

#include "MidiStudio2.h"

NTSTATUS CreateFileOfPipe(PDEVICE_OBJECT DeviceObject, PIRP Irp) 
{
    PUSBD_INTERFACE_INFORMATION UsbdInterfaceInformation;
    PMIDI_USB_PIPE_CONTEXT PipeContext;
    NTSTATUS status;
    PMIDI_DEVICE MidiDevice;
    PIO_STACK_LOCATION IoStackLocation;
    PFILE_OBJECT FileObject;

    PAGED_CODE();

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    FileObject = IoStackLocation->FileObject;

    if (MidiDevice->DeviceState == Working)
    {
        UsbdInterfaceInformation = MidiDevice->UsbdInterfaceInformation;
        if (UsbdInterfaceInformation)
        {
            if (FileObject)
            {
                FileObject->FsContext = NULL;
                if (FileObject->FileName.Length == 0)
                {
                    status = STATUS_SUCCESS;
                    InterlockedIncrement(&MidiDevice->OpenHandleCount);
                    if (MidiDevice->SSEnable)
                    {
                        UsbIoCancelSelectSuspend(MidiDevice);
                    }
                }
                else
                {
                    PipeContext = GetPipeNumFromString(DeviceObject, &FileObject->FileName);
                    status = STATUS_INVALID_PARAMETER;
                    if (PipeContext)
                    {
                        for (int i = 0; i < UsbdInterfaceInformation->NumberOfPipes; i++)
                        {
                            if (PipeContext == &MidiDevice->PipeContext[i])
                            {
                                status = STATUS_SUCCESS;
                                FileObject->FsContext = &UsbdInterfaceInformation->Pipes[i];
                                ASSERT(FileObject->FsContext);
                                PipeContext->PipeOpen = TRUE;
                                InterlockedIncrement(&MidiDevice->OpenHandleCount);
                                if (MidiDevice->SSEnable)
                                {
                                    UsbIoCancelSelectSuspend(MidiDevice);
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                status = STATUS_INVALID_PARAMETER;
            }
        }
        else
        {
            status = STATUS_INVALID_DEVICE_STATE;
        }
    }
    else
    {
        status = STATUS_INVALID_DEVICE_STATE;
    }
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = status;
    IofCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

NTSTATUS CloseFileOfPipe(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PMIDI_USB_PIPE_CONTEXT PipeContext = NULL;
    PIO_STACK_LOCATION IoStackLocation;
    PFILE_OBJECT FileObject;
    PMIDI_DEVICE MidiDevice;

    PAGED_CODE();

    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);
    FileObject = IoStackLocation->FileObject;
    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;

    if (FileObject && FileObject->FsContext)
    {
        if (FileObject->FileName.Length != 0)
        {
            PipeContext = GetPipeNumFromString(DeviceObject, &FileObject->FileName);
            if (PipeContext && PipeContext->PipeOpen)
            {
                PipeContext->PipeOpen = FALSE;
            }
        }
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IofCompleteRequest(Irp, IO_NO_INCREMENT);
    InterlockedDecrement(&MidiDevice->OpenHandleCount);
    return STATUS_SUCCESS;
}

PMIDI_USB_PIPE_CONTEXT GetPipeNumFromString(PDEVICE_OBJECT DeviceObject, PUNICODE_STRING FileName)
{
    ULONG uval = 0;
    ULONG nameLength;
    LONG ix;
    ULONG umultiplier;
    PMIDI_USB_PIPE_CONTEXT pipeContext = NULL;
    PMIDI_DEVICE MidiDevice;

    nameLength = (FileName->Length / sizeof(WCHAR));
    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;

    if (nameLength != 0)
    {
        ix = nameLength - 1;
        if (ix > -1)
        {
            while ((ix > -1) && ((FileName->Buffer[ix] < (WCHAR)'0') || (FileName->Buffer[ix] > (WCHAR)'9')))
            {
                ix--;
            }
            if (ix > -1)
            {
                umultiplier = 1;
                while ((ix > -1) && (FileName->Buffer[ix] >= (WCHAR)'0') && (FileName->Buffer[ix] <= (WCHAR)'9'))
                {
                    uval += (umultiplier * (ULONG)(FileName->Buffer[ix] - (WCHAR)'0'));
                    ix--;
                    umultiplier *= 10;
                }
                if (uval < 6 && MidiDevice->PipeContext)
                {
                    pipeContext = &MidiDevice->PipeContext[uval];
                }
            }
        }
    }
    return pipeContext;
}
