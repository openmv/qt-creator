/* Copyright (C) 2026 OpenMV, LLC.
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

#ifndef OPENMVTHIRDPARTYSETTINGS_H
#define OPENMVTHIRDPARTYSETTINGS_H

#include <coreplugin/dialogs/ioptionspage.h>

// The "Third Party Repositories" preferences page (a new top-level "OpenMV"
// category in the Preferences dialog, present in the IDE and the viewer).
//
// Every action is immediate and file-driven - the page is only a front-end
// for the third-party/<vendor>/ folders (see openmvthirdparty.h):
//
//   - the repo list is scanRepos() rendered as a table
//   - the override-warnings panel is this run's merge records, dynamically
//     regenerated at every startup, so it is always visible which built-in
//     boards are currently overridden by which repo
//   - Install from URL... downloads a config.json and its payloads
//   - Remove deletes a user-installed repo (greyed out for repos shipped in
//     the read-only application directory)
//   - Check for Updates runs the same check-and-prompt flow the IDE runs in
//     the background at startup
//
// Repo changes take effect on restart; every mutating action offers one.
//
// A static instance in the .cpp self-registers the page (Qt Creator options
// page pattern).

namespace OpenMV {
namespace Internal {

class OpenMVThirdPartySettingsPage : public Core::IOptionsPage
{
public:
    OpenMVThirdPartySettingsPage();
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVTHIRDPARTYSETTINGS_H
