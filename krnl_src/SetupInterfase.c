/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.
**********************************************************************************/

#include "MidiStudio2.h"

NTSTATUS DeviceDescriptorRequest(PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS status;
    PURB urb = NULL;
    PUSB_DEVICE_DESCRIPTOR UsbDeviseDescriptor = NULL;
    PMIDI_DEVICE MidiDevice;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    urb = (PURB)ExAllocatePoolWithTag(NonPagedPool, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST), MIDISTUDIO_02_TAG);
    if (!urb)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    UsbDeviseDescriptor = (PUSB_DEVICE_DESCRIPTOR)ExAllocatePoolWithTag(NonPagedPool, sizeof(USB_DEVICE_DESCRIPTOR), MIDISTUDIO_02_TAG);
    if (!UsbDeviseDescriptor)
    {
        ExFreePool(urb);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    UsbBuildGetDescriptorRequest(
        urb,
        sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
        USB_DEVICE_DESCRIPTOR_TYPE,
        0,
        0,
        UsbDeviseDescriptor,
        NULL,
        sizeof(USB_DEVICE_DESCRIPTOR),
        NULL);

    status = UsbSubmitUrbRequest(DeviceObject, urb);
    if (NT_SUCCESS(status))
    {
        ASSERT(UsbDeviseDescriptor->bNumConfigurations);
        MidiDevice->UsbDeviseDescriptor = UsbDeviseDescriptor;
        status = ConfigurationDescriptorWithPipeRequest(DeviceObject);
    }
    ExFreePool(urb);
    return status;
}

NTSTATUS ConfigurationDescriptorWithPipeRequest(PDEVICE_OBJECT DeviceObject) 
{
    SHORT TotalLength;
    NTSTATUS status;
    PMIDI_DEVICE MidiDevice;
    PURB urb = NULL;
    PUSB_CONFIGURATION_DESCRIPTOR UsbConfigurationDescriptor = NULL;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    urb = (PURB)ExAllocatePoolWithTag(NonPagedPool, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST), MIDISTUDIO_02_TAG);
    if (!urb)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    UsbConfigurationDescriptor = (PUSB_CONFIGURATION_DESCRIPTOR)ExAllocatePoolWithTag(NonPagedPool, sizeof(USB_CONFIGURATION_DESCRIPTOR), MIDISTUDIO_02_TAG);
    if (!UsbConfigurationDescriptor)
    {
        if (urb)
        {
            ExFreePool(urb);
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    UsbBuildGetDescriptorRequest(
        urb,
        (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
        USB_CONFIGURATION_DESCRIPTOR_TYPE,
        0,
        0,
        UsbConfigurationDescriptor,
        NULL,
        sizeof(USB_CONFIGURATION_DESCRIPTOR),
        NULL);

    status = UsbSubmitUrbRequest(DeviceObject, urb);
    if (!NT_SUCCESS(status))
    {
        if (urb)
        {
            ExFreePool(urb);
        }
        if (UsbConfigurationDescriptor)
        {
            ExFreePool(UsbConfigurationDescriptor);
        }
        return status;
    }
    TotalLength = UsbConfigurationDescriptor->wTotalLength;
    ExFreePool(UsbConfigurationDescriptor);
    UsbConfigurationDescriptor = NULL;
    UsbConfigurationDescriptor = (PUSB_CONFIGURATION_DESCRIPTOR)ExAllocatePoolWithTag(NonPagedPool, TotalLength, MIDISTUDIO_02_TAG);
    if (UsbConfigurationDescriptor)
    {
        UsbBuildGetDescriptorRequest(
            urb,
            (USHORT)sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
            USB_CONFIGURATION_DESCRIPTOR_TYPE,
            0,
            0,
            UsbConfigurationDescriptor,
            NULL,
            TotalLength,
            NULL);

        status = UsbSubmitUrbRequest(DeviceObject, urb);
        if (NT_SUCCESS(status))
        {
            MidiDevice->UsbConfigDescriptorWithPipes = UsbConfigurationDescriptor;
            if (UsbConfigurationDescriptor->bmAttributes & REMOTE_WAKEUP_MASK)
            {
                MidiDevice->WaitWakeEnable = 1;
            }
            else
            {
                MidiDevice->WaitWakeEnable = 0;
            }
            MidiDevice->ConfigDevicePtr = UsbConfigurationDescriptor;
            status = SelectInterfecesOfMidiBulkDevice(DeviceObject, UsbConfigurationDescriptor);
        }
    }
    else
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (urb)
    {
        ExFreePool(urb);
    }
    return status;
}

NTSTATUS SelectInterfecesOfMidiBulkDevice(PDEVICE_OBJECT DeviceObject, PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor)
{
    LONG NumInterfaces;
    PUSBD_INTERFACE_LIST_ENTRY InterfaceList;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    PURB urb;
    USHORT InterfaceLength = 0;
    ULONG NumPipe;
    DWORD count = 0;
    NTSTATUS status;
    LONG NextInterfaceNumber = 0;
    PMIDI_DEVICE MidiDevice;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    NumInterfaces = ConfigurationDescriptor->bNumInterfaces;
    InterfaceList = (PUSBD_INTERFACE_LIST_ENTRY)ExAllocatePoolWithTag(NonPagedPool, sizeof(USBD_INTERFACE_LIST_ENTRY) * (NumInterfaces + 1), MIDISTUDIO_02_TAG);

    if (!InterfaceList)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (NumInterfaces > 0)
    {
        do
        {
            InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(ConfigurationDescriptor, (PVOID)ConfigurationDescriptor, NextInterfaceNumber, 0, -1, -1, -1);
            if (InterfaceDescriptor)
            {
                InterfaceList[count].Interface = NULL;
                InterfaceList[count].InterfaceDescriptor = InterfaceDescriptor;
                count++;
            }
            NextInterfaceNumber++;
        } while (count < NumInterfaces);
    }
    InterfaceList[count].InterfaceDescriptor = NULL;
    InterfaceList[count].Interface = NULL;
    urb = USBD_CreateConfigurationRequestEx(ConfigurationDescriptor, InterfaceList);
    if (!urb)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        if (InterfaceList)
        {
            ExFreePool(InterfaceList);
        }
        if (urb)
        {
            ExFreePool(urb);
        }
        return status;
    }
    status = UsbSubmitUrbRequest(DeviceObject, urb);
    if (!NT_SUCCESS(status))
    {
        if (InterfaceList)
        {
            ExFreePool(InterfaceList);
        }
        if (urb)
        {
            ExFreePool(urb);
        }
        return status;
    }
    InterfaceLength = urb->UrbSelectConfiguration.Interface.Length;
    MidiDevice->UsbdInterfaceInformation = (PUSBD_INTERFACE_INFORMATION)ExAllocatePoolWithTag(NonPagedPool, InterfaceLength, MIDISTUDIO_02_TAG);
    if (MidiDevice->UsbdInterfaceInformation)
    {
        memcpy(MidiDevice->UsbdInterfaceInformation, &urb->UrbSelectConfiguration.Interface, InterfaceLength);
        MidiDevice->PtrInterfaceInfo = MidiDevice->UsbdInterfaceInformation;
    }
    else
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    NumPipe = urb->UrbSelectConfiguration.Interface.NumberOfPipes;
    if (!NumPipe)
    {
        if (InterfaceList)
        {
            ExFreePool(InterfaceList);
        }
        if (urb)
        {
            ExFreePool(urb);
        }
        return status;
    }
    MidiDevice->PipeContext = (PMIDI_USB_PIPE_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, NumPipe * sizeof(MIDI_USB_PIPE_CONTEXT), MIDISTUDIO_02_TAG);
    if (MidiDevice->PipeContext)
    {
        for (int i = 0; i < NumPipe; i++)
        {
            MidiDevice->PipeContext[i].PipeOpen = FALSE;
        }
    }
    else
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }

    if (InterfaceList)
    {
        ExFreePool(InterfaceList);
    }
    if (urb)
    {
        ExFreePool(urb);
    }
    return status;
}

