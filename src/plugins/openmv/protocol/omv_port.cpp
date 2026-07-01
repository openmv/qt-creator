/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Port
 *
 * This module provides an interface to create coms ports.
 */

#include <QSerialPort>

#include "omv_constants.h"
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
// OMVNetworkPort -- hybrid port: TCP control plane + UDP frame-data plane
// ---------------------------------------------------------------------------

// Length of the leading run of *whole* packets in buf (v2 wire layout:
// SYNC(2) SEQ(1) CHAN(1) FLAGS(1) OPCODE(1) LEN(2) HCRC(2) [payload] [PCRC(4)]). The remainder is a
// partial trailing packet to hold until more arrives, so a UDP frame datagram is never appended into
// the middle of a half-received TCP control packet. Assumes buf starts on a packet boundary -- true
// for an in-order TCP stream we always frame exactly; if a desync ever slips through, hand everything
// up and let the transport's own sync-scanning parser resynchronize.
static qsizetype omvWholePacketPrefix(const QByteArray &buf)
{
    const quint8 syncLo = quint8(OMVProto::SYNC_WORD & 0xFF);   // 0xAA -- SYNC_WORD little-endian
    const quint8 syncHi = quint8(OMVProto::SYNC_WORD >> 8);     // 0xD5
    const qsizetype n = buf.size();
    qsizetype off = 0;

    while (n - off >= OMVProto::HEADER_SIZE) {
        if (quint8(buf[off]) != syncLo || quint8(buf[off + 1]) != syncHi) {
            return n;
        }

        const int len = quint8(buf[off + 6]) | (quint8(buf[off + 7]) << 8);   // LEN field @ header offset 6
        const qsizetype pkt = OMVProto::HEADER_SIZE + len + (len ? OMVProto::CRC_SIZE : 0);

        if (n - off < pkt) {
            break;   // partial trailing packet -- hold it until the rest arrives
        }

        off += pkt;
    }

    return off;
}

OMVNetworkPort::OMVNetworkPort(const QString &name, QObject *parent) : OMVPort(name, parent)
{
    m_tcpSocket = new QTcpSocket(this);
    m_udpSocket = new QUdpSocket(this);
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

    // Data plane: receive frame datagrams on the SAME local port our TCP connection uses, so the
    // camera reuses the TCP peer address as the UDP frame destination -- no separate handshake (TCP
    // and UDP port spaces are independent). This bind essentially always succeeds (the OS just handed
    // that port to our TCP socket); if it somehow can't, control is unaffected and frames are lost.
    m_udpSocket->bind(QHostAddress::AnyIPv4, m_tcpSocket->localPort());

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
    // Control plane: drain the TCP stream but hand upstream only whole packets, holding any partial
    // trailing packet, so a UDP frame datagram is never spliced into a half-received control packet.
    m_tcpBuf.append(m_tcpSocket->readAll());
    const qsizetype whole = omvWholePacketPrefix(m_tcpBuf);
    QByteArray out = m_tcpBuf.left(whole);
    m_tcpBuf.remove(0, whole);

    // Data plane: each UDP datagram is exactly one whole frame-read packet (the camera flushes one
    // packet per datagram) -- append them as-is.
    while (m_udpSocket->hasPendingDatagrams()) {
        QByteArray datagram(int(m_udpSocket->pendingDatagramSize()), 0);
        m_udpSocket->readDatagram(datagram.data(), datagram.size());
        out.append(datagram);
    }

    return out;
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
    // New parseable bytes on either plane. The held partial-packet tail is deliberately excluded --
    // it can't be parsed yet, and counting it would spin the read loop returning nothing.
    qint64 n = m_tcpSocket->bytesAvailable();

    if (m_udpSocket->hasPendingDatagrams()) {
        n += m_udpSocket->pendingDatagramSize();
    }

    return n;
}

qint64 OMVNetworkPort::bytesToWrite()
{
    return m_tcpSocket->bytesToWrite();
}

bool OMVNetworkPort::waitForReadyRead(int msecs)
{
    // Wake on either plane. The read loop polls with a ~1ms timeout and gates on bytesAvailable(), so
    // waiting on TCP here and rechecking UDP after picks up frame datagrams within one poll tick.
    if (m_udpSocket->hasPendingDatagrams()) {
        return true;
    }

    bool ready = m_tcpSocket->waitForReadyRead(msecs);
    return ready || m_udpSocket->hasPendingDatagrams();
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
