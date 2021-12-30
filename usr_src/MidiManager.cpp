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

#include "MidiManager.h"

VOID modMessage(UINT wDevID, UINT wMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    LPMIDIOUTCAPSA LpMidiOutCapsA;
    WCHAR Name[13] = L"MidiStudio-2";

    if (wMsg < MODM_PREPARE && wMsg != MODM_GETNUMDEVS && wMsg == MODM_GETDEVCAPS)
    {
        LpMidiOutCapsA = (LPMIDIOUTCAPSA)dwParam1;
        LpMidiOutCapsA->wMid = 4369;
        LpMidiOutCapsA->wPid = 4369;
        LpMidiOutCapsA->vDriverVersion = 153;
        memset(LpMidiOutCapsA->szPname, 0, MAXPNAMELEN);
        lstrcpy((LPWSTR)LpMidiOutCapsA->szPname, Name);
        LpMidiOutCapsA->dwSupport = 0;
    }
}

DWORD WINAPI midMessage(UINT wDevID, UINT wMsg, DWORD_PTR dwUser, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    switch (wMsg)
    {
    case MIDM_GETNUMDEVS:
        return MMSYSERR_ERROR;

    case MIDM_GETDEVCAPS:
        return MidiGetDevCaps((LPMIDIINCAPSA)dwParam1);

    case MIDM_OPEN:
        return MidiOpen((LPMIDIOPENDESC)dwParam1);

    case MIDM_CLOSE:
        return MMSYSERR_NOERROR;

    case MIDM_PREPARE:
        return MidiPrepare((LPMIDIHDR)dwParam1);

    case MIDM_UNPREPARE:
        return MidiUnPrepare((LPMIDIHDR)dwParam1);

    case MIDM_ADDBUFFER:
        return MidiAddBuffer((LPMIDIHDR)dwParam1);

    case MIDM_START:
        return MidiStart(dwParam1);

    case MIDM_STOP:
        return MidiStop();

    case MIDM_RESET:
        return MidiReset();

    default:
        return MMSYSERR_NOTSUPPORTED;
    }
}

DWORD MidiGetDevCaps(LPMIDIINCAPSA lpMidiInCapsA)
{
    CountMid++;
    WCHAR Name[13] = L"MidiStudio-2";

    lpMidiInCapsA->wMid = 4369;
    lpMidiInCapsA->wPid = 4369;
    lpMidiInCapsA->vDriverVersion = 153;
    memset(lpMidiInCapsA->szPname, 0, MAXPNAMELEN);
    lstrcpy((LPWSTR)lpMidiInCapsA->szPname, Name);
    lpMidiInCapsA->dwSupport = 0;

    return MMSYSERR_NOERROR;
}

DWORD MidiOpen(LPMIDIOPENDESC lpMidiDesc)
{
    CountMid++;

    dwCallback = lpMidiDesc->dwCallback;
    hDevice = lpMidiDesc->hMidi;
    dwUser = lpMidiDesc->dwInstance;
    exLpMidiHdr = nullptr;

    return MMSYSERR_NOERROR;
}

DWORD MidiPrepare(LPMIDIHDR lpMidiHdr)
{
    lpMidiHdr->dwFlags |= MHDR_PREPARED;
    lpMidiHdr->lpNext = 0;
    lpMidiHdr->dwBytesRecorded = 0;

    return MMSYSERR_NOERROR;
}

DWORD MidiUnPrepare(LPMIDIHDR lpMidiHdr)
{
    if ((lpMidiHdr->dwFlags & MHDR_PREPARED) == 0)
    {
        return MIDIERR_UNPREPARED;
    }
    if ((lpMidiHdr->dwFlags & MHDR_INQUEUE) != 0)
    {
        return MIDIERR_STILLPLAYING;
    }
    lpMidiHdr->dwFlags &= ~MHDR_PREPARED;

    return MMSYSERR_NOERROR;
}

DWORD MidiAddBuffer(LPMIDIHDR lpMidiHdr)
{
    LPMIDIHDR ptr;

    if ((lpMidiHdr->dwFlags & MHDR_PREPARED) != 0)
    {
        if (exLpMidiHdr)
        {
            for (ptr = exLpMidiHdr; ptr->lpNext != 0; ptr = ptr->lpNext) { ; }
            ptr->lpNext = lpMidiHdr;
        }
        else
        {
            exLpMidiHdr = lpMidiHdr;
        }
        return MMSYSERR_NOERROR;
    }
    else
    {
        return MIDIERR_UNPREPARED;
    }
}