NTSTATUS CansellPipesOfMidiBulkDevice(PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS status;
    PMIDI_DEVICE MidiDevice;
    PMIDI_USB_PIPE_CONTEXT PipeContext;
    PUSBD_INTERFACE_INFORMATION UsbdInterfaceInformation;
    PURB urb;

    MidiDevice = (PMIDI_DEVICE)DeviceObject->DeviceExtension;
    PipeContext = MidiDevice->PipeContext;
    UsbdInterfaceInformation = MidiDevice->UsbdInterfaceInformation;

    if (!UsbdInterfaceInformation || !PipeContext || !UsbdInterfaceInformation->NumberOfPipes)
    {
        return STATUS_SUCCESS;
    }

    for (int i = 0; i < UsbdInterfaceInformation->NumberOfPipes; i++)
    {
        if (PipeContext[i].PipeOpen)
        {
            urb = (PURB)ExAllocatePoolWithTag(NonPagedPool, sizeof(struct _URB_PIPE_REQUEST), MIDISTUDIO_02_TAG);
            if (urb)
            {
                urb->UrbHeader.Length = sizeof(struct _URB_PIPE_REQUEST);
                urb->UrbHeader.Function = URB_FUNCTION_ABORT_PIPE;
                urb->UrbPipeRequest.PipeHandle = UsbdInterfaceInformation->Pipes[i].PipeHandle;
                status = UsbSubmitUrbRequest(DeviceObject, urb);
                ExFreePool(urb);
            }
            else
            {
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            if (NT_SUCCESS(status))
            {
                PipeContext[i].PipeOpen = FALSE;
            }
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS DeconfigureMidiBulkDevice(PDEVICE_OBJECT DeviceObject)
{
    PURB urb;
    NTSTATUS status;

    urb = (PURB)ExAllocatePoolWithTag(NonPagedPool, sizeof(struct _URB_SELECT_CONFIGURATION), MIDISTUDIO_02_TAG);

    if (!urb)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    UsbBuildSelectConfigurationRequest(urb, (USHORT)sizeof(struct _URB_SELECT_CONFIGURATION), NULL);
    status = UsbSubmitUrbRequest(DeviceObject, urb);
    ExFreePool(urb);
    return status;
}
