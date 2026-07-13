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

#include "omv_camera.h"
#include "omv_constants.h"
#include "omv_debug.h"
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
    , resyncPending(false)
    , frameEvent(false)
    , scriptState(false)
    , streamingEnabled(true)
    , rawStreaming(false)
    , streamingRes(QSize(640, 480))
{
}

OMVCamera::~OMVCamera()
{
    disconnect();
}

// Overall connect budget. The handshake gives up (throws) once this is spent, so a bad link fails
// cleanly instead of churning resyncs forever. Actual give-up is this plus one in-flight wait.
static const qint64 kConnectDeadlineMs = 10000;

void OMVCamera::checkConnectDeadline()
{
    if (m_connectDeadlineActive && m_connectDeadline.hasExpired(kConnectDeadlineMs)) {
        throw OMVPTimeoutException(
            QStringLiteral("Connect timed out after %1 seconds").arg(kConnectDeadlineMs / 1000));
    }
}

void OMVCamera::connect()
{
    /*
        Establish connection to the OpenMV camera
    */
    disconnect();

    // Bound the whole handshake (checked in the resync/resend loops). OMVPTimeoutException is not
    // caught by retryIfFailed, so it escapes straight out to the catch below.
    m_connectDeadline.start();
    m_connectDeadlineActive = true;

    try {
        // Perform resync (also creates transport)
        resync();

        // Cache channel list
        updateChannels();

        // Cache system info
        sysinfo = systemInfo();

        // Print system information
        printSystemInfo();

        // Update camera state variables
        QVariantMap status = readStatus();

        frameEvent = status.value(QStringLiteral("stream")).toBool();
        scriptState = status.value(QStringLiteral("stdin")).toBool();
        m_connectDeadlineActive = false;
    } catch (...) {
        m_connectDeadlineActive = false;
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
        /*
            Ensure all data is sent before closing the serial port
        */
        if (serial) {
            serial->flush(); // ignore return

            QElapsedTimer elaspedTimer;
            elaspedTimer.start();

            while(serial->bytesToWrite()) {
                serial->waitForBytesWritten(1);
                if(serial->bytesToWrite() && elaspedTimer.hasExpired(1000)) {
                    break;
                }
            }
        }

        delete transport;
        transport = nullptr;
    }

    channelsById.clear();
    channelsByName.clear();
    eventCounts.clear();
    sysinfo.clear();
    pendingChannelEvents = 0;
    resyncPending = false;
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

QVariantMap OMVCamera::hostStats() const
{
    /*
        Get transport statistics
    */
    if (!transport) {
        return QVariantMap();
    }

    QVariantMap m;
    m.insert(QStringLiteral("sent"), transport->stats.sent);
    m.insert(QStringLiteral("received"), transport->stats.received);
    m.insert(QStringLiteral("checksum"), transport->stats.checksum);
    m.insert(QStringLiteral("sequence"), transport->stats.sequence);
    return m;

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
        omvDebug() << "🔌 Not connected";
        throw OMVPException(QStringLiteral("Not connected"));
    }

    if (resyncPending) {
        // Clear pending first as resync calls updateCapabilities which calls sendCmdWaitResp.
        resyncPending = false;
        resync();
    }

    // Special handling for reset commands - they never return
    if (opcode == OMVPOpcode::SYS_RESET || opcode == OMVPOpcode::SYS_BOOT) {
        transport->send_packet(opcode, channel, 0, data);
        disconnect();  // Device will reset, connection lost
        return QByteArray();
    }

    try {
        transport->send_packet(opcode, channel, 0, data);

        // Bulk-channel reads (stream frames, profiler dumps) are served zero-copy on the device
        // (readp) and travel as UDP fragments that can drop. readp-ness isn't on the wire, but
        // every readp channel is necessarily a LOCK channel (a raw pointer into a live buffer
        // needs a locked snapshot), while the copying reads (stdout ringbuf) are not -- so LOCK
        // identifies the UDP-routed reads. Once the transport has learned this link's
        // inter-fragment gap, bound the wait with that adaptive window so a lost tail costs
        // ~100ms instead of the full command timeout. Recovery is unchanged: a timeout still
        // resyncs (below) and retries. Control reads (TCP, reliable) keep the default timeout.
        // Plain map lookup -- getChannelId() could recurse via updateChannels().
        qint64 timeout_override = -1;
        if (opcode == OMVPOpcode::CHANNEL_READ &&
            (channelsById.value(channel).flags & OMVPChannelFlags::LOCK)) {
            timeout_override = transport->fragment_read_timeout_ms();
        }

        QVariant resp = transport->recv_packet(false, opcode == OMVPOpcode::CHANNEL_SIZE,
                                               timeout_override, int(opcode), int(channel));

        if ((opcode == OMVPOpcode::CHANNEL_LOCK || opcode == OMVPOpcode::CHANNEL_UNLOCK) &&
            resp.canConvert<bool>()) {
            return QByteArray(1, resp.toBool());
        }

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
    } catch (const OMVPException &e) {
        omvDebug() << "❌ sendCmdWaitResp exception:" << e.what();
        // Gracefully handle channel size requests during disconnect.
        if (opcode == OMVPOpcode::CHANNEL_SIZE) {
            return QByteArray(4, 0);
        }
        resync(false); // no grace timeout
        throw OMVPResyncException(QStringLiteral("Resync requested"));
    } catch (const std::exception &e) {
        omvDebug() << "❌ sendCmdWaitResp exception:" << e.what();
        throw OMVPException(QString::fromUtf8(e.what()));
    } catch (...) {
        omvDebug() << "❌ sendCmdWaitResp unknown exception";
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

        omvDebug().noquote() << "🔔 System Event: channel=system, event=" << event_name;

        if (event == static_cast<uint16_t>(OMVPEventType::SOFT_REBOOT)) {
            omvDebug() << "🔥 Soft Reboot triggered";
            resyncPending = true;
            // Reset here to handle any in-flight packets.
            transport->reset_sequence();
        } else if (event == static_cast<uint16_t>(OMVPEventType::CHANNEL_REGISTERED)) {
            pendingChannelEvents += 1;
        }
    } else if (channelsById.contains(channel_id)) {
        // Channel events
        const ChannelInfo &ch = channelsById[channel_id];
        QString event_type;

        eventCounts[channel_id]++;

        if (ch.name == QStringLiteral("stream")) {
            frameEvent = true;
            event_type = QStringLiteral(" (Frame Ready)");
            lastFrameReady.restart();
            lastframeReadyAndScriptRunning.restart();
        } else if (ch.name == QStringLiteral("stdin")) {
            scriptState = (event == 1);
            event_type = scriptState
            ? QStringLiteral(" (Script Started)")
            : QStringLiteral(" (Script Stopped)");
            frameEvent = false;
            lastFrameReady.restart();
            lastScriptRunning.restart();
            lastframeReadyAndScriptRunning.restart();
        }

        omvDebug().noquote().nospace()
            << "🔔 Channel Event: channel=" << ch.name
            << ", event=0x" << QString::number(event, 16).rightJustified(4, QChar('0')).toUpper()
            << event_type;
    } else {
        omvDebug().noquote().nospace()
        << "⚠️ Unknown Event: channel=" << channel_id
        << ", event=0x"
        << QString::number(event, 16).rightJustified(4, QChar('0')).toUpper();
    }
}