DWORD MidiStart(DWORD_PTR dwParam1)
{
    HANDLE hFilePipe02;

    if (!MidiStarted)
    {
        MidiStarted = TRUE;
        hThread = 0;
        hFilePipe02 = OpenDevice(MidiInfo);
        if (hFilePipe02 != INVALID_HANDLE_VALUE)
        {
            if (!hThread)
            {
                MidiInfo->dwCallback = dwCallback;
                MidiInfo->hDevice = (HDRVR)hDevice;
                MidiInfo->dwUser = dwUser;
                MidiInfo->ClouseThread = FALSE;
                hThread = CreateThread(NULL, 4096, (LPTHREAD_START_ROUTINE)MidiMessageThread, MidiInfo, 0, (LPDWORD)&dwParam1);
                SetThreadPriority(hThread, THREAD_BASE_PRIORITY_LOWRT);
            }
        }
    }
    return MMSYSERR_NOERROR;
}

DWORD MidiStop()
{
    if (hThread != INVALID_HANDLE_VALUE)
    {
        MidiInfo->ClouseThread = TRUE;
        TerminateThread(hThread, 0);
        CloseHandle(hThread);
        hThread = INVALID_HANDLE_VALUE;
    }
    if (MidiInfo->hFilePipe02 != INVALID_HANDLE_VALUE)
    {
        ResetPipe(MidiInfo->hFilePipe02);
        ResetDevice(MidiInfo->hFilePipe02);
        ResetPipe(MidiInfo->hFilePipe02);
        MidiInfo->ClouseThread = TRUE;
        if (MidiInfo->hFilePipe02 != INVALID_HANDLE_VALUE)
        {
            CloseHandle(MidiInfo->hFilePipe02);
        }
        MidiInfo->hFilePipe02 = INVALID_HANDLE_VALUE;
    }
    MidiStarted = FALSE;
    return MMSYSERR_NOERROR;
}

DWORD MidiReset()
{
    LPMIDIHDR ptr;

    for (ptr = exLpMidiHdr; ptr->lpNext != 0; ptr = ptr->lpNext)
    {
        ptr->dwFlags &= ~MHDR_INQUEUE;
        ptr->dwFlags |= MHDR_DONE;
        DriverCallback(MidiInfo->dwCallback, DCB_FUNCTION, MidiInfo->hDevice, MIM_LONGDATA, MidiInfo->dwUser, (DWORD_PTR)ptr, MidiInfo->DifferenceTime);
    }
    if (ptr != nullptr)
    {
        ptr->dwFlags &= ~MHDR_INQUEUE;
        ptr->dwFlags |= MHDR_DONE;
        DriverCallback(MidiInfo->dwCallback, DCB_FUNCTION, MidiInfo->hDevice, MIM_LONGDATA, MidiInfo->dwUser, (DWORD_PTR)ptr, MidiInfo->DifferenceTime);
    }
    return MMSYSERR_NOERROR;
}

