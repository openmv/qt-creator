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

#include "openmvaimode.h"
#include "openmvtr.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/icontext.h>
#include <utils/icon.h>
#include <utils/theme/theme.h>

namespace OpenMV {
namespace Internal {

OpenMVAIMode::OpenMVAIMode(QObject *parent) : Core::IMode(parent)
{
    QWidget *container = new QWidget;
    m_layout = new QVBoxLayout(container);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    setDisplayName(Tr::tr("AI"));
    // The ACP client's own chat icon, recoloured like the Edit mode's.
    setIcon(Utils::Icon::sideBarIcon(
        Utils::Icon({{Utils::FilePath::fromString(QStringLiteral(":/acpclient/images/sparklebubble.png")), Utils::Theme::IconsBaseColor}}),
        Utils::Icon({{Utils::FilePath::fromString(QStringLiteral(":/acpclient/images/sparklebubble.png")), Utils::Theme::IconsBaseColor}})));
    setPriority(Core::Constants::P_MODE_EDIT - 10); // right after Edit, before Settings
    setId(Utils::Id("OpenMV.AIMode"));
    setContext(Core::Context("OpenMV.AIMode"));
    setWidget(container);
}

OpenMVAIMode::~OpenMVAIMode()
{
    // Hand the chat back to its plugin before our container (which the mode
    // owns) is destroyed.
    if(m_chat)
    {
        m_layout->removeWidget(m_chat);
        m_chat->setParent(Q_NULLPTR);
    }
}

void OpenMVAIMode::setChatWidget(QWidget *chat)
{
    if(!chat)
    {
        QLabel *notice = new QLabel(Tr::tr("The agentic chat (ACP Client plugin) is not available."));
        notice->setAlignment(Qt::AlignCenter);
        m_layout->addWidget(notice);
        return;
    }

    m_chat = chat;
    m_layout->addWidget(chat);
    chat->show();
}

} // namespace Internal
} // namespace OpenMV
