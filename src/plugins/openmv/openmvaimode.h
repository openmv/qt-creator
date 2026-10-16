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

#ifndef OPENMVAIMODE_H
#define OPENMVAIMODE_H

#include <QtCore>
#include <QtWidgets>

#include <coreplugin/imode.h>

namespace OpenMV {
namespace Internal {

// The AI mode: the agentic chat (ACP) fills the editor's place while the
// Frame Buffer and the panes stay where they are. Sits between Edit and
// Settings on the mode strip.
class OpenMVAIMode : public Core::IMode
{
    Q_OBJECT

public:

    explicit OpenMVAIMode(QObject *parent = Q_NULLPTR);
    ~OpenMVAIMode();

    // Hosts `chat` (not owned: it is handed back, parentless, when the mode
    // goes away, so its plugin can delete it). A null chat shows a notice.
    void setChatWidget(QWidget *chat);

private:

    QVBoxLayout *m_layout;
    QPointer<QWidget> m_chat;
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVAIMODE_H
