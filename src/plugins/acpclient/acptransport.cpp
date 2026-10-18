// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acptransport.h"

#include "acpclienttr.h"

// OPENMV-DIFF //
#include <QElapsedTimer>
#include <QTimer>
// OPENMV-DIFF //
#include <QJsonDocument>
#include <QLoggingCategory>

static Q_LOGGING_CATEGORY(logTransport, "qtc.acpclient.transport", QtWarningMsg);

namespace AcpClient::Internal {

AcpTransport::AcpTransport(QObject *parent)
    : QObject(parent)
{}

AcpTransport::~AcpTransport() = default;

void AcpTransport::send(const QJsonObject &message)
{
    const QByteArray content = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    qCDebug(logTransport) << "Sending:" << content;
    sendData(content);
}

void AcpTransport::parseData(const QByteArray &data)
{
    m_buffer.append(data);
    // OPENMV-DIFF //
    // Messages already waiting are handled first (by the queued continuation), so
    // the order never changes.
    if (!m_processingScheduled)
        processBuffer();
    // OPENMV-DIFF //
}

// OPENMV-DIFF //
// Handles the buffered newline-delimited JSON messages. A session load replays a
// whole conversation at once (hundreds or thousands of messages, each building
// chat widgets): handling them in one go froze the IDE until done, and slicing
// the remaining buffer after every line copied it each time (quadratic). Now the
// buffer is walked with an offset and the work is cut into ~10 ms slices, with
// the event loop running in between.
void AcpTransport::processBuffer()
{
    m_processingScheduled = false;
    QElapsedTimer slice;
    slice.start();

    qsizetype pos;
    while ((pos = m_buffer.indexOf('\n', m_bufferOffset)) >= 0) {
        if (slice.elapsed() >= 10) {
            m_processingScheduled = true;
            QTimer::singleShot(0, this, &AcpTransport::processBuffer);
            break;
        }

        const QByteArray line = m_buffer.mid(m_bufferOffset, pos - m_bufferOffset).trimmed();
        m_bufferOffset = pos + 1;

        if (line.isEmpty())
            continue;

        qCDebug(logTransport) << "Received:" << line;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError) {
            emit errorOccurred(Tr::tr("JSON parse error: %1").arg(parseError.errorString()));
            continue;
        }

        if (!doc.isObject()) {
            emit errorOccurred(Tr::tr("Expected JSON object."));
            continue;
        }

        emit messageReceived(doc.object());
    }

    // Drop what was handled, once per slice rather than once per line.
    if (m_bufferOffset > 0) {
        m_buffer.remove(0, m_bufferOffset);
        m_bufferOffset = 0;
    }
}
// OPENMV-DIFF //

} // namespace AcpClient::Internal
