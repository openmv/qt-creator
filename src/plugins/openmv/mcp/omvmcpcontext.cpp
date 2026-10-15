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

#include "omvmcpcontext.h"

#include <utils/qtcassert.h>

namespace OpenMV {
namespace Internal {
namespace OMVMCP {

// GUI-thread only, so a plain static is the whole "thread local".
static Call *s_currentCall = Q_NULLPTR;

QJsonObject Error::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("code"), code);
    o.insert(QStringLiteral("site"), site);
    o.insert(QStringLiteral("level"), level);
    if(!title.isEmpty()) o.insert(QStringLiteral("title"), title);
    if(!text.isEmpty()) o.insert(QStringLiteral("text"), text);
    if(!detailedText.isEmpty()) o.insert(QStringLiteral("detailedText"), detailedText);
    return o;
}

bool Call::supportsElicitation() const
{
    // The client must have advertised form elicitation in its capabilities,
    // and we must hold the tool handle (ToolInterfaceCallback tools) to send
    // the request through.
    if(!tool) return false;
    const auto &elicitation = tool->clientCapabilities().elicitation();
    return elicitation && elicitation->form();
}

bool Call::hasErrors(int minLevel) const
{
    for(const Error &e : errors)
    {
        if(e.level >= minLevel) return true;
    }

    return false;
}

void Call::note(const QString &line)
{
    log.append(line);
}

void Call::fail(const Error &e)
{
    errors.append(e);
    log.append(QStringLiteral("[%1] %2%3").arg(e.code, e.title.isEmpty() ? QString() : e.title + QStringLiteral(": "), e.text));
}

std::optional<QJsonValue> Call::answer(const QString &site) const
{
    if(site.isEmpty() || (!answers.contains(site))) return std::nullopt;
    return answers.value(site);
}

QJsonObject Call::toJson() const
{
    QJsonArray errs;
    for(const Error &e : errors) errs.append(e.toJson());

    QJsonObject o;
    o.insert(QStringLiteral("errors"), errs);
    o.insert(QStringLiteral("log"), QJsonArray::fromStringList(log));
    o.insert(QStringLiteral("elapsedMs"), wall.isValid() ? int(wall.elapsed()) : 0);
    if(cancelled) o.insert(QStringLiteral("cancelled"), true);
    return o;
}

Call *currentCall()
{
    return s_currentCall;
}

Scope::Scope(Call &call) : m_previous(s_currentCall)
{
    QTC_CHECK(QThread::currentThread() == qApp->thread());
    s_currentCall = &call;
}

Scope::~Scope()
{
    s_currentCall = m_previous;
}

QString synthesizeSite(const QString &title, const QString &text)
{
    // Stable across runs for the same (title, text); good enough to key an
    // answer or to tell two untagged sites apart in a result.
    QByteArray h = QCryptographicHash::hash((title + QLatin1Char('\x1f') + text).toUtf8(), QCryptographicHash::Sha1).toHex().left(8);
    return QStringLiteral("auto.") + QString::fromLatin1(h);
}

} // namespace OMVMCP
} // namespace Internal
} // namespace OpenMV
