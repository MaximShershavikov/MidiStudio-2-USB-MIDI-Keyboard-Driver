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

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_DETACH:
        OutputDebugStringA("MIDI_STUDIO_2: DLL_PROCESS_DETACH \n ");
        return TRUE;
    case DLL_PROCESS_ATTACH:
        OutputDebugStringA("MIDI_STUDIO_2: DLL_PROCESS_ATTACH \n ");
        break;
    case DLL_THREAD_ATTACH:
        break;
    case DLL_THREAD_DETACH:
        OutputDebugStringA("MIDI_STUDIO_2: DLL_THREAD_DETACH \n ");
        OutputDebugStringA("MIDI_STUDIO_2: DLL_PROCESS_DETACH \n ");
        return TRUE;
    default:
        return TRUE;
    }
    OutputDebugStringA("MIDI_STUDIO_2: DLL_THREAD_ATTACH \n ");
    OutputDebugStringA("MIDI_STUDIO_2: DLL_THREAD_DETACH \n ");
    OutputDebugStringA("MIDI_STUDIO_2: DLL_PROCESS_DETACH \n ");
    return TRUE;
}
