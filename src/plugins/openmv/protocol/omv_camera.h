/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Protocol Camera Interface
 *
 * This module provides the high-level OMVCamera class that handles all
 * camera operations and channel communications using the OpenMV Protocol.
 */

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QVariantMap>
#include <QtCore/QString>
#include <QtGui/QPixmap>

#include "omv_exceptions.h"
#include "omv_transport.h"

namespace omv {

struct OMVFrame {
    int      width  = 0;
    int      height = 0;
    uint32_t format = 0;
    uint32_t depth  = 0;
    QPixmap  pixmap;
    int      raw_size = 0;
};

class OMVCamera
{
public:
    /*
        OpenMV Camera Protocol Implementation
    */
    OMVCamera(OMVPort *serial_,
              bool crc          = true,
              bool seq          = true,
              bool ack          = true,
              bool events       = true,
              double timeout    = 1.0,
              int max_retry     = 1,
              int max_payload   = 4096,
              double drop_rate  = 0.0);

    ~OMVCamera();

    // Context-like usage (manual in C++)
    void connect();
    void disconnect();

    bool isConnected() const;

    void pollEvents();

    // Host transport stats (from OMVTransport)
    const OMVTransport::stats_t &hostStats() const;

    // Device/firmware stats & info
    QVariantMap deviceStats();   // PROTO_STATS
    QVariantMap systemInfo();    // SYS_INFO

    void printSystemInfo();

    // Basic device control
    void reset();
    void boot();
    void updateCapabilities();
    void stop();
    void exec(const QString &script);
    void streaming(bool enable, bool raw = false, const QSize &res = QSize());

    // Channels and polling
    QVariantMap readStatus();

    void profilerReset();
    void profilerMode(bool exclusive);
    void profilerEventType(uint32_t counter_num, uint32_t event_id);
    QVariantList readProfile();

    QString readStdout();
    bool readFrame(OMVFrame &outFrame);

    qsizetype channelSize(const QString &channel);
    QByteArray channelRead(const QString &channel, qsizetype size = -1);
    bool channelWrite(const QString &channel, const QByteArray &data);
    bool hasChannel(const QString &channel) const;

    // Access to cached system info map (Python-like keys)
    const QVariantMap &cachedSystemInfo() const { return sysinfo; }

    bool frameReady();
    bool scriptRunning(bool alwaysPoll = false);
    QPair<bool, bool> frameReadyAndScriptRunning();

    bool streamingEnabledState() const { return streamingEnabled; }
    bool rawStreamingState() const { return rawStreaming; }
    QSize streamingResolution() const { return streamingRes; }

private:
    struct ChannelInfo {
        QString name;
        uint8_t flags = 0;
    };

    // Configuration / capabilities
    bool     caps_crc;
    bool     caps_seq;
    bool     caps_ack;
    bool     caps_events;
    uint16_t caps_max_payload;

    // Connection / protocol state
    QPointer<OMVPort> serial;
    double       timeoutSec;
    int          maxRetry;
    double       dropRate;

    // Protocol components
    QMap<QString, uint8_t> channelsByName;
    QMap<uint8_t, ChannelInfo> channelsById;
    int          pendingChannelEvents;
    QVariantMap  sysinfo;
    OMVTransport *transport;
    bool         resyncPending;
    bool         frameEvent;
    bool         scriptState;
    bool         streamingEnabled;
    bool         rawStreaming;
    QSize        streamingRes;
    int stream_buffer_size_kb;
    QVariantList fw_v;
    QVariantList proto_v;
    QVariantList boot_v;
    QElapsedTimer lastFrameReady;
    QElapsedTimer lastScriptRunning;
    QElapsedTimer lastframeReadyAndScriptRunning;

private:
    // Helper: retry-on-resync (decorator equivalent)
    template <typename F>
    auto retryIfFailed(F f) -> decltype(f())
    {
        try {
            return f();
        } catch (const OMVPResyncException &) {
            return f();
        }
    }

    template <typename F>
    void retryIfFailedVoid(F f)
    {
        try {
            f();
        } catch (const OMVPResyncException &) {
            f();
        }
    }

    void handleEvent(uint8_t channel_id, uint16_t event);
    void resync();

    QByteArray sendCmdWaitResp(uint8_t opcode,
                               uint8_t channel = 0,
                               const QByteArray &data = QByteArray());

    bool channelLock(uint8_t channel_id);
    bool channelUnlock(uint8_t channel_id);
    uint32_t channelSizeRaw(uint8_t channel_id);
    QVector<uint32_t> channelShape(uint8_t channel_id);
    QByteArray channelReadRaw(uint8_t channel_id, uint32_t offset, uint32_t length);
    void channelWriteRaw(uint8_t channel_id, const QByteArray &data, uint32_t offset = 0);
    QByteArray channelIoctl(uint8_t channel_id, uint32_t cmd,
                            const char *fmt = nullptr,
                            const QList<uint32_t> &args = QList<uint32_t>());

    QMap<uint8_t, ChannelInfo> channelList();
    void updateChannels();

    // Helpers: get channel id/name
    uint8_t getChannelId(const QString &name);
    QString getChannelName(uint8_t channel_id) const;

    QSize bestFitAspect(uint32_t maxBytes, QSize ratio);
};

} // namespace omv
