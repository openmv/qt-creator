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

#ifndef USBPROBLEMS_H
#define USBPROBLEMS_H

#include <QStringList>

namespace OpenMV {
namespace Internal {

QStringList usbProblemDeviceNames();

// True on Apple Silicon Macs running macOS Ventura or later, where the
// "Allow accessories to connect" security setting can block DFU (and other
// USB accessory) connections with a system prompt. False on Intel Macs,
// older macOS, and on non-macOS platforms.
bool isMacAccessorySecurityLikelyToInterfere();

// True on macOS when the "Input Monitoring" privacy grant has NOT been
// given to this process (denied or never-asked). Child processes such as
// NXP's blhost/sdphost inherit the parent grant, so a denied state here
// means an RT1062 firmware update via SPSDK will fail with
// "UsbHidPeripheral() cannot open USB HID device". False on non-macOS.
bool isMacHidAccessDeniedForOpenMVIDE();

} // namespace Internal
} // namespace OpenMV

#endif // USBPROBLEMS_H
