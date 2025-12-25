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

OMVTCPPort::OMVTCPPort(const QString &name, QObject *parent) : OMVPort(name, parent)
{
    m_tcpSocket = new QTcpSocket(this);
}

int OMVTCPPort::readTimeoutMs()
{
    return TCP_READ_TIMEOUT;
}

int OMVTCPPort::readStallTimeoutMs()
{
    return TCP_READ_STALL_TIMEOUT;
}

void OMVTCPPort::setReadBufferSize(qint64 size)
{
    m_tcpSocket->setReadBufferSize(size);
}

bool OMVTCPPort::setBaudRate(qint32 /*baudRate*/)
{
    return true;
}

bool OMVTCPPort::open(QIODevice::OpenMode mode)
{
    QStringList list = m_portName.split(QLatin1Char(':'));

    if(list.size() != 3) {
        return false;
    }

    QString hostName = list.at(1);
    QString port = list.at(2);

    bool portNumberOkay;
    quint16 portNumber = port.toUInt(&portNumberOkay);

    if(!portNumberOkay) {
        return false;
    }

    m_tcpSocket->connectToHost(hostName, portNumber, mode);
    return m_tcpSocket->waitForConnected(3000);
}

bool OMVTCPPort::isOpen()
{
    return m_tcpSocket->isOpen();
}

bool OMVTCPPort::flush()
{
    return m_tcpSocket->flush();
}

QString OMVTCPPort::errorString()
{
    return m_tcpSocket->errorString();
}

void OMVTCPPort::clearError()
{
    // No clearError function in QTcpSocket
}

QByteArray OMVTCPPort::readAll()
{
    return m_tcpSocket->readAll();
}

qint64 OMVTCPPort::write(const char *data, qint64 maxSize)
{
    return m_tcpSocket->write(data, maxSize);
}

qint64 OMVTCPPort::bytesAvailable()
{
    return m_tcpSocket->bytesAvailable();
}

qint64 OMVTCPPort::bytesToWrite()
{
    return m_tcpSocket->bytesToWrite();
}

bool OMVTCPPort::waitForReadyRead(int msecs)
{
    return m_tcpSocket->waitForReadyRead(msecs);
}

bool OMVTCPPort::waitForBytesWritten(int msecs)
{
    return m_tcpSocket->waitForBytesWritten(msecs);
}

bool OMVTCPPort::setDataTerminalReady(bool /*set*/)
{
    return true;
}

bool OMVTCPPort::setRequestToSend(bool /*set*/)
{
    return true;
}

} // namespace omv
