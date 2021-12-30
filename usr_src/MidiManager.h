/**********************************************************************************
    MidiStudio-2 Driver for Windows-10 x64 and Windows-11 x64. Version 1.0.
    Driver for USB MIDI-keyboard MidiStudio-2. The driver is designed for
    Windows-10 x64 and Windows-11 x64

    Copyright (C) 2021  Maxim Shershavikov

    This file is part of MidiStudio-2 Driver.

    MidiStudio-2 Driver is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MidiStudio-2 Driver is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
    Email m.shershavikov@yandex.ru

    To read a copy of the GNU General Public License in a file COPYING.txt,
    to do this, click the AbautProgram button.
**********************************************************************************/

#ifndef PCH_H
#define PCH_H

#define WIN32_LEAN_AND_MEAN            

#include <windows.h>
#include <mmiscapi.h>
#include <winioctl.h>
#include "Mmsystem.h"
#include "mmddk.h"
#include "usbioctl.h"
#include "usb.h"

#define IOCTL_INTUSB_GET_CONFIG_DESCRIPTOR 0x220000
#define IOCTL_INTUSB_RESET_DEVICE 0x220004
#define IOCTL_INTUSB_RESET_PIPE 0x220008

#define MAX_BUF_SIZE 256
#define MAX_MIDI_BUF_SIZE 127

struct CONFIG_DEVICE
{
    USB_CONFIGURATION_DESCRIPTOR UsbConfigurationDesc;
    USB_INTERFACE_DESCRIPTOR UsbInterfaceDesc;
    USB_ENDPOINT_DESCRIPTOR UsbEndpointDesc_1;
    USB_ENDPOINT_DESCRIPTOR UsbEndpointDesc_2;
    USB_ENDPOINT_DESCRIPTOR UsbEndpointDesc_3;
    USB_ENDPOINT_DESCRIPTOR UsbEndpointDesc_4;
};

struct LINC_DEVICE
{
    CHAR lattice_1;
    CHAR lattice_2;
    CHAR symbol;
    CHAR lattice_3;
    CHAR USB[3];
    CHAR lattice_4;
    CHAR Vid_7104_Pid_2202[17];
    CHAR lattice_5;
    CHAR rest[1000];
};

struct MIDI_INFO
{
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hFilePipe02 = INVALID_HANDLE_VALUE;
    BOOL ClouseThread = FALSE;
    HDRVR hDevice = 0;
    DWORD_PTR dwCallback = 0;
    DWORD_PTR dwUser = 0;
    DWORD_PTR DifferenceTime = 0;
    CHAR DriverFilePach[MAX_BUF_SIZE];
    CHAR DriverFilePachPipe02[MAX_BUF_SIZE];
    CHAR *ReadBuffer = new CHAR[MAX_BUF_SIZE];
    CHAR LastMessage[MAX_BUF_SIZE];
    CHAR NewMessage[MAX_BUF_SIZE];
    WORD *NewMessageWord = (WORD*)&NewMessage[1];
};

typedef MIDI_INFO* PMIDI_INFO;

DWORD_PTR dwCallback;
HMIDI hDevice;
DWORD_PTR dwUser;
DWORD CountMid = 0;
LPMIDIHDR exLpMidiHdr = nullptr;
BOOL MidiStarted = FALSE;
HANDLE hThread = INVALID_HANDLE_VALUE;
ULONG PortStatus = 0;
CONFIG_DEVICE ConfigDevice;
PMIDI_INFO MidiInfo = new MIDI_INFO;

extern "C" __declspec (dllexport) VOID modMessage(UINT wDevID, UINT wMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2);
extern "C" __declspec (dllexport) DWORD WINAPI DriverProc(DWORD_PTR dwDriverIdentifier, HDRVR hdrvr, UINT uMsg, LPARAM lParam1, LPARAM lParam2);
extern "C" __declspec (dllexport) DWORD WINAPI midMessage(UINT wDevID, UINT wMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2);

BOOL WINAPI ResetDevice(HANDLE hFile); 
BOOL WINAPI ResetPipe(HANDLE hFile); 
BOOL WINAPI GetConfigDescriptor(HANDLE hFile);
HANDLE WINAPI OpenDevice(PMIDI_INFO MidiInfo); 
DWORD WINAPI MidiMessageThread(LPVOID lpParameter);
LSTATUS WINAPI FindDriverPach(CHAR* DrvPach);
DWORD MidiGetDevCaps(LPMIDIINCAPSA lpMidiInCapsA);
DWORD MidiOpen(LPMIDIOPENDESC lpMidiDesc);
DWORD MidiPrepare(LPMIDIHDR lpMidiHdr);
DWORD MidiUnPrepare(LPMIDIHDR lpMidiHdr);
DWORD MidiAddBuffer(LPMIDIHDR lpMidiHdr);
DWORD MidiStart(DWORD_PTR dwParam1);
DWORD MidiStop();
DWORD MidiReset();

#endif //PCH_H
