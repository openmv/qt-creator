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

#include "wifiscan.h"

#include <QSet>
#include <QProcess>
#include <QRegularExpression>

// NOTE: A native scan (Windows WLAN API / macOS CoreWLAN / Linux libnm) would be nicer, but the
// IDE's MinGW toolchain ships a broken <wlanapi.h>, and CoreWLAN/libnm pull in heavy Obj-C/D-Bus
// dependencies. The OS command-line tools below are reliable and toolchain-independent, and the
// SSID picker stays editable so anything they miss can be typed in by hand.

namespace OpenMV {
namespace Internal {

static QStringList sortUnique(const QStringList &in)
{
    QSet<QString> seen;
    QStringList out;

    for(const QString &ssid : in)
    {
        const QString trimmed = ssid.trimmed();

        if((!trimmed.isEmpty()) && (!seen.contains(trimmed)))
        {
            seen.insert(trimmed);
            out.append(trimmed);
        }
    }

    out.sort(Qt::CaseInsensitive);
    return out;
}

static QString runTool(const QString &program, const QStringList &args)
{
    QProcess process;
    process.start(program, args);

    if(process.waitForStarted(2000) && process.waitForFinished(8000))
    {
        return QString::fromUtf8(process.readAllStandardOutput());
    }

    return QString();
}

QStringList scanWifiNetworks()
{
    QStringList ssids;

#if defined(Q_OS_WIN)
    // "netsh wlan show networks" lists each as "SSID N : <name>" (a blank name == hidden network).
    const QStringList lines = runTool(QStringLiteral("netsh"),
        {QStringLiteral("wlan"), QStringLiteral("show"), QStringLiteral("networks")}).split(QChar('\n'));
    const QRegularExpression re(QStringLiteral("^\\s*SSID\\s+\\d+\\s*:\\s*(.*\\S)\\s*$"));

    for(const QString &line : lines)
    {
        const QRegularExpressionMatch m = re.match(line);

        if(m.hasMatch())
        {
            ssids.append(m.captured(1));
        }
    }
#elif defined(Q_OS_MAC)
    // The airport tool prints a table; SSID is the leading column up to the BSSID (>=2 spaces).
    const QStringList lines = runTool(
        QStringLiteral("/System/Library/PrivateFrameworks/Apple80211.framework/Versions/Current/Resources/airport"),
        {QStringLiteral("-s")}).split(QChar('\n'));

    for(int i = 1; i < lines.size(); i++) // skip the header row
    {
        const int split = lines.at(i).indexOf(QRegularExpression(QStringLiteral("\\s{2,}")));

        if(split > 0)
        {
            ssids.append(lines.at(i).left(split).trimmed());
        }
    }
#else // Linux and others
    ssids = runTool(QStringLiteral("nmcli"),
        {QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("SSID"),
         QStringLiteral("dev"), QStringLiteral("wifi")}).split(QChar('\n'));
#endif

    return sortUnique(ssids);
}

QString connectedWifiSsid()
{
#if defined(Q_OS_WIN)
    // "netsh wlan show interfaces" prints "SSID : <name>" for the connected interface (the "BSSID"
    // line won't match since it doesn't start with "SSID").
    const QStringList lines = runTool(QStringLiteral("netsh"),
        {QStringLiteral("wlan"), QStringLiteral("show"), QStringLiteral("interfaces")}).split(QChar('\n'));
    const QRegularExpression re(QStringLiteral("^\\s*SSID\\s*:\\s*(.*\\S)\\s*$"));

    for(const QString &line : lines)
    {
        const QRegularExpressionMatch m = re.match(line);

        if(m.hasMatch())
        {
            return m.captured(1);
        }
    }

    return QString();
#else
    return QString();
#endif
}

QString savedWifiPassword(const QString &ssid)
{
#if defined(Q_OS_WIN)
    if(ssid.isEmpty())
    {
        return QString();
    }

    // "netsh wlan show profile name=<SSID> key=clear" prints "Key Content : <password>" for a saved
    // profile owned by the current user (no elevation needed).
    const QStringList lines = runTool(QStringLiteral("netsh"),
        {QStringLiteral("wlan"), QStringLiteral("show"), QStringLiteral("profile"),
         QStringLiteral("name=") + ssid, QStringLiteral("key=clear")}).split(QChar('\n'));
    const QRegularExpression re(QStringLiteral("^\\s*Key Content\\s*:\\s*(.*\\S)\\s*$"));

    for(const QString &line : lines)
    {
        const QRegularExpressionMatch m = re.match(line);

        if(m.hasMatch())
        {
            return m.captured(1);
        }
    }

    return QString();
#else
    Q_UNUSED(ssid)
    return QString();
#endif
}

} // namespace Internal
} // namespace OpenMV
