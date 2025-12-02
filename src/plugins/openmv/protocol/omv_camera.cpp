/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Protocol Camera Interface
 *
 * This module provides the high-level OMVCamera class that handles all
 * camera operations and channel communications using the OpenMV Protocol.
 */

#include <QtCore/QDataStream>
#include <QtCore/QDebug>

#include "omv_camera.h"
#include "omv_constants.h"
#include "omv_image.h"
#include "omv_port.h"

namespace omv {

OMVCamera::OMVCamera(OMVPort *serial_,
                     bool crc,
                     bool seq,
                     bool ack,
                     bool events,
                     double timeout,
                     int max_retry,
                     int max_payload,
                     double drop_rate)
    : caps_crc(crc)
    , caps_seq(seq)
    , caps_ack(ack)
    , caps_events(events)
    , caps_max_payload(uint16_t(max_payload))
    , serial(serial_)
    , timeoutSec(timeout)
    , maxRetry(max_retry)
    , dropRate(drop_rate)
    , pendingChannelEvents(0)
    , transport(nullptr)
    , frameEvent(false)
    , scriptState(false)
{
}

OMVCamera::~OMVCamera()
{
    disconnect();
}

void OMVCamera::connect()
{
    /*
        Establish connection to the OpenMV camera
    */
    disconnect();

    try {
        // Perform resync (also creates transport)
        resync();

        // Cache channel list
        updateChannels();

        // Cache system info
        sysinfo = systemInfo();

        // Print system information
        printSystemInfo();
    } catch (...) {
        disconnect();
        throw;
    }
}

void OMVCamera::disconnect()
{
    /*
        Close connection to the OpenMV camera
    */
    if (transport) {
        delete transport;
        transport = nullptr;
    }

    channelsById.clear();
    channelsByName.clear();
    sysinfo.clear();
    pendingChannelEvents = 0;
    frameEvent = false;
    scriptState = false;
}

bool OMVCamera::isConnected() const
{
    /*
        Check if connected to camera
    */
    return serial && serial->isOpen() && transport;
}

void OMVCamera::pollEvents()
{
    if (!transport) {
        return;
    }
    transport->recv_packet(true); // poll_events = true
}

const OMVTransport::stats_t &OMVCamera::hostStats() const
{
    /*
        Get transport statistics
    */
    if (!transport) {
        static OMVTransport::stats_t emptyStats = {};
        return emptyStats;
    }
    return transport->stats;
}

// Low-level command send/recv with resync translation
QByteArray OMVCamera::sendCmdWaitResp(uint8_t opcode,
                                      uint8_t channel,
                                      const QByteArray &data)
{
    /*
        Send a command and wait for response (ACK/NAK or data)
    */
    if (!isConnected()) {
        throw OMVPException(QStringLiteral("Not connected"));
    }

    // Special handling for reset commands - they never return
    if (opcode == OMVPOpcode::SYS_RESET || opcode == OMVPOpcode::SYS_BOOT) {
        transport->send_packet(opcode, channel, 0, data);
        disconnect();  // Device will reset, connection lost
        return QByteArray();
    }

    try {
        transport->send_packet(opcode, channel, 0, data);
        QVariant resp = transport->recv_packet();

        if (!resp.isValid()) {
            return QByteArray();
        }

        if (resp.canConvert<QByteArray>()) {
            return resp.toByteArray();
        }

        // ACK-only (True in Python)
        if (resp.canConvert<bool>() && resp.toBool()) {
            return QByteArray();
        }

        return QByteArray();
    } catch (const OMVPException &) {
        resync();
        throw OMVPResyncException(QStringLiteral("Resync requested"));
    } catch (const std::exception &e) {
        qCritical() << "sendCmdWaitResp exception:" << e.what();
        throw OMVPException(QString::fromUtf8(e.what()));
    } catch (...) {
        qCritical() << "sendCmdWaitResp unknown exception";
        throw OMVPException(QStringLiteral("Unknown error in sendCmdWaitResp"));
    }
}

void OMVCamera::handleEvent(uint8_t channel_id, uint16_t event)
{
    /*
        Handle events from the device
    */
    if (channel_id == 0) {
        // System events
        QString event_name;
        if (static_cast<uint16_t>(event) <= static_cast<uint16_t>(OMVPEventType::SOFT_REBOOT)) {
            event_name = eventTypeName(event);
        } else {
            event_name = QStringLiteral("0x%1").arg(event, 4, 16, QChar('0')).toUpper();
        }

        qInfo().noquote() << "System Event: channel=system, event=" << event_name;

        if (event == static_cast<uint16_t>(OMVPEventType::SOFT_REBOOT)) {
            qInfo() << "Soft Reboot triggered";
        } else if (event == static_cast<uint16_t>(OMVPEventType::CHANNEL_REGISTERED)) {
            pendingChannelEvents += 1;
        }
    } else if (channelsById.contains(channel_id)) {
        // Channel events
        const ChannelInfo &ch = channelsById[channel_id];
        QString event_type;

        if (ch.name == QStringLiteral("stream")) {
            frameEvent = true;
            event_type = QStringLiteral(" (Frame Ready)");
        } else if (ch.name == QStringLiteral("stdin")) {
            scriptState = (event == 1);
            event_type = scriptState
            ? QStringLiteral(" (Script Started)")
            : QStringLiteral(" (Script Stopped)");
        }

        qInfo().noquote().nospace()
            << "Channel Event: channel=" << ch.name
            << ", event=0x" << QString::number(event, 16).rightJustified(4, QChar('0')).toUpper()
            << event_type;
    } else {
        qWarning().noquote().nospace()
        << "️Unknown Event: channel=" << channel_id
        << ", event=0x"
        << QString::number(event, 16).rightJustified(4, QChar('0')).toUpper();
    }
}

void OMVCamera::resync()
{
    qInfo() << "Resynchronizing";

    if (!serial || !serial->isOpen()) {
        throw OMVPTimeoutException(QStringLiteral("Serial not open for resync"));
    }

    if (transport) {
        delete transport;
        transport = nullptr;
    }

    // Use the protocol defaults for the initial connection
    transport = new OMVTransport(serial,
                                 /*crc*/ true,
                                 /*seq*/ true,
                                 /*max_payload*/ OMVProto::MIN_PAYLOAD_SIZE,
                                 /*timeout*/ timeoutSec,
                                 /*event_callback*/ [this](uint8_t ch, uint16_t ev) {
                                     this->handleEvent(ch, ev);
                                 },
                                 /*drop_rate*/ dropRate);

    // Perform resync sequence on timeout
    for (int attempt = 0; attempt < maxRetry; ++attempt) {
        try {
            transport->reset_sequence();
            transport->send_packet(OMVPOpcode::PROTO_SYNC, 0, 0);
            QVariant ok = transport->recv_packet();
            if (ok.isValid()) {
                transport->reset_sequence();
                break;
            }
        } catch (const OMVPException &e) {
            if (attempt < maxRetry - 1) {
                qWarning().noquote() << e.what() << "-"
                << "Sync attempt" << (attempt + 1) << "failed, retrying...";
                continue;
            } else {
                qCritical() << "Failed to resync after maximum attempts";
                throw OMVPTimeoutException(
                    QStringLiteral("Resync failed - unable to synchronize with device"));
            }
        }
    }

    // Set protocol configuration to user-requested values
    updateCapabilities();

    // Update transport with final negotiated capabilities
    transport->update_caps(caps_crc, caps_seq, caps_ack, caps_max_payload);
}

// Channel helpers

bool OMVCamera::channelLock(uint8_t channel_id)
{
    /*
        Lock a data channel
    */
    QByteArray resp = sendCmdWaitResp(OMVPOpcode::CHANNEL_LOCK, channel_id);
    Q_UNUSED(resp);
    // If no exception, treat as success
    return true;
}

bool OMVCamera::channelUnlock(uint8_t channel_id)
{
    /*
        Lock a data channel
    */
    QByteArray resp = sendCmdWaitResp(OMVPOpcode::CHANNEL_UNLOCK, channel_id);
    Q_UNUSED(resp);
    return true;
}

uint32_t OMVCamera::channelSizeRaw(uint8_t channel_id)
{
    /*
        Get available data size for a channel
    */
    QByteArray payload = sendCmdWaitResp(OMVPOpcode::CHANNEL_SIZE, channel_id);
    if (payload.size() < 4) {
        throw OMVPException(QStringLiteral("Invalid CHANNEL_SIZE payload"));
    }
    QDataStream ds(payload);
    ds.setByteOrder(QDataStream::LittleEndian);
    uint32_t size = 0;
    ds >> size;
    return size;
}

QVector<uint32_t> OMVCamera::channelShape(uint8_t channel_id)
{
    /*
        Get available data size for a channel
    */
    QByteArray payload = sendCmdWaitResp(OMVPOpcode::CHANNEL_SHAPE, channel_id);
    QVector<uint32_t> shape;
    if (payload.isEmpty()) {
        return shape;
    }

    if (payload.size() % 4 != 0) {
        throw OMVPException(QStringLiteral("Invalid CHANNEL_SHAPE payload size"));
    }

    QDataStream ds(payload);
    ds.setByteOrder(QDataStream::LittleEndian);
    while (!ds.atEnd()) {
        uint32_t v = 0;
        ds >> v;
        shape.append(v);
    }
    return shape;
}

QByteArray OMVCamera::channelReadRaw(uint8_t channel_id, uint32_t offset, uint32_t length)
{
    /*
        Read data from a channel (protocol handles fragmentation automatically)
    */
    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << offset << length;

    QByteArray data = sendCmdWaitResp(OMVPOpcode::CHANNEL_READ, channel_id, payload);
    return data;
}

void OMVCamera::channelWriteRaw(uint8_t channel_id, const QByteArray &data, uint32_t offset)
{
    /*
        Write data to a channel with automatic packet splitting
    */
    uint32_t chunk_size = caps_max_payload > 8 ? (caps_max_payload - 8) : 0;

    for (qsizetype start = 0; start < data.size(); start += chunk_size) {
        qsizetype len = qMin(qsizetype(chunk_size), data.size() - start);
        QByteArray payload;
        QDataStream ds(&payload, QIODevice::WriteOnly);
        ds.setByteOrder(QDataStream::LittleEndian);
        ds << (offset + uint32_t(start)) << uint32_t(len);
        payload.append(data.constData() + start, len);
        sendCmdWaitResp(OMVPOpcode::CHANNEL_WRITE, channel_id, payload);
    }
}

QByteArray OMVCamera::channelIoctl(uint8_t channel_id,
                                   uint32_t cmd,
                                   const char *fmt,
                                   const QList<uint32_t> &args)
{
    /*
        Perform ioctl operation on a channel
    */
    QByteArray payload;
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << cmd;

    if (fmt && *fmt && !args.isEmpty()) {
        // Python packs '<' + fmt, all 'I' in current usage.
        for (uint32_t v : args) {
            ds << v;
        }
    }

    return sendCmdWaitResp(OMVPOpcode::CHANNEL_IOCTL, channel_id, payload);
}

QMap<uint8_t, OMVCamera::ChannelInfo> OMVCamera::channelList()
{
    /*
        List registered channels on the device
    */
    QMap<uint8_t, ChannelInfo> channels;
    const int entry_size = 16;  // bytes: 1 (id) + 1 (flags) + 14 (name)

    QByteArray payload = sendCmdWaitResp(OMVPOpcode::CHANNEL_LIST);
    if (payload.isEmpty()) {
        return channels;
    }

    const int num_channels = payload.size() / entry_size;

    for (int i = 0; i < num_channels; ++i) {
        const int offset = i * entry_size;
        const uchar *p = reinterpret_cast<const uchar *>(payload.constData() + offset);

        uint8_t cid   = p[0];
        uint8_t flags = p[1];

        QByteArray raw_name(reinterpret_cast<const char *>(p + 2), 14);
        int null_pos = raw_name.indexOf('\0');
        if (null_pos >= 0) {
            raw_name.truncate(null_pos);
        }

        ChannelInfo info;
        info.name  = QString::fromUtf8(raw_name);
        info.flags = flags;

        channels.insert(cid, info);
    }

    return channels;
}

void OMVCamera::updateChannels()
{
    /*
        Update channel list from device
    */
    if (pendingChannelEvents > 0) {
        pendingChannelEvents -= 1;
    }

    channelsById = channelList();
    channelsByName.clear();

    for (auto it = channelsById.cbegin(); it != channelsById.cend(); ++it) {
        channelsByName.insert(it.value().name, it.key());
    }

    qInfo().nospace() << "Registered channels (" << channelsById.size() << "):";
    for (auto it = channelsById.cbegin(); it != channelsById.cend(); ++it) {
        qInfo().noquote().nospace()
        << "  ID: " << it.key()
        << ", Flags: 0x"
        << QString::number(it.value().flags, 16).rightJustified(2, QChar('0')).toUpper()
        << ", Name: " << it.value().name;
    }
}

uint8_t OMVCamera::getChannelId(const QString &name)
{
    /*
        Get channel ID by name with lazy loading
    */
    if (pendingChannelEvents > 0) {
        updateChannels();
    }
    return channelsByName.value(name, 0);
}

QString OMVCamera::getChannelName(uint8_t channel_id) const
{
    /*
        Get channel name by ID
    */
    if (channelsById.contains(channel_id)) {
        return channelsById.value(channel_id).name;
    }
    return QString();
}

// Public high-level API (Python @retry_if_failed equivalents)

QVariantMap OMVCamera::deviceStats()
{
    /*
        Get protocol statistics
    */
    return retryIfFailed([this]() -> QVariantMap {
        QByteArray payload = sendCmdWaitResp(OMVPOpcode::PROTO_STATS);
        if (payload.size() < 32) {
            throw OMVPException(
                QStringLiteral("Invalid PROTO_STATS payload size: %1").arg(payload.size()));
        }

        QDataStream ds(payload);
        ds.setByteOrder(QDataStream::LittleEndian);
        uint32_t data[8] = {};
        for (int i = 0; i < 8; ++i) {
            ds >> data[i];
        }

        QVariantMap m;
        m.insert(QStringLiteral("sent"), data[0]);
        m.insert(QStringLiteral("received"), data[1]);
        m.insert(QStringLiteral("checksum"), data[2]);
        m.insert(QStringLiteral("sequence"), data[3]);
        m.insert(QStringLiteral("retransmit"), data[4]);
        m.insert(QStringLiteral("transport"), data[5]);
        m.insert(QStringLiteral("sent_events"), data[6]);
        m.insert(QStringLiteral("max_ack_queue_depth"), data[7]);
        return m;
    });
}

void OMVCamera::reset()
{
    /*
        Reset the camera
    */
    retryIfFailedVoid([this]() {
        sendCmdWaitResp(OMVPOpcode::SYS_RESET);
    });
}

void OMVCamera::boot()
{
    /*
        Jump to bootloader
    */
    retryIfFailedVoid([this]() {
        sendCmdWaitResp(OMVPOpcode::SYS_BOOT);
    });
}

void OMVCamera::updateCapabilities()
{
    /*
        Set device capabilities
    */
    retryIfFailedVoid([this]() {
        QByteArray payload = sendCmdWaitResp(OMVPOpcode::PROTO_GET_CAPS);
        if (payload.size() < 6) {
            throw OMVPException(
                QStringLiteral("Invalid PROTO_GET_CAPS payload size: %1").arg(payload.size()));
        }

        QDataStream ds(payload);
        ds.setByteOrder(QDataStream::LittleEndian);
        uint32_t flags_remote = 0;
        uint16_t max_payload_remote = 0;
        ds >> flags_remote >> max_payload_remote;
        Q_UNUSED(flags_remote);

        uint32_t flags =
            (caps_crc    ? 1u : 0u) |
            (caps_seq    ? (1u << 1) : 0u) |
            (caps_ack    ? (1u << 2) : 0u) |
            (caps_events ? (1u << 3) : 0u);

        caps_max_payload = qMin<uint16_t>(max_payload_remote, caps_max_payload);

        QByteArray out;
        QDataStream ods(&out, QIODevice::WriteOnly);
        ods.setByteOrder(QDataStream::LittleEndian);
        ods << flags << caps_max_payload;
        // 10x padding: already zero as QDataStream does not auto pad; we need explicit.
        // Python used '<IH10x'. Here we just ensure same first 6 bytes; rest may be ignored.
        out.resize(16); // minimal header; firmware ignores extra bytes after caps.

        QByteArray response = sendCmdWaitResp(OMVPOpcode::PROTO_SET_CAPS, 0, out);
        Q_UNUSED(response);
    });
}

void OMVCamera::stop()
{
    /*
        Stop running script
    */
    retryIfFailedVoid([this]() {
        uint8_t stdin_id = getChannelId(QStringLiteral("stdin"));
        if (stdin_id) {
            channelIoctl(stdin_id, static_cast<uint32_t>(OMVPChannelIOCTL::STDIN_STOP));
            scriptState = false;
        }
    });
}

void OMVCamera::exec(const QString &script)
{
    /*
        Write and execute a script
    */
    retryIfFailedVoid([this, script]() {
        uint8_t stdin_id = getChannelId(QStringLiteral("stdin"));
        if (!stdin_id) {
            return;
        }

        // Reset script buffer
        channelIoctl(stdin_id, static_cast<uint32_t>(OMVPChannelIOCTL::STDIN_RESET));

        // Upload script data
        QByteArray utf8 = script.toUtf8();
        channelWriteRaw(stdin_id, utf8);

        // Execute the script
        channelIoctl(stdin_id, static_cast<uint32_t>(OMVPChannelIOCTL::STDIN_EXEC));

        scriptState = true;
    });
}

void OMVCamera::streaming(bool enable, bool raw, const QSize &res)
{
    /*
        Enable or disable streaming
    */
    retryIfFailedVoid([this, enable, raw, res]() {
        uint8_t stream_id = getChannelId(QStringLiteral("stream"));
        if (!stream_id) {
            return;
        }

        if (raw && res.isValid()) {
            QList<uint32_t> args;
            args << uint32_t(res.width()) << uint32_t(res.height());
            channelIoctl(stream_id,
                         static_cast<uint32_t>(OMVPChannelIOCTL::STREAM_RAW_CFG),
                         "II",
                         args);
        }

        {
            QList<uint32_t> args;
            args << (raw ? 1u : 0u);
            channelIoctl(stream_id,
                         static_cast<uint32_t>(OMVPChannelIOCTL::STREAM_RAW_CTRL),
                         "I",
                         args);
        }

        {
            QList<uint32_t> args;
            args << (enable ? 1u : 0u);
            channelIoctl(stream_id,
                         static_cast<uint32_t>(OMVPChannelIOCTL::STREAM_CTRL),
                         "I",
                         args);
        }
    });
}

QVariantMap OMVCamera::readStatus()
{
    /*
        Poll channels status and return a dictionary of channel readiness
    */
    return retryIfFailed([this]() -> QVariantMap {
        QByteArray payload = sendCmdWaitResp(OMVPOpcode::CHANNEL_POLL);
        if (payload.size() < 4) {
            throw OMVPException(
                QStringLiteral("Invalid CHANNEL_POLL payload size: %1").arg(payload.size()));
        }

        QDataStream ds(payload);
        ds.setByteOrder(QDataStream::LittleEndian);
        uint32_t flags = 0;
        ds >> flags;

        QVariantMap result;
        if (pendingChannelEvents > 0) {
            updateChannels();
        }

        for (auto it = channelsByName.cbegin(); it != channelsByName.cend(); ++it) {
            bool ready = (flags & (1u << it.value())) != 0;
            result.insert(it.key(), ready);
        }

        return result;
    });
}

void OMVCamera::profilerReset()
{
    /*
        Reset the profiler data
    */
    retryIfFailedVoid([this]() {
        uint8_t profile_id = getChannelId(QStringLiteral("profile"));
        if (!profile_id) {
            return;
        }
        channelIoctl(profile_id, static_cast<uint32_t>(OMVPChannelIOCTL::PROFILE_RESET));
        qDebug() << "Profiler reset";
    });
}

void OMVCamera::profilerMode(bool exclusive)
{
    /*
        Set profiler mode (exclusive=True for exclusive, False for inclusive)
    */
    retryIfFailedVoid([this, exclusive]() {
        uint8_t profile_id = getChannelId(QStringLiteral("profile"));
        if (!profile_id) {
            return;
        }
        QList<uint32_t> args;
        args << (exclusive ? 1u : 0u);
        channelIoctl(profile_id,
                     static_cast<uint32_t>(OMVPChannelIOCTL::PROFILE_MODE),
                     "I",
                     args);
        qDebug() << "Profile mode set to"
                 << (exclusive ? "exclusive" : "inclusive");
    });
}

void OMVCamera::profilerEventType(uint32_t counter_num, uint32_t event_id)
{
    /*
        Configure an event counter to monitor a specific event
    */
    retryIfFailedVoid([this, counter_num, event_id]() {
        uint8_t profile_id = getChannelId(QStringLiteral("profile"));
        if (!profile_id) {
            return;
        }
        QList<uint32_t> args;
        args << counter_num << event_id;
        channelIoctl(profile_id,
                     static_cast<uint32_t>(OMVPChannelIOCTL::PROFILE_SET_EVENT),
                     "II",
                     args);
        qDebug().noquote()
            << "Event counter" << counter_num
            << "set to event 0x"
            << QString::number(event_id, 16).rightJustified(4, QChar('0')).toUpper();
    });
}

QVariantList OMVCamera::readProfile()
{
    /*
        Read profiler data from the profile channel
    */
    return retryIfFailed([this]() -> QVariantList {
        QVariantList records;

        uint8_t profile_id = getChannelId(QStringLiteral("profile"));
        if (!profile_id) {
            return records;
        }

        // Get event count from cached system info (pmu_eventcnt field)
        uint32_t event_count = sysinfo.value(QStringLiteral("pmu_eventcnt")).toUInt();

        // Lock the profile channel
        if (!channelLock(profile_id)) {
            return records;
        }

        // All early exits below must unlock before returning
        QVector<uint32_t> shape = channelShape(profile_id);
        if (shape.size() < 2) {
            channelUnlock(profile_id);
            return records;
        }

        uint32_t record_count = shape[0];
        uint32_t record_size  = shape[1];
        uint32_t profile_size = record_count * record_size;
        if (profile_size == 0) {
            channelUnlock(profile_id);
            return records;
        }

        QByteArray data = channelReadRaw(profile_id, 0, profile_size);
        if (data.isEmpty()) {
            channelUnlock(profile_id);
            return records;
        }

        try {
            QDataStream ds(data);
            ds.setByteOrder(QDataStream::LittleEndian);

            for (uint32_t i = 0; i < record_count; ++i) {
                const qsizetype offset = qsizetype(i) * qsizetype(record_size);
                if (offset + qsizetype(record_size) > data.size()) {
                    break;
                }

                // Seek to start of this record
                ds.device()->seek(offset);

                // address, caller, call_count, min_ticks, max_ticks (5 x uint32)
                uint32_t address    = 0;
                uint32_t caller     = 0;
                uint32_t call_count = 0;
                uint32_t min_ticks  = 0;
                uint32_t max_ticks  = 0;

                ds >> address
                    >> caller
                    >> call_count
                    >> min_ticks
                    >> max_ticks;

                // total_ticks, total_cycles (2 x uint64)
                uint64_t total_ticks  = 0;
                uint64_t total_cycles = 0;
                ds >> total_ticks
                    >> total_cycles;

                // events: event_count x uint64
                QList<QVariant> events;
                events.reserve(int(event_count));
                for (uint32_t e = 0; e < event_count; ++e) {
                    uint64_t evv = 0;
                    ds >> evv;
                    events.append(QVariant::fromValue(evv));
                }

                // trailing uint32 (the final "I" in "<5I2Q{event_count}QI"), currently unused
                uint32_t tail = 0;
                ds >> tail;
                Q_UNUSED(tail);

                QVariantMap rec;
                rec.insert(QStringLiteral("address"),       address);
                rec.insert(QStringLiteral("caller"),        caller);
                rec.insert(QStringLiteral("call_count"),    call_count);
                rec.insert(QStringLiteral("min_ticks"),     min_ticks);
                rec.insert(QStringLiteral("max_ticks"),     max_ticks);
                rec.insert(QStringLiteral("total_ticks"),   total_ticks);
                rec.insert(QStringLiteral("total_cycles"),  total_cycles);
                rec.insert(QStringLiteral("events"),        events);

                records.append(rec);
            }

            channelUnlock(profile_id);
            return records;
        } catch (...) {
            channelUnlock(profile_id);
            throw;
        }
    });
}

QString OMVCamera::readStdout()
{
    /*
        Read text output buffer
    */
    return retryIfFailed([this]() -> QString {
        uint8_t stdout_id = getChannelId(QStringLiteral("stdout"));
        if (!stdout_id) {
            return QString();
        }

        uint32_t size = channelSizeRaw(stdout_id);
        if (!size) {
            return QString();
        }

        QByteArray data = channelReadRaw(stdout_id, 0, size);
        return QString::fromUtf8(data);
    });
}

bool OMVCamera::readFrame(OMVFrame &outFrame)
{
    /*
        Read stream buffer data with header at the beginning and convert to RGB888
    */
    return retryIfFailed([this, &outFrame]() -> bool {
        uint8_t stream_id = getChannelId(QStringLiteral("stream"));
        if (!stream_id) {
            return false;
        }

        if (!channelLock(stream_id)) {
            return false;
        }

        frameEvent = false;

        try {
            uint32_t size = channelSizeRaw(stream_id);
            if (size <= 16) {
                channelUnlock(stream_id);
                return false;
            }

            QByteArray data = channelReadRaw(stream_id, 0, size);
            if (data.size() < 16) {
                channelUnlock(stream_id);
                return false;
            }

            QDataStream ds(data);
            ds.setByteOrder(QDataStream::LittleEndian);
            uint32_t width  = 0;
            uint32_t height = 0;
            uint32_t pixfmt = 0;
            uint32_t depth  = 0;
            ds >> width >> height >> pixfmt >> depth;

            QByteArray raw_data = data.mid(16);

            QString fmt_str;
            QPixmap pm = convert_to_rgb888(raw_data,
                                           int(width),
                                           int(height),
                                           pixfmt,
                                           &fmt_str);
            if (pm.isNull()) {
                channelUnlock(stream_id);
                return false;
            }

            outFrame.width   = int(width);
            outFrame.height  = int(height);
            outFrame.format  = pixfmt;
            outFrame.depth   = depth;
            outFrame.pixmap  = pm;
            outFrame.raw_size = raw_data.size();

            channelUnlock(stream_id);
            return true;
        } catch (...) {
            channelUnlock(stream_id);
            throw;
        }
    });
}

qsizetype OMVCamera::channelSize(const QString &channel)
{
    /*
        Get size of data available in a custom channel
    */
    return retryIfFailed([this, channel]() -> qsizetype {
        uint8_t channel_id = getChannelId(channel);
        if (!channel_id) {
            return 0;
        }
        return qsizetype(channelSizeRaw(channel_id));
    });
}

QByteArray OMVCamera::channelRead(const QString &channel, qsizetype size)
{
    /*
        Read data from a custom channel
    */
    return retryIfFailed([this, channel, size]() -> QByteArray {
        uint8_t channel_id = getChannelId(channel);
        if (!channel_id) {
            return QByteArray();
        }

        uint32_t len = 0;
        if (size < 0) {
            len = channelSizeRaw(channel_id);
        } else {
            len = uint32_t(size);
        }

        return channelReadRaw(channel_id, 0, len);
    });
}

bool OMVCamera::channelWrite(const QString &channel, const QByteArray &data)
{
    /*
        Write data to a custom channel
    */
    return retryIfFailed([this, channel, data]() -> bool {
        uint8_t channel_id = getChannelId(channel);
        if (!channel_id) {
            return false;
        }
        channelWriteRaw(channel_id, data);
        return true;
    });
}

bool OMVCamera::hasChannel(const QString &channel) const
{
    /*
        Check if a channel exists
    */
    return channelsByName.contains(channel);
}

QVariantMap OMVCamera::systemInfo()
{
    /*
        Get system information
    */
    return retryIfFailed([this]() -> QVariantMap {
        QByteArray payload = sendCmdWaitResp(OMVPOpcode::SYS_INFO);
        if (payload.size() < 80) {
            throw OMVPException(
                QStringLiteral("Invalid SYS_INFO payload size: %1").arg(payload.size()));
        }

        QDataStream ds(payload);
        ds.setByteOrder(QDataStream::LittleEndian);

        uint32_t cpu_id = 0;
        uint32_t device_id[3] = {};
        uint32_t sensor_chip_id[3] = {};
        uint32_t id_reserved[2] = {};
        uint32_t hw_caps[2] = {};
        uint32_t memory[6] = {};

        ds >> cpu_id;
        for (int i = 0; i < 3; ++i) ds >> device_id[i];
        for (int i = 0; i < 3; ++i) ds >> sensor_chip_id[i];
        for (int i = 0; i < 2; ++i) ds >> id_reserved[i];
        for (int i = 0; i < 2; ++i) ds >> hw_caps[i];
        for (int i = 0; i < 6; ++i) ds >> memory[i];

        QByteArray fw_ver(3, Qt::Uninitialized);
        QByteArray proto_ver(3, Qt::Uninitialized);
        QByteArray boot_ver(3, Qt::Uninitialized);

        ds.readRawData(fw_ver.data(), 3);
        ds.readRawData(proto_ver.data(), 3);
        ds.readRawData(boot_ver.data(), 3);
        // padding 3x
        ds.skipRawData(3);

        uint32_t capabilities  = hw_caps[0];
        uint32_t capabilities2 = hw_caps[1];
        Q_UNUSED(capabilities2);

        QVariantMap m;
        m.insert(QStringLiteral("cpu_id"), cpu_id);

        QVariantList dev_list;
        for (int i = 0; i < 3; ++i) dev_list << device_id[i];
        m.insert(QStringLiteral("device_id"), dev_list);

        QVariantList chip_list;
        for (int i = 0; i < 3; ++i) chip_list << sensor_chip_id[i];
        m.insert(QStringLiteral("sensor_chip_id"), chip_list);

        m.insert(QStringLiteral("gpu_present"),  bool(capabilities & (1u << 0)));
        m.insert(QStringLiteral("npu_present"),  bool(capabilities & (1u << 1)));
        m.insert(QStringLiteral("isp_present"),  bool(capabilities & (1u << 2)));
        m.insert(QStringLiteral("venc_present"), bool(capabilities & (1u << 3)));
        m.insert(QStringLiteral("jpeg_present"), bool(capabilities & (1u << 4)));
        m.insert(QStringLiteral("dram_present"), bool(capabilities & (1u << 5)));
        m.insert(QStringLiteral("crc_present"),  bool(capabilities & (1u << 6)));
        m.insert(QStringLiteral("pmu_present"),  bool(capabilities & (1u << 7)));
        m.insert(QStringLiteral("pmu_eventcnt"), (capabilities >> 8) & 0xFFu);
        m.insert(QStringLiteral("wifi_present"), bool(capabilities & (1u << 16)));
        m.insert(QStringLiteral("bt_present"),   bool(capabilities & (1u << 17)));
        m.insert(QStringLiteral("sd_present"),   bool(capabilities & (1u << 18)));
        m.insert(QStringLiteral("eth_present"),  bool(capabilities & (1u << 19)));
        m.insert(QStringLiteral("usb_highspeed"), bool(capabilities & (1u << 20)));
        m.insert(QStringLiteral("multicore_present"), bool(capabilities & (1u << 21)));

        m.insert(QStringLiteral("flash_size_kb"),        memory[0]);
        m.insert(QStringLiteral("ram_size_kb"),          memory[1]);
        m.insert(QStringLiteral("framebuffer_size_kb"),  memory[2]);
        m.insert(QStringLiteral("stream_buffer_size_kb"), memory[3]);

        QVariantList fw_v;
        fw_v << uint8_t(fw_ver[0]) << uint8_t(fw_ver[1]) << uint8_t(fw_ver[2]);
        m.insert(QStringLiteral("firmware_version"), fw_v);

        QVariantList proto_v;
        proto_v << uint8_t(proto_ver[0]) << uint8_t(proto_ver[1]) << uint8_t(proto_ver[2]);
        m.insert(QStringLiteral("protocol_version"), proto_v);

        QVariantList boot_v;
        boot_v << uint8_t(boot_ver[0]) << uint8_t(boot_ver[1]) << uint8_t(boot_ver[2]);
        m.insert(QStringLiteral("bootloader_version"), boot_v);

        sysinfo = m; // cache
        return m;
    });
}

void OMVCamera::printSystemInfo()
{
    /*
        Print formatted system information
    */
    qInfo() << "=== OpenMV System Information ===";

    qInfo().noquote().nospace()
        << "CPU ID: 0x"
        << QString::number(sysinfo.value(QStringLiteral("cpu_id")).toUInt(),
                           16).rightJustified(8, QChar('0')).toUpper();

    // Device ID is now an array of 3 words
    QVariantList dev_id_list = sysinfo.value(QStringLiteral("device_id")).toList();
    QString dev_id_hex;
    for (const QVariant &v : std::as_const(dev_id_list)) {
        dev_id_hex += QString::number(v.toUInt(), 16).rightJustified(8, QChar('0')).toUpper();
    }
    qInfo().noquote() << "Device ID:" << dev_id_hex;

    // Sensor Chip IDs are now an array of 3 words
    QVariantList chip_list = sysinfo.value(QStringLiteral("sensor_chip_id")).toList();
    for (int i = 0; i < chip_list.size(); ++i) {
        uint32_t chip_id = chip_list[i].toUInt();
        if (chip_id != 0) {
            qInfo().noquote()
            << QStringLiteral("CSI%1: 0x%2")
                    .arg(i)
                    .arg(QString::number(chip_id, 16).rightJustified(4, QChar('0')).toUpper());
        }
    }

    // Memory info
    auto print_if_positive = [&](const char *label, const char *key) {
        uint32_t val = sysinfo.value(QString::fromLatin1(key)).toUInt();
        if (val > 0) {
            qInfo().noquote().nospace()
            << label << ": " << val << "KB";
        }
    };

    print_if_positive("Flash", "flash_size_kb");
    print_if_positive("RAM", "ram_size_kb");
    print_if_positive("Framebuffer", "framebuffer_size_kb");
    print_if_positive("Stream Buffer", "stream_buffer_size_kb");

    // Hardware capabilities
    qInfo() << "Hardware capabilities:";
    auto yn = [&](const char *key) {
        return sysinfo.value(QString::fromLatin1(key)).toBool() ? "Yes" : "No";
    };

    qInfo().noquote() << "  GPU:" << yn("gpu_present");
    qInfo().noquote() << "  NPU:" << yn("npu_present");
    qInfo().noquote() << "  ISP:" << yn("isp_present");
    qInfo().noquote() << "  Video Encoder:" << yn("venc_present");
    qInfo().noquote() << "  JPEG Encoder:" << yn("jpeg_present");
    qInfo().noquote() << "  DRAM:" << yn("dram_present");
    qInfo().noquote() << "  CRC Hardware:" << yn("crc_present");
    qInfo().noquote().nospace()
        << "  PMU: "
        << yn("pmu_present")
        << " (" << sysinfo.value(QStringLiteral("pmu_eventcnt")).toUInt()
        << " counters)";

    qInfo().noquote() << "  Multi-core:" << yn("multicore_present");
    qInfo().noquote() << "  WiFi:" << yn("wifi_present");
    qInfo().noquote() << "  Bluetooth:" << yn("bt_present");
    qInfo().noquote() << "  SD Card:" << yn("sd_present");
    qInfo().noquote() << "  Ethernet:" << yn("eth_present");
    qInfo().noquote() << "  USB High-Speed:" << yn("usb_highspeed");

    // Profiler info
    bool profile_available = channelsByName.contains(QStringLiteral("profile"));
    qInfo().noquote()
        << "Profiler:"
        << (profile_available ? "Available" : "Not available");

    // Version info
    auto print_ver = [&](const char *label, const char *key) {
        QVariantList v = sysinfo.value(QString::fromLatin1(key)).toList();
        if (v.size() == 3) {
            qInfo().noquote().nospace()
            << label << " version: "
            << v[0].toUInt() << "."
            << v[1].toUInt() << "."
            << v[2].toUInt();
        }
    };

    print_ver("Firmware", "firmware_version");
    print_ver("Protocol", "protocol_version");
    print_ver("Bootloader", "bootloader_version");

    qInfo().noquote().nospace()
        << "Protocol capabilities: "
        << "CRC=" << caps_crc
        << ", SEQ=" << caps_seq
        << ", ACK=" << caps_ack
        << ", EVENTS=" << caps_events
        << ", PAYLOAD=" << caps_max_payload;

    qInfo() << "=================================";
}

} // namespace omv
