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

#ifndef WIFISCAN_H
#define WIFISCAN_H

#include <QStringList>

namespace OpenMV {
namespace Internal {

// Best-effort scan for nearby WiFi SSIDs, sorted and de-duplicated (empty list on failure or if
// the host has no WiFi). Uses a native OS API where we have one (Windows WLAN) and falls back to a
// command-line tool otherwise. Intended to populate an editable SSID picker -- the user can always
// type a network this misses (hidden SSIDs, out-of-range, no WiFi adapter).
QStringList scanWifiNetworks();

// The SSID the host is currently connected to, or empty if not connected / unsupported. Windows only.
QString connectedWifiSsid();

// The saved password for this SSID from the host's WiFi credential store, or empty if unavailable.
// Windows only (netsh, for the current user's own profiles -- no elevation); always empty elsewhere,
// since macOS/Linux would trigger a Keychain/polkit auth prompt.
QString savedWifiPassword(const QString &ssid);

} // namespace Internal
} // namespace OpenMV

#endif // WIFISCAN_H
