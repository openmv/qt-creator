/* Copyright (C) 2023-2026 OpenMV, LLC.
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

#ifndef HARDWAREMONITOR_H
#define HARDWAREMONITOR_H

#ifdef _WIN32
#include <windows.h>
#include <dbt.h>
#endif

#include <QtCore>

#if defined(Q_OS_MAC)
#include <CoreFoundation/CoreFoundation.h>
#include <DiskArbitration/DiskArbitration.h>

#include <IOKit/IOKitLib.h>
#elif defined(Q_OS_LINUX)
#include <sys/socket.h>
#include <linux/netlink.h>
#endif

namespace OpenMV {
namespace Internal {

class HardwareMonitor : public QObject
{
    Q_OBJECT

public:
    explicit HardwareMonitor(QObject *parent = nullptr);
    ~HardwareMonitor();

signals:
    void hardwareEventDetected();

private:
#if defined(Q_OS_MAC)
    IONotificationPortRef m_notifyPort;
    io_iterator_t m_usbAddedIter;
    io_iterator_t m_usbRemovedIter;
    DASessionRef m_daSession;

    static void usbDeviceAdded(void *refCon, io_iterator_t iterator);
    static void usbDeviceRemoved(void *refCon, io_iterator_t iterator);
    static void diskAppeared(DADiskRef disk, void *context);
    static void diskDisappeared(DADiskRef disk, void *context);
#elif defined(Q_OS_WIN)
    HWND m_hwnd;
    HDEVNOTIFY m_hDevNotify;

    static LRESULT __stdcall windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static const wchar_t *kWindowClassName;
#elif defined(Q_OS_LINUX)
    int m_netlinkFd;
    QSocketNotifier *m_netlinkNotifier;

    void onNetlinkEvent();
#endif
};

} // namespace Internal
} // namespace OpenMV

#endif // HARDWAREMONITOR_H
