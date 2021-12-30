/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.
**********************************************************************************/

#include "MidiStudio2.h"

WMIGUIDREGINFO MidiBulkUsbGuidList[1] = { { &MIDI_USB_WMI_STD_DATA_GUID, 1, 0 } };
ULONG DebugLevel = 1;

NTSTATUS WmiSystemControlDevice(PDEVICE_OBJECT DeviceObject, PIRP Irp) 
{
    NTSTATUS status;
    SYSCTL_IRP_DISPOSITION IrpDisposition;
    PMIDI_DEVICE MidiDevice;
    PIO_STACK_LOCATION IoStackLocation;

    PAGED_CODE();

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    IoStackLocation = IoGetCurrentIrpStackLocation(Irp);

    if (MidiDevice->DeviceState == Removed)
    {
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_DELETE_PENDING;
        IofCompleteRequest(Irp, IO_NO_INCREMENT);
        status = STATUS_DELETE_PENDING;
    }
    else
    {
        UsbIoIncrement(MidiDevice);
        status = WmiSystemControl(&MidiDevice->WmiLibContext, DeviceObject, Irp, &IrpDisposition);
        if (IrpDisposition)
        {
            if (IrpDisposition == IrpNotCompleted)
            {
                IofCompleteRequest(Irp, IO_NO_INCREMENT);
            }
            else if (IrpDisposition == IrpForward || IrpDisposition == IrpNotWmi)
            {
                IoSkipCurrentIrpStackLocation(Irp);
                status = IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
            }
            else
            {
                ASSERT(FALSE);
                IoSkipCurrentIrpStackLocation(Irp);
                status = IofCallDriver(MidiDevice->NextLowerDeviceObject, Irp);
            }
        }
        UsbIoDecrement(MidiDevice);
    }
    return status;
}

NTSTATUS WmiLibContextInit(PMIDI_DEVICE MidiDevice)
{
    PAGED_CODE();

    MidiDevice->WmiLibContext.ExecuteWmiMethod = NULL;
    MidiDevice->WmiLibContext.WmiFunctionControl = NULL;

    MidiDevice->WmiLibContext.GuidCount = sizeof(MidiBulkUsbGuidList) / sizeof(WMIGUIDREGINFO);
    MidiDevice->WmiLibContext.GuidList = &MidiBulkUsbGuidList;
    MidiDevice->WmiLibContext.QueryWmiRegInfo = QueryWmiRegInfo;
    MidiDevice->WmiLibContext.QueryWmiDataBlock = QueryWmiDataBlock;
    MidiDevice->WmiLibContext.SetWmiDataBlock = SetWmiDataBlock;
    MidiDevice->WmiLibContext.SetWmiDataItem = SetWmiDataItem;
    return IoWMIRegistrationControl(MidiDevice->DeviceObject, WMIREG_ACTION_REGISTER);
}

NTSTATUS QueryWmiRegInfo(PDEVICE_OBJECT DeviceObject, PULONG RegFlags, PUNICODE_STRING InstanceName, PUNICODE_STRING* RegistryPath, PUNICODE_STRING MofResourceName, PDEVICE_OBJECT* Pdo)
{
    PMIDI_DEVICE MidiDevice;

    PAGED_CODE();

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    *RegFlags = WMIREG_FLAG_INSTANCE_PDO;
    *RegistryPath = &MidiRegistryPath;
    *Pdo = MidiDevice->PhysicalDeviceObject;
    RtlInitUnicodeString(MofResourceName, L"MofResourceName");
    return STATUS_SUCCESS;
}

NTSTATUS QueryWmiDataBlock(PDEVICE_OBJECT DeviceObject, PIRP Irp, ULONG GuidIndex, ULONG InstanceIndex, ULONG InstanceCount, PULONG InstanceLengthArray, ULONG BufferAvail, PUCHAR Buffer)
{
    ULONG size = 0;
    USHORT modelNameLen;
    NTSTATUS status;
    WCHAR modelName[] = L"Aishverya";

    PAGED_CODE();
    ASSERT((InstanceIndex == 0) && (InstanceCount == 1));

    modelNameLen = (wcslen(modelName) + 1) * sizeof(WCHAR);
    if (GuidIndex)
    {
        status = STATUS_WMI_GUID_NOT_FOUND;
    }
    else
    {
        size = sizeof(ULONG) + modelNameLen + sizeof(USHORT);
        if (BufferAvail >= size)
        {
            *(PULONG)Buffer = DebugLevel;
            Buffer += sizeof(ULONG);
            *((PUSHORT)Buffer) = modelNameLen;
            Buffer = (PUCHAR)Buffer + sizeof(USHORT);
            memcpy((PVOID)Buffer, (PVOID)modelName, modelNameLen);
            *InstanceLengthArray = size;
            status = STATUS_SUCCESS;
        }
        else
        {
            status = STATUS_BUFFER_TOO_SMALL;
        }
    }
    return WmiCompleteRequest(DeviceObject, Irp, status, size, IO_NO_INCREMENT);
}

NTSTATUS SetWmiDataBlock(PDEVICE_OBJECT DeviceObject, PIRP Irp, ULONG GuidIndex, ULONG InstanceIndex, ULONG BufferSize, PUCHAR Buffer)
{
    ULONG info = 0;
    NTSTATUS status;

    PAGED_CODE();

    if (GuidIndex)
    {
        status = STATUS_WMI_GUID_NOT_FOUND;
    }
    else if (BufferSize == sizeof(ULONG))
    {
        DebugLevel = *(PULONG)Buffer;
        status = STATUS_SUCCESS;
        info = sizeof(ULONG);
    }
    else
    {
        status = STATUS_INFO_LENGTH_MISMATCH;
    }
    return WmiCompleteRequest(DeviceObject, Irp, status, info, IO_NO_INCREMENT);
}

NTSTATUS SetWmiDataItem(PDEVICE_OBJECT DeviceObject, PIRP Irp, ULONG GuidIndex, ULONG InstanceIndex, ULONG DataItemId, ULONG BufferSize, PUCHAR Buffer)
{
    ULONG info = 0;
    NTSTATUS status;

    PAGED_CODE();

    if (GuidIndex)
    {
        status = STATUS_WMI_GUID_NOT_FOUND;
    }
    else if (DataItemId == 1)
    {
        if (BufferSize == sizeof(ULONG))
        {
            DebugLevel = *((PULONG)Buffer);
            status = STATUS_SUCCESS;
            info = sizeof(ULONG);
        }
        else
        {
            status = STATUS_INFO_LENGTH_MISMATCH;
        }
    }
    else
    {
        status = STATUS_WMI_READ_ONLY;
    }
    return WmiCompleteRequest(DeviceObject, Irp, status, info, IO_NO_INCREMENT);
}

NTSTATUS IoWmiDeRegCtrl(PMIDI_DEVICE MidiDevice)
{
    PAGED_CODE();
    return IoWMIRegistrationControl(MidiDevice->DeviceObject, WMIREG_ACTION_DEREGISTER);
}
