/* Copyright (C) 2023-2024 OpenMV, LLC.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Any redistribution, use, or modification in source or binary form
 *    is done solely for personal benefit and not for any commercial
 *    purpose or for monetary gain. For commercial licensing options,
 *    please contact openmv@openmv.io
 *
 * THIS SOFTWARE IS PROVIDED BY THE LICENSOR AND COPYRIGHT OWNER "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE LICENSOR OR COPYRIGHT
 * OWNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// https://support.microsoft.com/en-us/help/165721/how-to-ejecting-removable-media-in-windows-nt-windows-2000-windows-xp

#include <QtGlobal>

#if defined(Q_OS_WIN)

#include <windows.h>
#include <winioctl.h>
#include <tchar.h>
#include <stdio.h>

// Prototypes

BOOL EjectVolume(TCHAR cDriveLetter);

HANDLE OpenVolume(TCHAR cDriveLetter);
BOOL LockVolume(HANDLE hVolume);
BOOL DismountVolume(HANDLE hVolume);
BOOL PreventRemovalOfVolume(HANDLE hVolume, BOOL fPrevent);
BOOL AutoEjectVolume(HANDLE hVolume);
BOOL CloseVolume(HANDLE hVolume);

LPCTSTR szVolumeFormat = TEXT("\\\\.\\%c:");
LPCTSTR szRootFormat = TEXT("%c:\\");
LPCTSTR szErrorFormat = TEXT("Error %d: %s\n");

HANDLE OpenVolume(TCHAR cDriveLetter)
{
    HANDLE hVolume;
    UINT uDriveType;
    TCHAR szVolumeName[8];
    TCHAR szRootName[5];
    DWORD dwAccessFlags;

    wsprintf(szRootName, szRootFormat, cDriveLetter);

    uDriveType = GetDriveType(szRootName);
    switch(uDriveType) {
    case DRIVE_REMOVABLE:
        dwAccessFlags = GENERIC_READ | GENERIC_WRITE;
        break;
    case DRIVE_CDROM:
        dwAccessFlags = GENERIC_READ;
        break;
    default:
        return INVALID_HANDLE_VALUE;
    }

    wsprintf(szVolumeName, szVolumeFormat, cDriveLetter);

    hVolume = CreateFile(   szVolumeName,
                            dwAccessFlags,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL,
                            OPEN_EXISTING,
                            0,
                            NULL );

    return hVolume;
}

BOOL CloseVolume(HANDLE hVolume)
{
    return CloseHandle(hVolume);
}

#define LOCK_TIMEOUT        10000       // 10 Seconds
#define LOCK_RETRIES        20

BOOL LockVolume(HANDLE hVolume)
{
    DWORD dwBytesReturned;
    DWORD dwSleepAmount;
    int nTryCount;

    dwSleepAmount = LOCK_TIMEOUT / LOCK_RETRIES;

    // Do this in a loop until a timeout period has expired
    for (nTryCount = 0; nTryCount < LOCK_RETRIES; nTryCount++) {
        if (DeviceIoControl(hVolume,
                            FSCTL_LOCK_VOLUME,
                            NULL, 0,
                            NULL, 0,
                            &dwBytesReturned,
                            NULL))
            return TRUE;

        Sleep(dwSleepAmount);
    }

    return FALSE;
}

BOOL DismountVolume(HANDLE hVolume)
{
    DWORD dwBytesReturned;

    return DeviceIoControl( hVolume,
                            FSCTL_DISMOUNT_VOLUME,
                            NULL, 0,
                            NULL, 0,
                            &dwBytesReturned,
                            NULL);
}

BOOL PreventRemovalOfVolume(HANDLE hVolume, BOOL fPreventRemoval)
{
    DWORD dwBytesReturned;
    PREVENT_MEDIA_REMOVAL PMRBuffer;

    PMRBuffer.PreventMediaRemoval = fPreventRemoval;

    return DeviceIoControl( hVolume,
                            IOCTL_STORAGE_MEDIA_REMOVAL,
                            &PMRBuffer, sizeof(PREVENT_MEDIA_REMOVAL),
                            NULL, 0,
                            &dwBytesReturned,
                            NULL);
}

BOOL AutoEjectVolume(HANDLE hVolume)
{
    DWORD dwBytesReturned;

    return DeviceIoControl( hVolume,
                            IOCTL_STORAGE_EJECT_MEDIA,
                            NULL, 0,
                            NULL, 0,
                            &dwBytesReturned,
                            NULL);
}

BOOL EjectVolume(TCHAR cDriveLetter)
{
    HANDLE hVolume;

    BOOL fRemoveSafely = FALSE;
    BOOL fAutoEject = FALSE;

    // Open the volume.
    hVolume = OpenVolume(cDriveLetter);
    if (hVolume == INVALID_HANDLE_VALUE)
        return FALSE;

    // Lock and dismount the volume.
    if (LockVolume(hVolume) && DismountVolume(hVolume)) {
        fRemoveSafely = TRUE;

        // Set prevent removal to false and eject the volume.
        if (PreventRemovalOfVolume(hVolume, FALSE) &&
            AutoEjectVolume(hVolume))
            fAutoEject = TRUE;
    }

    // Close the volume so other processes can use the drive.
    if (!CloseVolume(hVolume))
        return FALSE;

    if (fAutoEject)
        (void) 0;
    else {
        if (fRemoveSafely)
            (void) 0;
    }

    return TRUE;
}

#endif

bool ejectVolume(wchar_t driveLetter)
{
#if defined(Q_OS_WIN)
    return EjectVolume(driveLetter);
#else
    Q_UNUSED(driveLetter)
    return false;
#endif
}

bool flushVolume(wchar_t driveLetter)
{
#if defined(Q_OS_WIN)
    wchar_t volPath[8];
    swprintf(volPath, _countof(volPath), L"\\\\.\\%c:", driveLetter);

    HANDLE hVol = CreateFileW(volPath, GENERIC_READ | GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, 0, nullptr);

    if (hVol == INVALID_HANDLE_VALUE) {
        return false;
    }

    bool ok = !!FlushFileBuffers(hVol);
    CloseHandle(hVol);
    return ok;
#else
    Q_UNUSED(driveLetter)
    return false;
#endif
}
