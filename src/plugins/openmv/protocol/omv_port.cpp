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

#define UDP_READ_TIMEOUT 5000
#define UDP_READ_STALL_TIMEOUT 3000

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

OMVUDPPort::OMVUDPPort(const QString &name, QObject *parent) : OMVPort(name, parent)
{
    m_udpSocket = new QUdpSocket(this);
    m_remotePort = 0;
}

int OMVUDPPort::readTimeoutMs()
{
    return UDP_READ_TIMEOUT;
}

int OMVUDPPort::readStallTimeoutMs()
{
    return UDP_READ_STALL_TIMEOUT;
}

void OMVUDPPort::setReadBufferSize(qint64 size)
{
    Q_UNUSED(size)
}

bool OMVUDPPort::setBaudRate(qint32 /*baudRate*/)
{
    return true;
}

bool OMVUDPPort::open(QIODevice::OpenMode mode)
{
    Q_UNUSED(mode)

    QStringList list = m_portName.split(QLatin1Char(':'));

    if(list.size() != 3) {
        return false;
    }

    m_remoteHost = QHostAddress(list.at(1));
    bool portNumberOkay;
    m_remotePort = list.at(2).toUInt(&portNumberOkay);

    if(!portNumberOkay) {
        return false;
    }

    return m_udpSocket->bind(QHostAddress::AnyIPv4, 0);
}

bool OMVUDPPort::isOpen()
{
    return m_udpSocket->state() == QAbstractSocket::BoundState;
}

bool OMVUDPPort::flush()
{
    return true;
}

QString OMVUDPPort::errorString()
{
    return m_udpSocket->errorString();
}

void OMVUDPPort::clearError()
{
}

QByteArray OMVUDPPort::readAll()
{
    QByteArray result;

    while(m_udpSocket->hasPendingDatagrams())
    {
        QByteArray datagram(m_udpSocket->pendingDatagramSize(), 0);
        m_udpSocket->readDatagram(datagram.data(), datagram.size());
        result.append(datagram);
    }

    return result;
}

qint64 OMVUDPPort::write(const char *data, qint64 maxSize)
{
    return m_udpSocket->writeDatagram(data, maxSize, m_remoteHost, m_remotePort);
}

qint64 OMVUDPPort::bytesAvailable()
{
    return m_udpSocket->hasPendingDatagrams() ? m_udpSocket->pendingDatagramSize() : 0;
}

qint64 OMVUDPPort::bytesToWrite()
{
    return 0;
}

bool OMVUDPPort::waitForReadyRead(int msecs)
{
    return m_udpSocket->waitForReadyRead(msecs);
}

bool OMVUDPPort::waitForBytesWritten(int msecs)
{
    Q_UNUSED(msecs)
    return true;
}

bool OMVUDPPort::setDataTerminalReady(bool /*set*/)
{
    return true;
}

bool OMVUDPPort::setRequestToSend(bool /*set*/)
{
    return true;
}

} // namespace omv