HANDLE WINAPI OpenDevice(PMIDI_INFO MidiInfo)
{
    HANDLE result;

    memset(&MidiInfo->DriverFilePach, 0, 256);
    memset(&MidiInfo->DriverFilePachPipe02, 0, 256);
    FindDriverPach(MidiInfo->DriverFilePach);
    MidiInfo->hFile = CreateFileA(MidiInfo->DriverFilePach, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    result = INVALID_HANDLE_VALUE;
    if (MidiInfo->hFile != INVALID_HANDLE_VALUE)
    {
        strcpy_s(MidiInfo->DriverFilePachPipe02, 256, MidiInfo->DriverFilePach);
        strcat_s(MidiInfo->DriverFilePachPipe02, "\\PIPE02");

        MidiInfo->hFilePipe02 = CreateFileA(MidiInfo->DriverFilePachPipe02, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (MidiInfo->hFilePipe02 == INVALID_HANDLE_VALUE)
        {
            CloseHandle(MidiInfo->hFile);
            return result;
        }
        ResetPipe(MidiInfo->hFilePipe02);
        ResetDevice(MidiInfo->hFile);
        GetConfigDescriptor(MidiInfo->hFile);
        ResetPipe(MidiInfo->hFilePipe02);
        
        CloseHandle(MidiInfo->hFile);
        MidiInfo->hFile = INVALID_HANDLE_VALUE;
        result = MidiInfo->hFilePipe02;
    }
    return result;
}

LSTATUS WINAPI FindDriverPach(CHAR* DrvPach)
{
    DWORD cSubKeys = 0;
    DWORD cbMaxSubKeyLen = 0;
    DWORD cValues = 0;
    DWORD cbMaxValueNameLen = 0;
    DWORD cbMaxValueLen = 0;
    LSTATUS status;
    DWORD count = 0;
    HKEY phkResult;
    CHAR Name[MAX_BUF_SIZE];
    CHAR usb[4];
    CHAR PidVid[18];
    CHAR DriverPach[MAX_BUF_SIZE] = "\\\\?\\";
    LINC_DEVICE* LincDev = new LINC_DEVICE;
    HANDLE hFile;

    hFile = INVALID_HANDLE_VALUE;

    status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\DeviceClasses\\{00873fdf-61a8-11d1-aa5e-00c04fb1728b}", 0, KEY_READ, &phkResult);
    if (status)
    {
        return status;
    }
    if (RegQueryInfoKeyA(phkResult, 0, 0, 0, &cSubKeys, &cbMaxSubKeyLen, 0, &cValues, &cbMaxValueNameLen, &cbMaxValueLen, 0, 0))
    {
        return RegCloseKey(phkResult);
    }

    while (hFile == INVALID_HANDLE_VALUE)
    {
        memset(Name, 0, cbMaxSubKeyLen);
        memset(LincDev, 0, sizeof(LINC_DEVICE));
        memset(&DriverPach[4], 0, MAX_BUF_SIZE - 4);
        memset(usb, 0, sizeof(usb));
        memset(PidVid, 0, sizeof(PidVid));

        status = RegEnumKeyA(phkResult, count++, Name, cbMaxSubKeyLen + 1);
        if (status == ERROR_NO_MORE_ITEMS)
        {
            break;
        }
        memcpy_s(LincDev, sizeof(LINC_DEVICE), Name, MAX_BUF_SIZE);

        strncat_s(usb, LincDev->USB, 3);
        if (strcmp(usb, "USB"))
        {
            continue;
        }
        strncat_s(PidVid, LincDev->Vid_7104_Pid_2202, 17);
        if (strcmp(PidVid, "VID_7104&PID_2202"))
        {
            continue;
        }
        strcat_s(DriverPach, LincDev->USB);
        hFile = CreateFileA(DriverPach, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    }

    if (hFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hFile);
        strcpy_s(DrvPach, MAX_BUF_SIZE, DriverPach);
    }

    return RegCloseKey(phkResult);
}

BOOL WINAPI GetConfigDescriptor(HANDLE hFile)
{
    BOOL result;
    DWORD lpBytesReturned = 0;

    memset(&ConfigDevice, 0, sizeof(CONFIG_DEVICE));

    result = -1;
    if (hFile != INVALID_HANDLE_VALUE)
    {
        result = DeviceIoControl(hFile, IOCTL_INTUSB_GET_CONFIG_DESCRIPTOR, NULL, 0, &ConfigDevice, sizeof(CONFIG_DEVICE), &lpBytesReturned, NULL);
    }
    return result;
}

BOOL WINAPI ResetDevice(HANDLE hFile)
{
    BOOL result;
    DWORD lpBytesReturned = 0;

    result = -1;
    if (hFile != INVALID_HANDLE_VALUE)
    {
        result = DeviceIoControl(hFile, IOCTL_INTUSB_RESET_DEVICE, NULL, 0, &PortStatus, sizeof(ULONG), &lpBytesReturned, NULL);
    }
    return result;
}

BOOL WINAPI ResetPipe(HANDLE hFile)
{
    BOOL result;
    DWORD lpBytesReturned = 0;

    result = -1;
    if (hFile != INVALID_HANDLE_VALUE)
    {
        result = DeviceIoControl(hFile, IOCTL_INTUSB_RESET_PIPE, NULL, 0, NULL, 0, &lpBytesReturned, NULL);
    }
    return result;
}

DWORD WINAPI MidiMessageThread(LPVOID lpParameter)
{
    DWORD status;
    HANDLE hFile;
    BOOL ReadFileDev;
    DWORD_PTR DifferenceTime;
    DWORD_PTR MidiMessage = 0;
    DWORD step;
    DWORD NumberOfBytesRead;
    DWORD TimeBegin;
    INT32 BytesRead;
    int count = 0;

    PMIDI_INFO MidiInfo = (PMIDI_INFO)lpParameter;

    TimeBegin = timeGetTime();
    memset(&MidiInfo->LastMessage, 0, MAX_BUF_SIZE);
    while (true)
    {
        while (!MidiInfo) {;}
        status = -1;
        if (MidiInfo->hFilePipe02 == INVALID_HANDLE_VALUE || MidiInfo->ClouseThread == TRUE)
        {
            break;
        }
        BytesRead = 0;
        memset(&MidiInfo->NewMessage, 0, MAX_BUF_SIZE);
        hFile = MidiInfo->hFilePipe02;
        if (hFile != INVALID_HANDLE_VALUE)
        {
            memset(MidiInfo->ReadBuffer, 0, sizeof(MidiInfo->ReadBuffer));
            ReadFileDev = ReadFile(hFile, MidiInfo->ReadBuffer, MAX_MIDI_BUF_SIZE, &NumberOfBytesRead, 0);
            if (NumberOfBytesRead < MAX_BUF_SIZE)
            {
                BytesRead = NumberOfBytesRead;
                memcpy_s(&MidiInfo->NewMessage, NumberOfBytesRead, MidiInfo->ReadBuffer, NumberOfBytesRead);
                status = ReadFileDev;
            }
            else status = -1;
        }
        if (MidiInfo->ClouseThread == TRUE || !BytesRead || BytesRead >= MAX_MIDI_BUF_SIZE)
        {
            break;
        }
        DifferenceTime = timeGetTime() - TimeBegin;
        MidiInfo->DifferenceTime = DifferenceTime;
        if (status != -1 && memcmp(&MidiInfo->LastMessage, &MidiInfo->NewMessage, BytesRead) && BytesRead / 4 > 0)
        {
            step = BytesRead / 4;
            while (step)
            {
                MidiMessage = MAKELONG(MidiInfo->NewMessageWord[count], MidiInfo->NewMessageWord[count+1]);
                DriverCallback(MidiInfo->dwCallback, DCB_FUNCTION, MidiInfo->hDevice, MIM_DATA, MidiInfo->dwUser, MidiMessage, DifferenceTime);
                count += 2;
                step--;
                MidiMessage = 0;
            }
            count = 0;
        }
        memcpy_s(&MidiInfo->LastMessage, BytesRead, &MidiInfo->NewMessage, BytesRead);
    }
    if (MidiInfo->hFilePipe02 != INVALID_HANDLE_VALUE)
    {
        CloseHandle(MidiInfo->hFilePipe02);
    }
    ExitThread(0);
}

DWORD WINAPI DriverProc(DWORD_PTR dwDriverIdentifier, HDRVR hdrvr, UINT uMsg, LPARAM lParam1, LPARAM lParam2)
{
    int result;

    if (uMsg > MCI_OPEN_DRIVER)
    {
        if (uMsg > MCI_STATUS)
        {
            if (uMsg != MCI_RESUME)
            {
                return DefDriverProc(dwDriverIdentifier, hdrvr, uMsg, lParam1, lParam2);
            }
        }
        else if (uMsg != MCI_STATUS)
        {
            switch (uMsg)
            {
            case MCI_CLOSE_DRIVER:
            case MCI_OPEN:
            case MCI_CLOSE:
            case MCI_PLAY:
            case MCI_STOP:
            case MCI_PAUSE:
            case MCI_INFO:
            case MCI_GETDEVCAPS:
            case MCI_SET:
            case MCI_RECORD:
                return 0;
            default:
                return DefDriverProc(dwDriverIdentifier, hdrvr, uMsg, lParam1, lParam2);
            }
        }
        return 0;
    }
    if (uMsg == MCI_OPEN_DRIVER)
    {
        return 0;
    }
    switch (uMsg)
    {
    case DRV_LOAD:
    case DRV_ENABLE:
    case DRV_OPEN:
    case DRV_CLOSE:
    case DRV_DISABLE:
    case DRV_FREE:
    case DRV_CONFIGURE:
    case DRV_QUERYCONFIGURE:
        result = 1;
        break;
    case DRV_INSTALL:
    case DRV_REMOVE:
        result = 2;
        break;
    default:
        return DefDriverProc(dwDriverIdentifier, hdrvr, uMsg, lParam1, lParam2);
    }
    return result;
}
