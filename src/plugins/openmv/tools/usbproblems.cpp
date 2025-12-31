/* Copyright (C) 2023-2025 OpenMV, LLC.
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

#ifdef Q_OS_WIN

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifdef CALLBACK
#undef CALLBACK
#endif

#include <windows.h>

#ifndef CALLBACK
#define CALLBACK __stdcall
#endif

#include <cfgmgr32.h>
#include <setupapi.h>

#include <QByteArray>
#include <QList>

namespace OpenMV {
namespace Internal {

static QString getDevRegPropString(HDEVINFO h, SP_DEVINFO_DATA &dev, DWORD prop)
{
    DWORD regType = 0;
    DWORD size = 0;

    SetupDiGetDeviceRegistryPropertyW(h, &dev, prop, &regType, nullptr, 0, &size);
    if (!size) return QString();

    QByteArray buf;
    buf.resize(int(size));

    if (!SetupDiGetDeviceRegistryPropertyW(h, &dev, prop, &regType,
                                           reinterpret_cast<PBYTE>(buf.data()),
                                           DWORD(buf.size()), &size)) {
        return QString();
    }

    if (regType == REG_SZ) {
        return QString::fromWCharArray(reinterpret_cast<const wchar_t*>(buf.constData()));
    } else if (regType == REG_MULTI_SZ) {
        // Return first string (good enough for display)
        const wchar_t *p = reinterpret_cast<const wchar_t*>(buf.constData());
        return (p && *p) ? QString::fromWCharArray(p) : QString();
    }

    return QString();
}

static QString getDeviceInstanceId(DEVINST devInst)
{
    wchar_t id[MAX_DEVICE_ID_LEN] = {};

    if (CM_Get_Device_IDW(devInst, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
        return QString::fromWCharArray(id);
    }

    return QString();
}

static bool isUsbEnumerator(HDEVINFO h, SP_DEVINFO_DATA &dev)
{
    const QString enumerator = getDevRegPropString(h, dev, SPDRP_ENUMERATOR_NAME);
    return enumerator.compare(QStringLiteral("USB"), Qt::CaseInsensitive) == 0;
}

static QString bestDisplayName(HDEVINFO h, SP_DEVINFO_DATA &dev)
{
    QString name = getDevRegPropString(h, dev, SPDRP_FRIENDLYNAME);

    if (name.isEmpty()) {
        name = getDevRegPropString(h, dev, SPDRP_DEVICEDESC);
    }

    if (name.isEmpty()) {
        name = getDeviceInstanceId(dev.DevInst);
    }

    return name;
}

QStringList usbProblemDeviceNames()
{
    QStringList out;

    HDEVINFO h = SetupDiGetClassDevsW(nullptr, nullptr, nullptr,
                                      DIGCF_ALLCLASSES | DIGCF_PRESENT);

    if (h == INVALID_HANDLE_VALUE) {
        return out;
    }

    for (DWORD i = 0; ; ++i) {
        SP_DEVINFO_DATA dev;
        dev.cbSize = sizeof(dev);

        if (!SetupDiEnumDeviceInfo(h, i, &dev)) {
            if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
            continue;
        }

        // Only USB-enumerated present devices
        if (!isUsbEnumerator(h, dev)) {
            continue;
        }

        ULONG status = 0;
        ULONG problem = 0;
        const CONFIGRET cr = CM_Get_DevNode_Status(&status, &problem, dev.DevInst, 0);

        if (cr != CR_SUCCESS) {
            continue;
        }

        // Device Manager yellow triangle: non-zero problem code
        if (problem != 0) {
            const QString name = bestDisplayName(h, dev);
            if (!name.isEmpty() && !out.contains(name)) {
                out.append(name);
            }
        }
    }

    SetupDiDestroyDeviceInfoList(h);
    return out;
}

#else

QStringList usbProblemDeviceNames()
{
    return QStringList();
}

#endif

} // namespace Internal
} // namespace OpenMV
