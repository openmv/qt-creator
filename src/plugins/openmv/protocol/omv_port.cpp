/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Port
 *
 * This module provides an interface to create coms ports.
 */

#include <QSerialPort>

#include "omv_port.h"

#define SERIAL_READ_TIMEOUT 3000
#define SERIAL_READ_STALL_TIMEOUT 1000

#define TCP_READ_TIMEOUT 5000
#define TCP_READ_STALL_TIMEOUT 3000
#define TCP_CONNECT_TIMEOUT 3000

#define READ_BUFFER_SIZE (64 * 1024 * 1024)
#define WRITE_BUFFER_SIZE (64 * 1024 * 1024)

namespace omv {

OMVSerialPort::OMVSerialPort(const QString &name, QObject *parent) : OMVPort(name, parent)
{
    m_serialPort = new QSerialPort(name, this);
}

int OMVSerialPort::readTimeoutMs()
{
    return SERIAL_READ_TIMEOUT;
}

int OMVSerialPort::readStallTimeoutMs()
{
    return SERIAL_READ_STALL_TIMEOUT;
}

bool OMVSerialPort::hasVIDPID()
{
    QSerialPortInfo info(*m_serialPort);
    return info.hasVendorIdentifier() && info.hasProductIdentifier();
}

QPair<int, int> OMVSerialPort::getVIDPID()
{
    QSerialPortInfo info(*m_serialPort);
    return QPair<int, int>(info.vendorIdentifier(), info.productIdentifier());
}

void OMVSerialPort::setReadBufferSize(qint64 size)
{
    m_serialPort->setReadBufferSize(size);
}

bool OMVSerialPort::setBaudRate(qint32 baudRate)
{
    return m_serialPort->setBaudRate(baudRate);
}

bool OMVSerialPort::open(QIODevice::OpenMode mode)
{
    bool ok = m_serialPort->open(mode);

#ifdef Q_OS_WIN
    void *handle = m_serialPort->handle();

    if(handle)
    {
        ok = ok && SetupComm(handle, READ_BUFFER_SIZE, WRITE_BUFFER_SIZE);
    }
    else
    {
        ok = false;
    }
#endif

    return ok;
}

bool OMVSerialPort::isOpen()
{
    return m_serialPort->isOpen();
}

bool OMVSerialPort::flush()
{
    return m_serialPort->flush();
}

QString OMVSerialPort::errorString()
{
    return m_serialPort->errorString();
}

void OMVSerialPort::clearError()
{
    m_serialPort->clearError();
}

QByteArray OMVSerialPort::readAll()
{
    return m_serialPort->readAll();
}

qint64 OMVSerialPort::write(const char *data, qint64 maxSize)
{
    return m_serialPort->write(data, maxSize);
}

qint64 OMVSerialPort::bytesAvailable()
{
    return m_serialPort->bytesAvailable();
}

qint64 OMVSerialPort::bytesToWrite()
{
    return m_serialPort->bytesToWrite();
}

bool OMVSerialPort::waitForReadyRead(int msecs)
{
    return m_serialPort->waitForReadyRead(msecs);
}

bool OMVSerialPort::waitForBytesWritten(int msecs)
{
    return m_serialPort->waitForBytesWritten(msecs);
}

bool OMVSerialPort::setDataTerminalReady(bool set)
{
    return m_serialPort->setDataTerminalReady(set);
}

bool OMVSerialPort::setRequestToSend(bool set)
{
    return m_serialPort->setRequestToSend(set);
}

// ---------------------------------------------------------------------------
// OMVNetworkPort -- hybrid TCP-control / UDP-frame port (Phase 1: TCP control only)
// ---------------------------------------------------------------------------

OMVNetworkPort::OMVNetworkPort(const QString &name, QObject *parent) : OMVPort(name, parent)
{
    m_tcpSocket = new QTcpSocket(this);
}

int OMVNetworkPort::readTimeoutMs()
{
    return TCP_READ_TIMEOUT;
}

int OMVNetworkPort::readStallTimeoutMs()
{
    return TCP_READ_STALL_TIMEOUT;
}

void OMVNetworkPort::setReadBufferSize(qint64 size)
{
    m_tcpSocket->setReadBufferSize(size);
}

bool OMVNetworkPort::setBaudRate(qint32 /*baudRate*/)
{
    return true;
}

bool OMVNetworkPort::open(QIODevice::OpenMode mode)
{
    Q_UNUSED(mode)

    QStringList list = m_portName.split(QLatin1Char(':'));

    if(list.size() != 3) {
        return false;
    }

    QHostAddress host(list.at(1));
    bool portNumberOkay;
    quint16 port = list.at(2).toUShort(&portNumberOkay);

    if(!portNumberOkay) {
        return false;
    }

    m_tcpSocket->connectToHost(host, port);

    if(!m_tcpSocket->waitForConnected(TCP_CONNECT_TIMEOUT)) {
        return false;
    }

    // Small control packets must go out immediately; the protocol runs synchronously with no event
    // loop, so Nagle + delayed-ACK would otherwise stall the handshake.
    m_tcpSocket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    return true;
}

bool OMVNetworkPort::isOpen()
{
    return m_tcpSocket->state() == QAbstractSocket::ConnectedState;
}

bool OMVNetworkPort::flush()
{
    return m_tcpSocket->flush();
}

QString OMVNetworkPort::errorString()
{
    return m_tcpSocket->errorString();
}

void OMVNetworkPort::clearError()
{
}

QByteArray OMVNetworkPort::readAll()
{
    return m_tcpSocket->readAll();
}

qint64 OMVNetworkPort::write(const char *data, qint64 maxSize)
{
    // QTcpSocket::write() only buffers -- the bytes are not handed to the OS until the event loop
    // runs or we flush. The protocol code sends and then blocks in recv without returning to the
    // event loop, so flush here or the send never leaves and the handshake deadlocks. The IDE only
    // ever writes small control packets (it receives frames), so this flush always completes.
    qint64 ret = m_tcpSocket->write(data, maxSize);
    m_tcpSocket->flush();
    return ret;
}

qint64 OMVNetworkPort::bytesAvailable()
{
    return m_tcpSocket->bytesAvailable();
}

qint64 OMVNetworkPort::bytesToWrite()
{
    return m_tcpSocket->bytesToWrite();
}

bool OMVNetworkPort::waitForReadyRead(int msecs)
{
    return m_tcpSocket->waitForReadyRead(msecs);
}

bool OMVNetworkPort::waitForBytesWritten(int msecs)
{
    return m_tcpSocket->waitForBytesWritten(msecs);
}

bool OMVNetworkPort::setDataTerminalReady(bool /*set*/)
{
    return true;
}

bool OMVNetworkPort::setRequestToSend(bool /*set*/)
{
    return true;
}

} // namespace omv
