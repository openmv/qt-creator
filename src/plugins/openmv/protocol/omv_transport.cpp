/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Protocol Transport Layer
 *
 * This module provides the low-level transport layer with the protocol state
 * machine for packet parsing and communication management.
 */

#include "omv_constants.h"
#include "omv_debug.h"
#include "omv_exceptions.h"
#include "omv_crc.h"
#include "omv_transport.h"

#include <QtCore/QBuffer>
#include <QtCore/QDateTime>

namespace omv {

static bool logging_enabled = false;
static bool print_frags = false;

OMVTransport::OMVTransport(OMVPort *serial_,
                           bool crc,
                           bool seq,
                           qsizetype max_payload_,
                           double timeout_,
                           std::function<void(uint8_t, uint16_t)> event_callback_,
                           double drop_rate_)
    : serial(serial_),
    timeout(timeout_),
    max_payload(max_payload_),
    sequence(0),
    state(OMVPState::SYNC),
    crc_enabled(crc),
    seq_enabled(seq),
    event_callback(event_callback_),
    drop_rate(drop_rate_),
    buf(qMax(max_payload_ * 4, qsizetype(4 * 1024 * 1024))),
    pbuf(max_payload_ + OMVProto::HEADER_SIZE + OMVProto::CRC_SIZE, char(0)),
    plength(0)
{
    stats.sent = 0;
    stats.received = 0;
    stats.checksum = 0;
    stats.sequence = 0;
}

void OMVTransport::setLoggingEnabled(bool enabled)
{
    logging_enabled = enabled;
}

bool OMVTransport::isLoggingEnabled()
{
    return logging_enabled;
}

void OMVTransport::reset_sequence()
{
    /*
        Reset sequence counter to 0
    */
    sequence = 0;
}

void OMVTransport::update_caps(bool crc, bool seq, bool ack, qsizetype max_payload_)
{
    /*
        Update transport capabilities
    */
    crc_enabled = crc;
    seq_enabled = seq;
    ack_enabled = ack;
    max_payload = max_payload_;

    // Reallocate buffers to accommodate new max_payload
    buf = OMVRingBuffer(qMax(max_payload_ * 4, qsizetype(4 * 1024 * 1024)));
    pbuf.resize(max_payload_ + OMVProto::HEADER_SIZE + OMVProto::CRC_SIZE);
    pbuf.fill(char(0));
}

uint32_t OMVTransport::_crc(QByteArrayView data, int crc_size) const
{
    /*
        Calculate CRC with specified size (16 or 32)
    */
    if (!crc_enabled) {
        return 0;
    }
    return (crc_size == 16) ? crc16(data) : crc32(data);
}

bool OMVTransport::_check_crc(uint32_t crc, QByteArrayView buffer, int crc_size) const
{
    /*
        Check if CRC matches the calculated value or CRC is disabled
    */
    return (!crc_enabled) || (crc == _crc(buffer, crc_size));
}

bool OMVTransport::_check_seq(uint8_t sequence_,
                              uint8_t expected_sequence,
                              uint8_t opcode,
                              uint8_t flags) const
{
    /*
        Check if sequence is valid or sequence checking is disabled
    */
    return (!seq_enabled) ||
           (flags & OMVPFlags::EVENT) ||
           (flags & OMVPFlags::RTX) ||
           (sequence_ == expected_sequence) ||
           (opcode == OMVPOpcode::PROTO_SYNC);
}

QString OMVTransport::_format_flags(uint8_t flags) const
{
    /*
        Get human-readable name for packet flags
    */
    if (flags == 0) {
        return QStringLiteral("0x00");
    }

    QStringList parts;
    if (flags & OMVPFlags::ACK)      parts << QStringLiteral("ACK");
    if (flags & OMVPFlags::NAK)      parts << QStringLiteral("NAK");
    if (flags & OMVPFlags::RTX)      parts << QStringLiteral("RTX");
    if (flags & OMVPFlags::FRAGMENT) parts << QStringLiteral("FRAG");
    if (flags & OMVPFlags::EVENT)    parts << QStringLiteral("EVT");
    if (flags & OMVPFlags::ACK_REQ)  parts << QStringLiteral("ACK_REQ");

    return parts.isEmpty() ? QStringLiteral("0x%1").arg(flags, 2, 16, QChar('0')).toUpper()
                           : parts.join('|');
}

void OMVTransport::log(int seq,
                       int ch,
                       int opcode,
                       int flags,
                       int length,
                       const char *direction)
{
    if (!logging_enabled) return;
    if (!print_frags && (flags & OMVPFlags::FRAGMENT)) return;

    /*
        Log packet information for debugging
    */
    QString opcode_str;
    switch (opcode) {
    case OMVPOpcode::PROTO_SYNC:      opcode_str = "PROTO_SYNC"; break;
    case OMVPOpcode::PROTO_GET_CAPS:  opcode_str = "PROTO_GET_CAPS"; break;
    case OMVPOpcode::PROTO_SET_CAPS:  opcode_str = "PROTO_SET_CAPS"; break;
    case OMVPOpcode::PROTO_STATS:     opcode_str = "PROTO_STATS"; break;

    case OMVPOpcode::SYS_RESET:       opcode_str = "SYS_RESET"; break;
    case OMVPOpcode::SYS_BOOT:        opcode_str = "SYS_BOOT"; break;
    case OMVPOpcode::SYS_INFO:        opcode_str = "SYS_INFO"; break;
    case OMVPOpcode::SYS_EVENT:       opcode_str = "SYS_EVENT"; break;

    case OMVPOpcode::CHANNEL_LIST:    opcode_str = "CHANNEL_LIST"; break;
    case OMVPOpcode::CHANNEL_POLL:    opcode_str = "CHANNEL_POLL"; break;
    case OMVPOpcode::CHANNEL_LOCK:    opcode_str = "CHANNEL_LOCK"; break;
    case OMVPOpcode::CHANNEL_UNLOCK:  opcode_str = "CHANNEL_UNLOCK"; break;
    case OMVPOpcode::CHANNEL_SHAPE:   opcode_str = "CHANNEL_SHAPE"; break;
    case OMVPOpcode::CHANNEL_SIZE:    opcode_str = "CHANNEL_SIZE"; break;
    case OMVPOpcode::CHANNEL_READ:    opcode_str = "CHANNEL_READ"; break;
    case OMVPOpcode::CHANNEL_WRITE:   opcode_str = "CHANNEL_WRITE"; break;
    case OMVPOpcode::CHANNEL_IOCTL:   opcode_str = "CHANNEL_IOCTL"; break;
    case OMVPOpcode::CHANNEL_EVENT:   opcode_str = "CHANNEL_EVENT"; break;

    default:
        opcode_str = QStringLiteral("0x%1").arg(opcode, 2, 16, QChar('0')).toUpper();
        break;
    }

    QString flags_str = _format_flags(uint8_t(flags));

    omvDebug().noquote().nospace()
        << direction
        << ": seq=" << QStringLiteral("%1").arg(seq, 3, 10, QChar('0'))
        << ", chan=" << ch
        << ", opcode=" << opcode_str
        << ", flags=" << flags_str
        << ", length=" << length
        << ", time=" << (QDateTime::currentMSecsSinceEpoch() % 10000) << "ms";
}

void OMVTransport::send_packet(uint8_t opcode,
                               uint8_t channel,
                               uint8_t flags,
                               QByteArrayView data,
                               int sequence_override)
{
    /*
        Send a packet to the camera
    */
    if (!serial || !serial->isOpen()) {
        throw OMVPTimeoutException(QStringLiteral("Serial connection not open"));
    }

    uint8_t seq_to_use = (sequence_override < 0) ? sequence : uint8_t(sequence_override);
    uint16_t length = uint16_t(data.size());

    if (length > max_payload) {
        throw OMVPException(QStringLiteral("Payload too large: %1 > %2")
                                .arg(length)
                                .arg(max_payload));
    }

    const qsizetype packet_size =
        OMVProto::HEADER_SIZE + length + (length ? OMVProto::CRC_SIZE : 0);

    if (packet_size > pbuf.size()) {
        throw OMVPException(QStringLiteral("Internal pbuf too small"));
    }

    // Use QDataStream to pack header into preallocated pbuf (no resize)
    QBuffer dev(&pbuf);
    dev.open(QIODevice::WriteOnly);
    dev.seek(0);

    QDataStream ds(&dev);
    ds.setByteOrder(QDataStream::LittleEndian);

    // Pack header without CRC first (10 bytes total, CRC added after)
    ds << quint16(OMVProto::SYNC_WORD)
       << quint8(seq_to_use)
       << quint8(channel)
       << quint8(flags)
       << quint8(opcode)
       << quint16(length);

    // Header CRC16 over first 8 bytes
    const uint16_t hcrc =
        uint16_t(_crc(QByteArrayView(pbuf.constData(), OMVProto::HEADER_SIZE - 2), 16));

    ds << quint16(hcrc);

    // Pack data if present
    if (length > 0) {
        dev.write(data.data(), length);

        const uint32_t pcrc =
            _crc(QByteArrayView(pbuf.constData() + OMVProto::HEADER_SIZE, length), 32);

        ds << quint32(pcrc);
    }

    log(seq_to_use, channel, opcode, flags, length, "Send");

    for (qint64 written = 0;;) {
        qint64 ret = serial->write(pbuf.constData() + written, packet_size - written);

        if (ret < 0) {
            throw OMVPTimeoutException(QStringLiteral("Failed to write to serial port"));
        }

        written += ret;

        if ((written >= packet_size)) {
            break;
        }

        serial->flush(); // ignore return

        QElapsedTimer elaspedTimer;
        elaspedTimer.start();

        while (serial->bytesToWrite()) {
            serial->waitForBytesWritten(1);
            if(serial->bytesToWrite() && elaspedTimer.hasExpired(timeout * 1000.0)) {
                throw OMVPTimeoutException(QStringLiteral("Failed to write to serial port"));
            }
        }
    }

    stats.sent += 1;
}

QVariant OMVTransport::recv_packet(bool poll_events)
{
    /*
        Receive and parse a packet from the camera with NAK handling
    */
    if (!serial || !serial->isOpen()) {
        throw OMVPException(QStringLiteral("Serial connection not open"));
    }

    QByteArray fragments;
    QElapsedTimer timer;
    timer.start();

    const qint64 timeout_ms = qint64(timeout * 1000.0);

    while (timer.elapsed() < timeout_ms) {
        serial->waitForReadyRead(1);

        if (serial->bytesAvailable() > 0) {
            QByteArray data = serial->readAll();
            buf.extend(data);
        }

        Packet packet;
        if (!_process(packet)) {
            if (poll_events) {
                return QVariant(); // None
            }
            // QThread::msleep(1);
            continue;
        }

        // Simulate packet drops by randomly dropping parsed packets
        if (drop_rate > 0.0) {
            if (QRandomGenerator::global()->generateDouble() < drop_rate) {
                log(packet.sequence, packet.channel, packet.opcode, packet.flags, packet.length, "Drop");
                continue;
            }
        }

        stats.received += 1;
        log(packet.sequence, packet.channel, packet.opcode, packet.flags, packet.length, "Recv");

        // Handle retransmission
        if ((packet.flags & OMVPFlags::RTX) && (sequence != packet.sequence)) {
            if (packet.flags & OMVPFlags::ACK_REQ) {
                send_packet(packet.opcode, packet.channel, OMVPFlags::ACK, QByteArrayView(), packet.sequence);
            }
            continue;
        }

        // ACK the received packet
        if (packet.flags & OMVPFlags::ACK_REQ) {
            if (drop_rate > 0.0 && QRandomGenerator::global()->generateDouble() < drop_rate) {
                log(packet.sequence, packet.channel, packet.opcode, OMVPFlags::ACK, 0, "Drop");
            } else {
                send_packet(packet.opcode, packet.channel, OMVPFlags::ACK);
            }
        }

        // Handle event packets
        if (packet.flags & OMVPFlags::EVENT) {
            uint16_t evt = 0xFFFF;
            if (packet.length >= 2) {
                QDataStream ds(packet.payload);
                ds.setByteOrder(QDataStream::LittleEndian);
                ds >> evt;
            }
            event_callback(packet.channel, evt);
            timer.restart();
            continue;
        }

        // Update sequence after each packet (including fragments)
        sequence = uint8_t((sequence + 1) & 0xFF);

        // Check if this is a fragmented packet
        if (packet.flags & OMVPFlags::FRAGMENT) {
            fragments.append(packet.payload);

            const qsizetype frag_limit = 1024 * 1024 * 1024;

            if (fragments.size() > frag_limit) {
                // Treat as overflow / desync.
                stats.checksum += 1; // closest existing stat; or add stats.overflow
                log(packet.sequence, packet.channel, packet.opcode,
                    packet.flags, packet.length, "Rjct1");

                fragments.clear();
                // reset parser state to hunt for next sync
                state = OMVPState::SYNC;
                // Optional: drop one byte to move forward if buffer is stuck.
                if (buf.length() > 0) {
                    buf.consume(1);
                }
                continue;
            }

            timer.restart();
            continue;
        }

        // Either last fragment or non-fragmented packet
        if (!fragments.isEmpty()) {
            fragments.append(packet.payload);

            const qsizetype frag_limit = 1024 * 1024 * 1024;

            if (fragments.size() > frag_limit) {
                stats.checksum += 1;
                log(packet.sequence, packet.channel, packet.opcode,
                    packet.flags, packet.length, "Rjct2");

                fragments.clear();
                state = OMVPState::SYNC;
                if (buf.length() > 0) {
                    buf.consume(1);
                }
                continue;
            }

            packet.payload = fragments;
            packet.length = uint16_t(fragments.size());
        }

        // Handle NAK flags
        if (packet.flags & OMVPFlags::NAK) {
            uint16_t status = 0;
            if (packet.payload.size() >= 2) {
                QDataStream ds(packet.payload);
                ds.setByteOrder(QDataStream::LittleEndian);
                ds >> status;
            }

            if (status == OMVPStatus::CHECKSUM) {
                throw OMVPChecksumException(QString());
            } else if (status == OMVPStatus::SEQUENCE) {
                throw OMVPSequenceException(QString());
            } else if (status == OMVPStatus::TIMEOUT) {
                throw OMVPTimeoutException(QString());
            } else if (status != OMVPStatus::BUSY) {
                throw OMVPException(QStringLiteral("Command failed with status: %1").arg(status));
            }

            return QVariant(false); // BUSY => False
        }

        // Return payload or True for ACK
        if (!packet.length) {
            return QVariant(true);
        }

        return QVariant(packet.payload);
    }

    if (!poll_events) {
        throw OMVPTimeoutException(QStringLiteral("Packet receive timeout"));
    }

    return QVariant(); // None
}

bool OMVTransport::_process(Packet &out_packet)
{
    /*
        Process the protocol state machine
    */
    while (buf.length() > 2) {
        if (state == OMVPState::SYNC) {
            // Find sync pattern
            while (buf.length() > 2) {
                quint16 sync = 0;
                if (!buf.peek16(sync)) {
                    return false;
                }
                if (sync == OMVProto::SYNC_WORD) {
                    state = OMVPState::HEADER;
                    break;
                }
                buf.consume(1);
            }
        } else if (state == OMVPState::HEADER) {
            // Wait for complete header
            if (buf.length() < OMVProto::HEADER_SIZE) {
                return false;
            }

            QByteArrayView header_view = buf.peek(OMVProto::HEADER_SIZE);
            QByteArray header_raw = QByteArray::fromRawData(header_view.data(), header_view.size());

            // Parse header using QDataStream: <HBBBBHH
            QDataStream ds(header_raw);
            ds.setByteOrder(QDataStream::LittleEndian);

            quint16 sync;
            quint8 seq;
            quint8 chan;
            quint8 flags;
            quint8 opcode;
            quint16 length;
            quint16 crc;

            ds >> sync >> seq >> chan >> flags >> opcode >> length >> crc;

            state = OMVPState::SYNC;

            if (length > max_payload) {
                sequence = uint8_t((seq + 1) & 0xFF);
                log(seq, chan, opcode, flags, length, "Rjct3");
                buf.consume(1);
            } else if (!_check_seq(seq, sequence, opcode, flags)) {
                sequence = uint8_t((seq + 1) & 0xFF);
                stats.sequence += 1;
                log(seq, chan, opcode, flags, length, "Rjct4");
                buf.consume(1);
            } else if (!_check_crc(crc,
                                   QByteArrayView(header_view.data(), OMVProto::HEADER_SIZE - 2),
                                   16)) {
                sequence = uint8_t((seq + 1) & 0xFF);
                stats.checksum += 1;
                log(seq, chan, opcode, flags, length, "Rjct5");
                buf.consume(1);
            } else {
                state = OMVPState::PAYLOAD;
                plength = OMVProto::HEADER_SIZE + length;
                plength += (length ? OMVProto::CRC_SIZE : 0);
            }
        } else if (state == OMVPState::PAYLOAD) {
            // Wait for a complete packet
            if (buf.length() < plength) {
                return false;
            }

            state = OMVPState::SYNC;

            QByteArrayView packet_view = buf.peek(plength);
            QByteArray packet_raw = QByteArray::fromRawData(packet_view.data(), packet_view.size());

            QDataStream ds(packet_raw);
            ds.setByteOrder(QDataStream::LittleEndian);

            quint16 sync;
            quint8 seq;
            quint8 chan;
            quint8 flags;
            quint8 opcode;
            quint16 length;
            quint16 header_crc;

            ds >> sync >> seq >> chan >> flags >> opcode >> length >> header_crc;

            QByteArray payload;

            if (length > 0) {
                const char *payload_ptr = packet_view.data() + OMVProto::HEADER_SIZE;
                QByteArrayView payload_view(payload_ptr, length);

                const char *crc_ptr = packet_view.data() + (plength - OMVProto::CRC_SIZE);
                quint32 payload_crc =
                    quint32(uint8_t(crc_ptr[0]) |
                            (uint8_t(crc_ptr[1]) << 8) |
                            (uint8_t(crc_ptr[2]) << 16) |
                            (uint8_t(crc_ptr[3]) << 24));

                if (!_check_crc(payload_crc, payload_view, 32)) {
                    sequence = uint8_t((seq + 1) & 0xFF);
                    stats.checksum += 1;
                    log(seq, chan, opcode, flags, length, "Rjct6");
                    buf.consume(1);
                    continue;
                }

                // Only copy after CRC passes (matches Python returning bytes)
                payload = QByteArray(payload_ptr, length);
            }

            // Print the payload data nicely with 32 bytes displayed as hex per line
            // for (qsizetype offset = 0; offset < payload.size(); offset += 32) {
            //     QByteArray line = payload.mid(offset, 32);
            //     QStringList hex_parts;
            //     for (char byte : line) {
            //         hex_parts << QStringLiteral("%1").arg(uint8_t(byte), 2, 16, QChar('0')).toUpper();
            //     }
            //     omvDebug().noquote().nospace() << hex_parts.join(' ');
            // }

            buf.consume(plength);

            out_packet.sync = sync;
            out_packet.sequence = seq;
            out_packet.channel = chan;
            out_packet.flags = flags;
            out_packet.opcode = opcode;
            out_packet.length = length;
            out_packet.header_crc = header_crc;
            out_packet.payload = payload;

            // Anytime we receive a valid packet send a keep alive byte to prevent stalls.
            // Ensure the keep alive byte is flushed immediately if this is not a fragment.
            #ifdef Q_OS_WIN
            if ((flags & OMVPFlags::FRAGMENT) && (!ack_enabled)) _sendKeepAlive();
            #endif

            return true;
        }
    }

    return false;
}

/*
    Sends a 0 byte on the serial port to keep the serial connection from stalling.
 */
#ifdef Q_OS_WIN
void OMVTransport::_sendKeepAlive()
{
// This is only needed on Windows for its serial port drivers. The problem is that
// the serial port read call will not return any data until a write to the serial
// port is done. The contents of that write do not that matter, other than one
// is completed. Afterwhich, the serial port will resume returning data again.
    /*
        Send a packet to the camera
    */
    if (!serial || !serial->isOpen()) {
        throw OMVPTimeoutException(QStringLiteral("Serial connection not open"));
    }

    qint64 ret = serial->write(QByteArray(1, char(0x00)));

    if (ret < 0) {
        throw OMVPTimeoutException(QStringLiteral("Failed to write to serial port"));
    }

    if (ret == 1) {
        return;
    }

    serial->flush(); // ignore return

    QElapsedTimer elaspedTimer;
    elaspedTimer.start();

    while (serial->bytesToWrite()) {
        serial->waitForBytesWritten(1);
        if(serial->bytesToWrite() && elaspedTimer.hasExpired(timeout * 1000.0)) {
            throw OMVPTimeoutException(QStringLiteral("Failed to write to serial port"));
        }
    }
}
#endif

} // namespace omv
