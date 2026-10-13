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

#include "hardwaremonitor.h"

namespace OpenMV {
namespace Internal {

#if defined(Q_OS_MAC)

void HardwareMonitor::usbDeviceAdded(void *refCon, io_iterator_t iterator)
{
    // Drain the iterator to re-arm the notification
    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL)
        IOObjectRelease(service);

    HardwareMonitor *monitor = static_cast<HardwareMonitor *>(refCon);
    QMetaObject::invokeMethod(monitor, "hardwareEventDetected", Qt::QueuedConnection);
}

void HardwareMonitor::usbDeviceRemoved(void *refCon, io_iterator_t iterator)
{
    // Drain the iterator to re-arm the notification
    io_service_t service;
    while ((service = IOIteratorNext(iterator)) != IO_OBJECT_NULL)
        IOObjectRelease(service);

    HardwareMonitor *monitor = static_cast<HardwareMonitor *>(refCon);
    QMetaObject::invokeMethod(monitor, "hardwareEventDetected", Qt::QueuedConnection);
}

void HardwareMonitor::diskAppeared(DADiskRef /*disk*/, void *context)
{
    HardwareMonitor *monitor = static_cast<HardwareMonitor *>(context);
    QMetaObject::invokeMethod(monitor, "hardwareEventDetected", Qt::QueuedConnection);
}

void HardwareMonitor::diskDisappeared(DADiskRef /*disk*/, void *context)
{
    HardwareMonitor *monitor = static_cast<HardwareMonitor *>(context);
    QMetaObject::invokeMethod(monitor, "hardwareEventDetected", Qt::QueuedConnection);
}

HardwareMonitor::HardwareMonitor(QObject *parent)
    : QObject(parent)
    , m_notifyPort(nullptr)
    , m_usbAddedIter(IO_OBJECT_NULL)
    , m_usbRemovedIter(IO_OBJECT_NULL)
    , m_daSession(nullptr)
{
    // --- IOKit USB notifications ---
    m_notifyPort = IONotificationPortCreate(kIOMainPortDefault);

    if (m_notifyPort) {
        CFRunLoopSourceRef runLoopSource = IONotificationPortGetRunLoopSource(m_notifyPort);
        CFRunLoopAddSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopDefaultMode);

        // USB device added — each call consumes the dict, so allocate separately
        CFMutableDictionaryRef matchAdded = IOServiceMatching("IOUSBHostDevice");

        if (matchAdded) {
            kern_return_t kr = IOServiceAddMatchingNotification(
                m_notifyPort, kIOFirstMatchNotification,
                matchAdded, usbDeviceAdded, this, &m_usbAddedIter);

            if (kr == KERN_SUCCESS && m_usbAddedIter != IO_OBJECT_NULL) {
                // Drain to arm the notification
                io_service_t service;

                while ((service = IOIteratorNext(m_usbAddedIter)) != IO_OBJECT_NULL)
                    IOObjectRelease(service);
            }
        }

        // USB device removed
        CFMutableDictionaryRef matchRemoved = IOServiceMatching("IOUSBHostDevice");

        if (matchRemoved) {
            kern_return_t kr = IOServiceAddMatchingNotification(
                m_notifyPort, kIOTerminatedNotification,
                matchRemoved, usbDeviceRemoved, this, &m_usbRemovedIter);

            if (kr == KERN_SUCCESS && m_usbRemovedIter != IO_OBJECT_NULL) {
                io_service_t service;

                while ((service = IOIteratorNext(m_usbRemovedIter)) != IO_OBJECT_NULL)
                    IOObjectRelease(service);
            }
        }
    }

    // --- DiskArbitration disk mount/dismount ---
    m_daSession = DASessionCreate(kCFAllocatorDefault);