void OMVCamera::resync(bool grace_timeout)
{
    omvDebug() << "🔁 Resynchronizing";

    if (!serial || !serial->isOpen()) {
        throw OMVPTimeoutException(QStringLiteral("Serial not open for resync"));
    }

    // Perform resync sequence on timeout
    for (int attempt = 0; attempt < maxRetry; ++attempt) {
        checkConnectDeadline(); // give up the whole connect if its budget is spent
        if (transport) {
            delete transport;
            transport = nullptr;
        }

        // Per-port: fast enough for serial, tolerant of the multi-hundred-ms hiccups a weak WiFi
        // link shows (a 1s attempt window made resync itself fail under the same hiccup that
        // triggered it).
        const double graceTimeoutSec = serial->readStallTimeoutMs() / 1000.0;
        const double attemptTimeoutSec = grace_timeout
            ? (attempt ? graceTimeoutSec : qMax(timeoutSec, graceTimeoutSec))
            : graceTimeoutSec;
        // Use the protocol defaults for the initial connection
        transport = new OMVTransport(serial,
                                     /*crc*/ true,
                                     /*seq*/ true,
                                     /*max_payload*/ OMVProto::MIN_PAYLOAD_SIZE,
                                     /*timeout*/ attemptTimeoutSec,
                                     /*event_callback*/ [this](uint8_t ch, uint16_t ev) {
                                         this->handleEvent(ch, ev);
                                     },
                                     /*drop_rate*/ dropRate);

        try {
            transport->reset_sequence();
            transport->send_packet(OMVPOpcode::PROTO_SYNC, 0, 0);
            // Match the SYNC ACK specifically: during a resync the pipe still carries the dying
            // session's stragglers (frame fragments, stale responses), and accepting the first
            // completed packet as the handshake reply poisoned the negotiated caps.
            QVariant ok = transport->recv_packet(false, false, -1,
                                                 int(OMVPOpcode::PROTO_SYNC), 0);
            if (ok.isValid()) {
                transport->reset_sequence();
                break;
            }
        } catch (const OMVPException &e) {
            if (attempt < maxRetry - 1) {
                omvDebug().noquote() << "⚠️" << e.what() << "-"
                << "Sync attempt" << (attempt + 1) << "failed, retrying...";
                continue;
            } else {
                omvDebug() << "❌ Failed to resync after maximum attempts";
                delete transport;
                transport = nullptr;
                delete serial;
                serial = NULL;
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
    QByteArray a = sendCmdWaitResp(OMVPOpcode::CHANNEL_LOCK, channel_id);
    return (!a.isEmpty()) && a.at(0);
}

bool OMVCamera::channelUnlock(uint8_t channel_id)
{
    /*
        Unlock a data channel
    */
    QByteArray a = sendCmdWaitResp(OMVPOpcode::CHANNEL_UNLOCK, channel_id);
    return (!a.isEmpty()) && a.at(0);
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
        const QString &name = it.value().name;
        // On a name collision, prefer a dynamically-registered channel over a built-in one. The WiFi
        // debug agent registers its own dynamic "stdin" shadow so Run/Stop target its persistent-exec
        // channel instead of the C stdin channel, whose exec path soft-resets the cam every run.
        if (channelsByName.contains(name) && !(it.value().flags & OMVPChannelFlags::DYNAMIC)) {
            continue;
        }
        channelsByName.insert(name, it.key());
    }

    omvDebug().nospace() << "📋 Registered channels (" << channelsById.size() << "):";
    for (auto it = channelsById.cbegin(); it != channelsById.cend(); ++it) {
        omvDebug().noquote().nospace()
        << "📡   ID: " << it.key()
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

QVariantList OMVCamera::channelEventCounts() const
{
    /*
        Per-channel event totals for this session, in channel-id order.
    */
    QVariantList list;

    for (auto it = channelsById.constBegin(); it != channelsById.constEnd(); ++it) {
        QVariantMap m;
        m.insert(QStringLiteral("id"), it.key());
        m.insert(QStringLiteral("name"), it.value().name);
        m.insert(QStringLiteral("events"), eventCounts.value(it.key(), 0));
        list.append(m);
    }

    return list;
}

QVariantList OMVCamera::memoryStats()
{
    /*
        Get memory statistics: one GC-heap entry followed by one entry per
        UMA pool. SYS_MEMORY was added in protocol 1.0.2; gate on the cached
        protocol version so older firmware is never sent an opcode it would
        NAK (which costs a resync round). An empty list means "unsupported".
    */
    if (sysinfo.isEmpty()) {
        systemInfo(); // caches sysinfo + proto_v
    }

    int version = (proto_v.size() >= 3)
        ? ((proto_v.at(0).toInt() * 10000) + (proto_v.at(1).toInt() * 100) + proto_v.at(2).toInt())
        : 0;

    if (version < 10002) {
        return QVariantList();
    }

    return retryIfFailed([this]() -> QVariantList {
        QByteArray payload = sendCmdWaitResp(OMVPOpcode::SYS_MEMORY);
        if (payload.size() < 4) {
            throw OMVPException(
                QStringLiteral("Invalid SYS_MEMORY payload size: %1").arg(payload.size()));
        }

        // 4-byte header (count u8 + 3 reserved), then 24-byte entries:
        // type u8, reserved u8, flags u16, total/used/free/persist/peak u32 (LE).
        int count = uint8_t(payload.at(0));
        QVariantList entries;

        for (int i = 0; i < count; ++i) {
            int offset = 4 + (i * 24);
            if ((offset + 24) > payload.size()) {
                break;
            }

            QDataStream ds(payload.mid(offset, 24));
            ds.setByteOrder(QDataStream::LittleEndian);

            uint8_t type = 0, reserved = 0;
            uint16_t flags = 0;
            uint32_t total = 0, used = 0, free = 0, persist = 0, peak = 0;
            ds >> type >> reserved >> flags >> total >> used >> free >> persist >> peak;

            QVariantMap m;
            m.insert(QStringLiteral("mem_type"), (type == 0)
                ? QStringLiteral("gc") : QStringLiteral("uma"));
            m.insert(QStringLiteral("flags"), flags);
            m.insert(QStringLiteral("total"), total);
            m.insert(QStringLiteral("used"), used);
            m.insert(QStringLiteral("free"), free);
            m.insert(QStringLiteral("persist"), persist);
            m.insert(QStringLiteral("peak"), peak);
            entries.append(m);
        }

        return entries;
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
        int boot_major = boot_v.at(0).toInt();
        int boot_minor = boot_v.at(1).toInt();
        int boot_patch = boot_v.at(2).toInt();
        if((boot_major < 1)
        || ((boot_major == 1) && (boot_minor < 0))
        || ((boot_major == 1) && (boot_minor == 0) && (boot_patch < 2))) {
            // Bootloader can get stuck if forced on older versions.
            sendCmdWaitResp(OMVPOpcode::SYS_RESET);
        } else {
            sendCmdWaitResp(OMVPOpcode::SYS_BOOT);
        }
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
        // The cached channel map can go stale across a lossy reconnect, leaving "stdin" resolved
        // to the built-in channel instead of a debug agent's dynamic shadow -- Stop then no-ops
        // with a clean ACK. Stop is user-paced, so refresh the map before resolving.
        updateChannels();
        uint8_t stdin_id = getChannelId(QStringLiteral("stdin"));
        if (stdin_id) {
            channelIoctl(stdin_id, static_cast<uint32_t>(OMVPChannelIOCTL::STDIN_STOP));
            scriptState = false;
            // Anticipate that stopping the script may trigger a resync..
            if (!caps_events) resyncPending = true;
        }
    });
}

void OMVCamera::exec(const QString &script)
{
    /*
        Write and execute a script
    */
    retryIfFailedVoid([this, script]() {
        // Same map-refresh rationale as stop(): a stale "stdin" resolution sends the script to the
        // neutered built-in channel, where EXEC is ACK'd but nothing runs.
        updateChannels();
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

        // Store streaming state
        streamingEnabled = enable;
        rawStreaming     = raw;
        streamingRes     = res;
    });
}

void OMVCamera::setStreamSource(uint32_t chip_id)
{
    /*
        Select which sensor feeds the stream channel.
    */
    retryIfFailedVoid([&]() {
        uint8_t stream_id = getChannelId(QStringLiteral("stream"));
        QList<uint32_t> args;
        args << chip_id;
        channelIoctl(stream_id,
                     static_cast<uint32_t>(OMVPChannelIOCTL::STREAM_SOURCE),
                     "I",
                     args);
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
        omvDebug() << "📊 Profiler reset";
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
        omvDebug() << "📊 Profile mode set to"
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
        omvDebug().noquote()
            << "🔢 Event counter" << counter_num
            << "set to event 0x"
            << QString::number(event_id, 16).rightJustified(4, QChar('0')).toUpper();
    });
}

QVariantList OMVCamera::readProfile()
{
    /*
        Read profiler data from the profile channel
    */
    // Best-effort like readFrame: profiler dumps also ride lossy UDP, so don't let a second
    // consecutive lost read escape as an exception (the resync already repaired the link) --
    // return no records this poll instead. See readFrame for the full rationale.
    try {
        return readProfileInner();
    } catch (const OMVPResyncException &) {
        return QVariantList();
    }
}

QVariantList OMVCamera::readProfileInner()
{
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
            QThread::msleep(10); // Avoid busy loop if lock fails
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
        omvDebug().noquote().nospace() << "📊 Profiler records: " << record_count
                                       << ", record size: " << record_size
                                       << ", total size: " << profile_size;
        if (profile_size == 0) {
            channelUnlock(profile_id);
            return records;
        }

        QByteArray data = channelReadRaw(profile_id, 0, profile_size);
        if (data.size() != profile_size) {
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
                quint64 total_ticks  = 0;
                quint64 total_cycles = 0;
                ds >> total_ticks
                   >> total_cycles;

                // events: event_count x uint64
                QList<QVariant> events;
                events.reserve(int(event_count));
                for (uint32_t e = 0; e < event_count; ++e) {
                    quint64 evv = 0;
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
            omvDebug().noquote().nospace() << "📊 Parsed profiler records: " << record_count;

            channelUnlock(profile_id);
            return records;
        } catch (const OMVPTimeoutException &e) {
            throw;
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
        return QString::fromUtf8(data); // handle short reads.
    });
}

bool OMVCamera::readFrame(OMVFrame &outFrame)
{
    /*
        Read stream buffer data with header at the beginning and convert to RGB888
    */
    // Frame reads are best-effort: their data travels over lossy UDP, so under sustained loss
    // both retryIfFailed attempts can lose their read and the second OMVPResyncException would
    // escape -- where callers treat any exception as a dead connection and tear the camera
    // down. By the time that exception reaches here the resync has already repaired the link
    // (sequence counters realigned, channels unlocked), so a missing frame is not an error:
    // report "no frame this poll" and let the next poll proceed.
    try {
        return readFrameInner(outFrame);
    } catch (const OMVPResyncException &) {
        return false;
    }
}

bool OMVCamera::readFrameInner(OMVFrame &outFrame)
{
    return retryIfFailed([this, &outFrame]() -> bool {
        uint8_t stream_id = getChannelId(QStringLiteral("stream"));
        if (!stream_id) {
            return false;
        }

        if (!channelLock(stream_id)) {
            QThread::msleep(10); // Avoid busy loop if lock fails
            return false;
        }

        frameEvent = false;

        try {
            uint32_t size = channelSizeRaw(stream_id);
            omvDebug() << "📷 Frame Size:" << size;
            if (size <= 20) {
                channelUnlock(stream_id);
                return false;
            }

            QByteArray data = channelReadRaw(stream_id, 0, size);
            omvDebug() << "📷 Frame Data Size:" << data.size();
            if (data.size() != size) {
                channelUnlock(stream_id);
                return false;
            }

            QDataStream ds(data);
            ds.setByteOrder(QDataStream::LittleEndian);
            ds.setFloatingPointPrecision(QDataStream::SinglePrecision); // fps is a 32-bit float
            uint32_t width  = 0;
            uint32_t height = 0;
            uint32_t pixfmt = 0;
            uint32_t depth  = 0;
            uint32_t offset  = 0;
            ds >> width >> height >> pixfmt >> depth >> offset;

            // v5.0.0 firmware grows the header by a 32-bit fps float at bytes 20..23, bumping
            // the pixel-data offset to >= 24. Older headers stop at 20 (offset == 20, no fps).
            float fps = 0.0f;
            bool has_fps = false;
            if (offset >= 24) {
                ds >> fps;
                has_fps = true;
            }

            QByteArray raw_data = data.mid(offset);

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

            QSize oldStreamingRes = streamingRes;

            if (fmt_str == QStringLiteral("BINARY")) {
                streamingRes = bestFitAspect(stream_buffer_size_kb * 1024, 0, QSize(640, 480));
            } else if (fmt_str == QStringLiteral("GRAY")) {
                streamingRes = bestFitAspect(stream_buffer_size_kb * 1024, 1, QSize(640, 480));
            } else if (fmt_str == QStringLiteral("RGB565")) {
                streamingRes = bestFitAspect(stream_buffer_size_kb * 1024, 2, QSize(640, 480));
            } else if (fmt_str == QStringLiteral("ARGB8")) {
                streamingRes = bestFitAspect(stream_buffer_size_kb * 1024, 4, QSize(640, 480));
                // In the event that the raw buffer size is larger than what can actually be held
                // the openmv cam will return a jpeg instead of the raw data. When this happens we
                // need to reset the buffer size back to the default.
            } else if (fmt_str == QStringLiteral("JPEG") && rawStreaming) {
                streamingRes = bestFitAspect(stream_buffer_size_kb * 1024, 2, QSize(640, 480));
            } else if (fmt_str == QStringLiteral("PNG") && rawStreaming) {
                streamingRes = bestFitAspect(stream_buffer_size_kb * 1024, 2, QSize(640, 480));
            }

            if (streamingRes != oldStreamingRes) {
                omvDebug() << "🎨" << fmt_str << "format detected, adjusting streaming resolution to"
                           << streamingRes;
                QList<uint32_t> args;
                args << uint32_t(streamingRes.width()) << uint32_t(streamingRes.height());
                channelIoctl(stream_id,
                             static_cast<uint32_t>(OMVPChannelIOCTL::STREAM_RAW_CFG),
                             "II",
                             args);
            }

            // And then toss that jpeg...
            if ((fmt_str == QStringLiteral("JPEG") || fmt_str == QStringLiteral("PNG")) && rawStreaming) {
                channelUnlock(stream_id);
                return false;
            }

            outFrame.width   = int(width);
            outFrame.height  = int(height);
            outFrame.format  = pixfmt;
            outFrame.depth   = depth;
            outFrame.pixmap  = pm;
            outFrame.raw_size = raw_data.size();
            outFrame.fps     = fps;
            outFrame.has_fps = has_fps;

            channelUnlock(stream_id);
            return true;
        } catch (const OMVPTimeoutException &e) {
            throw;
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

bool OMVCamera::frameReady(bool checkEvent)
{
    if (checkEvent) {
        return caps_events && frameEvent;
    }

    if (caps_events) {
        if (lastFrameReady.isValid()) {
            if (lastFrameReady.elapsed() < 1000) {
                return frameEvent;
            }
            // fall through otherwise
        } else {
            lastFrameReady.start();
            return frameEvent;
        }
    }

    // Update camera state variables
    QVariantMap status = readStatus();
    lastFrameReady.restart();
    frameEvent = status.value(QStringLiteral("stream")).toBool();
    return frameEvent;
}

bool OMVCamera::scriptRunning(bool alwaysPoll)
{
    if ((!alwaysPoll) && caps_events) {
        if (lastScriptRunning.isValid()) {
            if (lastScriptRunning.elapsed() < 1000) {
                return scriptState;
            }
            // fall through otherwise
        } else {
            lastScriptRunning.start();
            return scriptState;
        }
    }

    // Update camera state variables
    QVariantMap status = readStatus();
    lastScriptRunning.restart();
    scriptState = status.value(QStringLiteral("stdin")).toBool();
    return scriptState;
}

QPair<bool, bool> OMVCamera::frameReadyAndScriptRunning()
{
    if (caps_events) {
        if (lastframeReadyAndScriptRunning.isValid()) {
            if (lastframeReadyAndScriptRunning.elapsed() < 1000) {
                return QPair<bool, bool>(frameEvent, scriptState);
            }
            // fall through otherwise
        } else {
            lastframeReadyAndScriptRunning.start();
            return QPair<bool, bool>(frameEvent, scriptState);
        }
    }

    // Update camera state variables
    QVariantMap status = readStatus();
    lastframeReadyAndScriptRunning.restart();
    frameEvent = status.value(QStringLiteral("stream")).toBool();
    scriptState = status.value(QStringLiteral("stdin")).toBool();
    return QPair<bool, bool>(frameEvent, scriptState);
}

QVariantMap OMVCamera::systemInfo()
{
    /*
        Get system information
    */
    return retryIfFailed([this]() -> QVariantMap {
        QByteArray proto_ver(3, Qt::Uninitialized);
        QByteArray boot_ver(3, Qt::Uninitialized);
        QByteArray fw_ver(3, Qt::Uninitialized);

        {
            QByteArray payload = sendCmdWaitResp(OMVPOpcode::PROTO_VERSION);
            if (payload.size() < 16) {
                throw OMVPException(
                    QStringLiteral("Invalid PROTO_VERSION payload size: %1").arg(payload.size()));
            }

            QDataStream ds(payload);
            ds.setByteOrder(QDataStream::LittleEndian);

            ds.readRawData(proto_ver.data(), 3);
            ds.readRawData(boot_ver.data(), 3);
            ds.readRawData(fw_ver.data(), 3);
            ds.skipRawData(7);
        }

        QByteArray payload = sendCmdWaitResp(OMVPOpcode::SYS_INFO);
        if (payload.size() < 76) {
            throw OMVPException(
                QStringLiteral("Invalid SYS_INFO payload size: %1").arg(payload.size()));
        }

        QDataStream ds(payload);
        ds.setByteOrder(QDataStream::LittleEndian);

        uint32_t cpu_id = 0;
        uint32_t device_id[3] = {};
        uint32_t sensor_chip_id[3] = {};
        uint32_t usb_id = 0;
        uint32_t id_reserved[2] = {};
        uint32_t hw_caps[2] = {};
        uint32_t memory[4] = {};
        uint32_t memory_reserved[3] = {};

        ds >> cpu_id;
        for (int i = 0; i < 3; ++i) ds >> device_id[i];
        ds >> usb_id;
        for (int i = 0; i < 3; ++i) ds >> sensor_chip_id[i];
        for (int i = 0; i < 2; ++i) ds >> id_reserved[i];
        for (int i = 0; i < 2; ++i) ds >> hw_caps[i];
        for (int i = 0; i < 4; ++i) ds >> memory[i];
        for (int i = 0; i < 3; ++i) ds >> memory_reserved[i];

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

        m.insert(QStringLiteral("usb_vid"), usb_id >> 16 & 0xFFFF);
        m.insert(QStringLiteral("usb_pid"), usb_id & 0xFFFF);

        m.insert(QStringLiteral("gpu_present"),       bool(capabilities & (1u << 0)));
        m.insert(QStringLiteral("npu_present"),       bool(capabilities & (1u << 1)));
        m.insert(QStringLiteral("isp_present"),       bool(capabilities & (1u << 2)));
        m.insert(QStringLiteral("venc_present"),      bool(capabilities & (1u << 3)));
        m.insert(QStringLiteral("jpeg_present"),      bool(capabilities & (1u << 4)));
        m.insert(QStringLiteral("dram_present"),      bool(capabilities & (1u << 5)));
        m.insert(QStringLiteral("crc_present"),       bool(capabilities & (1u << 6)));
        m.insert(QStringLiteral("pmu_present"),       bool(capabilities & (1u << 7)));
        m.insert(QStringLiteral("pmu_eventcnt"),      (capabilities >> 8) & 0xFFu);
        m.insert(QStringLiteral("wifi_present"),      bool(capabilities & (1u << 16)));
        m.insert(QStringLiteral("bt_present"),        bool(capabilities & (1u << 17)));
        m.insert(QStringLiteral("sd_present"),        bool(capabilities & (1u << 18)));
        m.insert(QStringLiteral("eth_present"),       bool(capabilities & (1u << 19)));
        m.insert(QStringLiteral("usb_highspeed"),     bool(capabilities & (1u << 20)));
        m.insert(QStringLiteral("multicore_present"), bool(capabilities & (1u << 21)));

        stream_buffer_size_kb = memory[3];
        m.insert(QStringLiteral("flash_size_kb"),         memory[0]);
        m.insert(QStringLiteral("ram_size_kb"),           memory[1]);
        m.insert(QStringLiteral("framebuffer_size_kb"),   memory[2]);
        m.insert(QStringLiteral("stream_buffer_size_kb"), memory[3]);

        fw_v << uint8_t(fw_ver[0]) << uint8_t(fw_ver[1]) << uint8_t(fw_ver[2]);
        m.insert(QStringLiteral("firmware_version"), fw_v);

        proto_v << uint8_t(proto_ver[0]) << uint8_t(proto_ver[1]) << uint8_t(proto_ver[2]);
        m.insert(QStringLiteral("protocol_version"), proto_v);

        boot_v << uint8_t(boot_ver[0]) << uint8_t(boot_ver[1]) << uint8_t(boot_ver[2]);
        m.insert(QStringLiteral("bootloader_version"), boot_v);

        streamingRes = bestFitAspect(stream_buffer_size_kb * 1024, 2, QSize(640, 480));
        omvDebug().noquote().nospace()
            << "📐 Calculated max streaming resolution: "
            << streamingRes.width() << "x" << streamingRes.height();

        sysinfo = m; // cache
        return m;
    });
}

QString OMVCamera::systemInfoString()
{
    QString info;
    QTextStream stream(&info);

    stream
        << "CPU ID: 0x"
        << QString::number(sysinfo.value(QStringLiteral("cpu_id")).toUInt(),
                           16).rightJustified(8, QChar('0')).toUpper() << " - ";

    // Device ID is now an array of 3 words
    QVariantList dev_id_list = sysinfo.value(QStringLiteral("device_id")).toList();
    QString dev_id_hex;
    for (const QVariant &v : std::as_const(dev_id_list)) {
        dev_id_hex += QString::number(v.toUInt(), 16).rightJustified(8, QChar('0')).toUpper();
    }
    stream << "Device ID: " << dev_id_hex << '\n';

    // Sensor Chip IDs are now an array of 3 words
    QVariantList chip_list = sysinfo.value(QStringLiteral("sensor_chip_id")).toList();
    for (int i = 0; i < chip_list.size(); ++i) {
        uint32_t chip_id = chip_list[i].toUInt();
        if (chip_id != 0) {
            if (i) stream << ", ";
            stream << QStringLiteral("CSI%1: 0x%2").arg(i)
                .arg(QString::number(chip_id, 16).rightJustified(4, QChar('0')).toUpper());
        }
    }
    stream << '\n';

    stream << "USB ID: " <<
        QString::number(sysinfo.value(QStringLiteral("usb_vid")).toUInt(),
                        16).rightJustified(4, QChar('0')).toUpper() << ":" <<
        QString::number(sysinfo.value(QStringLiteral("usb_pid")).toUInt(),
                        16).rightJustified(4, QChar('0')).toUpper() << '\n';

    // Memory info
    auto print_if_positive = [&](const char *label, const char *key) {
        uint32_t val = sysinfo.value(QString::fromLatin1(key)).toUInt();
        if (val > 0) {
            stream
                << label << ": " << val << "KB" << '\n';
        }
    };

    print_if_positive("Flash", "flash_size_kb");
    print_if_positive("RAM", "ram_size_kb");
    print_if_positive("Framebuffer", "framebuffer_size_kb");
    print_if_positive("Stream Buffer", "stream_buffer_size_kb");

    // Hardware capabilities
    stream << "Hardware capabilities:" << '\n';
    auto yn = [&](const char *key) {
        return sysinfo.value(QString::fromLatin1(key)).toBool() ? "Yes" : "No";
    };

    stream << "  GPU: " << yn("gpu_present");
    stream << "\t\t\tNPU: " << yn("npu_present") << '\n';
    stream << "  ISP: " << yn("isp_present");
    stream << "\t\t\tVideo Encoder: " << yn("venc_present") << '\n';
    stream << "  JPEG Encoder: " << yn("jpeg_present");
    stream << "\t\tDRAM: " << yn("dram_present") << '\n';
    stream << "  CRC Hardware: " << yn("crc_present");
    stream
        << "\t\tPMU: "
        << yn("pmu_present")
        << " (" << sysinfo.value(QStringLiteral("pmu_eventcnt")).toUInt()
        << " counters)" << '\n';

    stream << "  Multi-core: " << yn("multicore_present");
    stream << "\t\tWiFi: " << yn("wifi_present") << '\n';
    stream << "  Bluetooth: " << yn("bt_present");
    stream << "\t\tSD Card: " << yn("sd_present") << '\n';
    stream << "  Ethernet: " << yn("eth_present");
    stream << "\t\tUSB High-Speed: " << yn("usb_highspeed") << '\n';

    // Profiler info
    bool profile_available = channelsByName.contains(QStringLiteral("profile"));
    stream
        << "Profiler: "
        << (profile_available ? "Available" : "Not available") << '\n';

    // Version info
    auto print_ver = [&](const char *label, const char *key) {
        QVariantList v = sysinfo.value(QString::fromLatin1(key)).toList();
        if (v.size() == 3) {
            stream
                << label << " version: "
                << v[0].toUInt() << "."
                << v[1].toUInt() << "."
                << v[2].toUInt() << '\n';
        }
    };

    print_ver("Firmware", "firmware_version");
    print_ver("Protocol", "protocol_version");
    print_ver("Bootloader", "bootloader_version");

    stream
        << "Protocol capabilities:\n"
        << "  CRC=" << caps_crc
        << ", SEQ=" << caps_seq
        << ", ACK=" << caps_ack
        << ", EVENTS=" << caps_events
        << ", PAYLOAD=" << caps_max_payload;

    return info;
}

void OMVCamera::printSystemInfo()
{
    /*
        Print formatted system information
    */
    omvDebug() << "🖥️ === OpenMV System Information ===";
    // Mark every line so each row is identifiable as debug output. systemInfoString()
    // is also shown raw in the System Info dialog, so prefix here at the print site only
    // -- never inside systemInfoString() itself.
    const auto lines = systemInfoString().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        omvDebug().noquote().nospace() << "🖥️ " << line;
    }
    omvDebug() << "🖥️ =================================";
}

QSize OMVCamera::bestFitAspect(uint32_t maxBytes, int bpp, QSize ratio)
{
    if (!maxBytes || !ratio.width() || !ratio.height()) {
        return QSize(0, 0);
    }

    // Reduce aspect ratio (inline gcd).
    int a = ratio.width();
    int b = ratio.height();

    while (b) {
        int t = a % b;
        a = b;
        b = t;
    }

    int aw = ratio.width() / a;
    int ah = ratio.height() / a;
    int64_t maxPixels = bpp ? (maxBytes / bpp) : maxBytes;

    if (!maxPixels) {
        return QSize(0, 0);
    }

    // Start with the widest possible width.
    for (int64_t w = maxPixels & ~1; w >= 2; w -= 2) {
        // Height from aspect ratio (floor).
        int64_t h = ((w * ah) / aw) & ~1;

        if ((h >= 2) && ((bpp ? (w * h) : (((w + 31) / 32) * 4 * h)) <= maxPixels)) {
            return QSize(w, h);
        }
    }

    return QSize(0, 0);
}

} // namespace omv
