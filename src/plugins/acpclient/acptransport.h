// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QJsonObject>
#include <QObject>

namespace AcpClient::Internal {

class AcpTransport : public QObject
{
    Q_OBJECT

public:
    explicit AcpTransport(QObject *parent = nullptr);
    ~AcpTransport() override;

    void send(const QJsonObject &message);

    virtual void start() = 0;
    virtual void stop() = 0;

signals:
    void messageReceived(const QJsonObject &message);
    void started();
    void finished();
    void errorOccurred(const QString &message);

protected:
    virtual void sendData(const QByteArray &data) = 0;
    void parseData(const QByteArray &data);
    // OPENMV-DIFF //
    void processBuffer(); // handles buffered messages in time slices
    // OPENMV-DIFF //

private:
    QByteArray m_buffer;
    // OPENMV-DIFF //
    qsizetype m_bufferOffset = 0;      // start of the unprocessed data in m_buffer
    bool m_processingScheduled = false; // a continuation is queued
    // OPENMV-DIFF //
};

} // namespace AcpClient::Internal
