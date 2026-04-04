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

#else // !Q_OS_MAC

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
