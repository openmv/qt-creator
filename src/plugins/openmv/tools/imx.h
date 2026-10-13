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

#ifndef IMX_H
#define IMX_H

#include <QList>
#include <QJsonObject>
#include <QPair>

#include "openmvromfs.h"

namespace Utils { class Process; }

namespace OpenMV {
namespace Internal {

QList<QPair<int, int> > imxVidPidList(const QJsonDocument &settings, bool spd_host = true, bool bl_host = true);
// Returns PID/VID of SPD and BL bootloaders on the system.
QStringList imxGetAllDevices(const QJsonDocument &settings, bool spd_host = true, bool bl_host = true);
bool imxGetDevice(QJsonObject &obj);

// Pre-armed detection of the mboot/blhost bootloader device. The SBL only holds
// its USB device for ~1s after a reset before jumping to the app, and importing
// spsdk to run a probe takes seconds -- too slow on a loaded host. So a resident
// python process imports spsdk up front, prints READY, then hot-loops
// MbootUSBInterface.scan() and claims the device (get_property) the instant it
// appears. Arm it (blocks until READY or arm-timeout) BEFORE the reset/jump that
// makes the device appear, then await the result.
//
// pidvidKey is the bootloaderSettings key holding the target's "VID,PID"
// (e.g. "blhost_pidvid"). mode is "claim" (scan+open+get_property, validates the
// SBL) or "wait" (scan only, for a flashloader that holds once up). Returns the
// running process (caller passes it to imxAwaitCatcher) or nullptr if python/spsdk
// couldn't be resolved, the process never armed, or *canceled went true during
// the (multi-second) spsdk import. Pass the cancel flag so the user can bail
// during arming BEFORE the board is reset; check it at the call site and skip
// the reset when set.
Utils::Process *imxArmCatcher(const QJsonObject &obj, const QString &pidvidKey,
                              const char *mode, int timeoutS, const bool *canceled = nullptr);
// Pumps events until the catcher exits or *canceled goes true (kills it then).
// Deletes the process. Returns true only on a clean claim/find (exit 0).
bool imxAwaitCatcher(Utils::Process *proc, const bool *canceled);
bool imxDownloadBootloaderAndFirmware(QJsonObject &obj, bool forceFlashFSErase, bool justEraseFlashFs, OpenMVROMFSAccess romfsAccess);
bool imxDownloadFirmware(QJsonObject &obj, bool forceFlashFSErase, bool justEraseFlashFs, OpenMVROMFSAccess romfsAccess);

} // namespace Internal
} // namespace OpenMV

#endif // IMX_H