    if (m_daSession) {
        DASessionScheduleWithRunLoop(m_daSession, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
        DARegisterDiskAppearedCallback(m_daSession, nullptr, diskAppeared, this);
        DARegisterDiskDisappearedCallback(m_daSession, nullptr, diskDisappeared, this);
    }
}

HardwareMonitor::~HardwareMonitor()
{
    if (m_usbAddedIter != IO_OBJECT_NULL) {
        IOObjectRelease(m_usbAddedIter);
        m_usbAddedIter = IO_OBJECT_NULL;
    }

    if (m_usbRemovedIter != IO_OBJECT_NULL) {
        IOObjectRelease(m_usbRemovedIter);
        m_usbRemovedIter = IO_OBJECT_NULL;
    }

    if (m_daSession) {
        DAUnregisterCallback(m_daSession, reinterpret_cast<void *>(diskAppeared), this);
        DAUnregisterCallback(m_daSession, reinterpret_cast<void *>(diskDisappeared), this);
        DASessionUnscheduleFromRunLoop(m_daSession, CFRunLoopGetMain(), kCFRunLoopDefaultMode);
        CFRelease(m_daSession);
        m_daSession = nullptr;
    }

    if (m_notifyPort) {
        CFRunLoopSourceRef runLoopSource = IONotificationPortGetRunLoopSource(m_notifyPort);
        CFRunLoopRemoveSource(CFRunLoopGetMain(), runLoopSource, kCFRunLoopDefaultMode);
        IONotificationPortDestroy(m_notifyPort);
        m_notifyPort = nullptr;
    }
}

#elif defined(Q_OS_WIN)

const wchar_t *HardwareMonitor::kWindowClassName = L"OpenMVHardwareMonitorClass";

LRESULT __stdcall HardwareMonitor::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DEVICECHANGE) {
        if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE) {
            HardwareMonitor *monitor = reinterpret_cast<HardwareMonitor *>(
                GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (monitor) {
                QMetaObject::invokeMethod(monitor, "hardwareEventDetected",
                                          Qt::QueuedConnection);
            }
        }
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

HardwareMonitor::HardwareMonitor(QObject *parent)
    : QObject(parent)
    , m_hwnd(nullptr)
    , m_hDevNotify(nullptr)
{
    // Register a window class for our message-only window
    WNDCLASSW wc = {};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClassName;
    RegisterClassW(&wc);

    // Create message-only window (HWND_MESSAGE = no visible window)
    m_hwnd = CreateWindowExW(
        0, kWindowClassName, L"OpenMV HW Monitor",
        0, 0, 0, 0, 0,
        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);

    if (m_hwnd) {
        // Store 'this' pointer for the window proc
        SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

        // Register for USB device interface notifications
        // GUID_DEVINTERFACE_USB_DEVICE = {A5DCBF10-6530-11D2-901F-00C04FB951ED}
        DEV_BROADCAST_DEVICEINTERFACE_W filter = {};
        filter.dbcc_size = sizeof(filter);
        filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
        filter.dbcc_classguid = {0xA5DCBF10, 0x6530, 0x11D2,
                                 {0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED}};

        m_hDevNotify = RegisterDeviceNotificationW(
            m_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);
    }
}

HardwareMonitor::~HardwareMonitor()
{
    if (m_hDevNotify) {
        UnregisterDeviceNotification(m_hDevNotify);
        m_hDevNotify = nullptr;
    }

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
}

#elif defined(Q_OS_LINUX)

#include <unistd.h>

HardwareMonitor::HardwareMonitor(QObject *parent)
    : QObject(parent)
    , m_netlinkFd(-1)
    , m_netlinkNotifier(nullptr)
{
    m_netlinkFd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                         NETLINK_KOBJECT_UEVENT);
    if (m_netlinkFd < 0)
        return;

    struct sockaddr_nl addr = {};
    addr.nl_family = AF_NETLINK;
    addr.nl_pid = 0;
    // UEVENT_KERNEL_MCAST_GRP = 1 — receive kernel uevent broadcasts
    addr.nl_groups = 1;

    if (bind(m_netlinkFd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0) {
        close(m_netlinkFd);
        m_netlinkFd = -1;
        return;
    }

    m_netlinkNotifier = new QSocketNotifier(m_netlinkFd, QSocketNotifier::Read, this);
    connect(m_netlinkNotifier, &QSocketNotifier::activated,
            this, &HardwareMonitor::onNetlinkEvent);
}

void HardwareMonitor::onNetlinkEvent()
{
    // Drain all pending messages from the socket
    char buf[4096];
    while (recv(m_netlinkFd, buf, sizeof(buf), MSG_DONTWAIT) > 0) {
        // just drain
    }

    emit hardwareEventDetected();
}

HardwareMonitor::~HardwareMonitor()
{
    if (m_netlinkNotifier) {
        m_netlinkNotifier->setEnabled(false);
    }

    if (m_netlinkFd >= 0) {
        close(m_netlinkFd);
        m_netlinkFd = -1;
    }
}

#else // Unsupported platform

HardwareMonitor::HardwareMonitor(QObject *parent)
    : QObject(parent)
{
    // No hardware monitoring on this platform — scanning timers stay always-on.
}

HardwareMonitor::~HardwareMonitor()
{
}

#endif

} // namespace Internal
} // namespace OpenMV
