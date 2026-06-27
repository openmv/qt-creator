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

#ifndef SETTINGSEDITOR_H
#define SETTINGSEDITOR_H

#include <QByteArray>
#include <QString>

#include <functional>

namespace OpenMV {
namespace Internal {

// Writes `data` to `path`, returning false (and setting *err) on failure. The plugin
// supplies one that also flushes the file + volume when the path is on the OpenMV Cam's
// mounted drive, so the cam's filesystem sees the change (same as Save Script -> main.py).
using ConfigFileWriter = std::function<bool(const QString &path, const QByteArray &data, QString *err)>;

// Opens a *.json config file (file dialog, defaulting to the camera drive), renders its
// "controls" as a GUI of standard Qt widgets, and writes edited values back on Save.
void settingsEditorAction(const QString &drivePath, const ConfigFileWriter &writer);

// Writes a generic starter config (a simple-camera-app scaffold that exercises every
// control type) to a user-chosen path, then opens it in the editor.
void createDefaultConfigAction(const QString &drivePath, const ConfigFileWriter &writer);

} // namespace Internal
} // namespace OpenMV

#endif // SETTINGSEDITOR_H
