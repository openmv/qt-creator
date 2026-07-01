/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Port
 *
 * This module provides an interface to create coms ports.
 */
#pragma once

#include <QtCore/QIODevice>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpSocket>
#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>

namespace omv {

typedef enum OMVPortType {
    OMVPortType_Serial,
    OMVPortType_Network
} OMVPortType_t;

class OMVPort : public QObject
{
    Q_OBJECT
public:
    explicit OMVPort(const QString &name, QObject *parent = nullptr)
        : QObject(parent), m_portName(name) { }
    QString portName() { return m_portName; }
    virtual OMVPortType_t portType() = 0;
    virtual int readTimeoutMs() = 0;
    virtual int readStallTimeoutMs() = 0;
    virtual bool hasVIDPID() { return false; }
    virtual QPair<int, int> getVIDPID() { return QPair<int, int>(); }
    virtual bool reliableTransport() { return true; }
    virtual bool fullDuplexTransport() { return true; }

    virtual void setReadBufferSize(qint64 size) = 0;
    virtual bool setBaudRate(qint32 baudRate) = 0;

    virtual bool open(QIODevice::OpenMode mode) = 0;
    virtual bool isOpen() = 0;
    virtual bool flush() = 0;

    virtual QString errorString() = 0;
    virtual void clearError() = 0;

    virtual QByteArray readAll() = 0;
    virtual qint64 write(const char *data, qint64 maxSize) = 0;
    qint64 write(const QByteArray &data) {
        return write(data.constData(), data.size());
    }

    virtual qint64 bytesAvailable() = 0;
    virtual qint64 bytesToWrite() = 0;

    virtual bool waitForReadyRead(int msecs) = 0;
    virtual bool waitForBytesWritten(int msecs) = 0;
    virtual bool setDataTerminalReady(bool set) = 0;
    virtual bool setRequestToSend(bool set) = 0;
protected:
    QString m_portName;
};

class OMVSerialPort : public OMVPort
{
    Q_OBJECT
public:
    explicit OMVSerialPort(const QString &name, QObject *parent = nullptr);
    OMVPortType_t portType() override { return OMVPortType_Serial; }
    int readTimeoutMs() override;
    int readStallTimeoutMs() override;
    bool hasVIDPID() override;
    QPair<int, int> getVIDPID() override;
    // A serial port is only reliable if it's VID/PID matches known USB VCP OpenMV Cams.
    bool reliableTransport() override { return false; }

    void setReadBufferSize(qint64 size) override;
    bool setBaudRate(qint32 baudRate) override;

    bool open(QIODevice::OpenMode mode) override;
    bool isOpen() override;
    bool flush() override;

    QString errorString() override;
    void clearError() override;

    QByteArray readAll() override;
    qint64 write(const char *data, qint64 maxSize) override;

    qint64 bytesAvailable() override;
    qint64 bytesToWrite() override;

    bool waitForReadyRead(int msecs) override;
    bool waitForBytesWritten(int msecs) override;
    bool setDataTerminalReady(bool set) override;
    bool setRequestToSend(bool set) override;
private:
    QSerialPort *m_serialPort;
};

// Hybrid network port. All protocol control traffic rides a reliable TCP connection; bulk camera
// frame data is received over a UDP socket (wired in Phase 2). reliableTransport() is inherited from
// OMVPort (true), so the OMVCamera construction seam keeps ACK off and leaves the hand-rolled
// retransmit/resync machinery inert -- kernel TCP handles reliability.
class OMVNetworkPort : public OMVPort
{
    Q_OBJECT
public:
    explicit OMVNetworkPort(const QString &name, QObject *parent = nullptr);
    OMVPortType_t portType() override { return OMVPortType_Network; }
    int readTimeoutMs() override;
    int readStallTimeoutMs() override;

    void setReadBufferSize(qint64 size) override;
    bool setBaudRate(qint32 baudRate) override;

    bool open(QIODevice::OpenMode mode) override;
    bool isOpen() override;
    bool flush() override;

    QString errorString() override;
    void clearError() override;

    QByteArray readAll() override;
    qint64 write(const char *data, qint64 maxSize) override;

    qint64 bytesAvailable() override;
    qint64 bytesToWrite() override;

    bool waitForReadyRead(int msecs) override;
    bool waitForBytesWritten(int msecs) override;
    bool setDataTerminalReady(bool set) override;
    bool setRequestToSend(bool set) override;
private:
    QTcpSocket *m_tcpSocket;
};

class OMVPortFactory
{
public:
    static OMVPort *createPort(const QString &name, QObject *parent = nullptr) {
        if(!QSerialPortInfo(name).isNull()) {
            return new OMVSerialPort(name, parent);
        } else {
            // A name that isn't a serial port is a network entry ("name:ip:port") discovered via
            // mDNS -- build the hybrid TCP-control / UDP-frame port.
            return new OMVNetworkPort(name, parent);
        }
    }
};

} // namespace omv
