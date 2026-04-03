/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Protocol Transport Layer
 *
 * This module provides the low-level transport layer with the protocol state
 * machine for packet parsing and communication management.
 */

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QByteArrayView>
#include <QtCore/QVariant>
#include <QtCore/QElapsedTimer>
#include <QtCore/QDebug>
#include <QtCore/QRandomGenerator>
#include <QtCore/QThread>
#include <QtCore/QDataStream>
#include <functional>
#include <cstdint>

#include "omv_port.h"
#include "omv_buffer.h"

namespace omv {

class OMVTransport
{
public:
    /*
        Low-level transport layer with state machine
    */
    OMVTransport(OMVPort *serial,
                 bool crc,
                 bool seq,
                 qsizetype max_payload,
                 double timeout,
                 std::function<void(uint8_t, uint16_t)> event_callback,
                 double drop_rate = 0.0);

    /*
        Logging messages per packet send/recv.
    */
    static void setLoggingEnabled(bool enabled);
    static bool isLoggingEnabled();

    /*
        Reset sequence counter to 0
    */
    void reset_sequence();

    /*
        Update transport capabilities
    */
    void update_caps(bool crc, bool seq, bool ack, qsizetype max_payload);

    /*
        Send a packet to the camera
    */
    void send_packet(uint8_t opcode,
                     uint8_t channel,
                     uint8_t flags,
                     QByteArrayView data = QByteArrayView(),
                     int sequence = -1);

    /*
        Receive and parse a packet from the camera with NAK handling

        Returns:
            QVariant()            -> None (poll or no packet yet)
            QVariant(false)       -> False (BUSY NAK)
            QVariant(true)        -> True (ACK only)
            QVariant(QByteArray)  -> bytes payload
    */
    QVariant recv_packet(bool poll_events = false, bool short_timeout = false);

    typedef struct _stats {
        uint32_t sent;
        uint32_t received;
        uint32_t checksum;
        uint32_t sequence;
    } stats_t;

    // Statistics
    stats_t stats;

private:
    /*
        Calculate CRC with specified size (16 or 32)
    */
    uint32_t _crc(QByteArrayView data, int crc_size = 16) const;

    /*
        Check if CRC matches the calculated value or CRC is disabled
    */
    bool _check_crc(uint32_t crc, QByteArrayView buffer, int crc_size = 16) const;

    /*
        Check if sequence is valid or sequence checking is disabled
    */
    bool _check_seq(uint8_t sequence,
                    uint8_t expected_sequence,
                    uint8_t opcode,
                    uint8_t flags) const;

    /*
        Get human-readable name for packet flags
    */
    QString _format_flags(uint8_t flags) const;

    /*
        Log packet information for debugging
    */
    void log(int seq,
             int ch,
             int opcode,
             int flags,
             int length,
             const char *direction);

    struct Packet {
        uint16_t sync;
        uint8_t sequence;
        uint8_t channel;
        uint8_t flags;
        uint8_t opcode;
        uint16_t length;
        uint16_t header_crc;
        QByteArray payload;
    };

    /*
        Process the protocol state machine
    */
    bool _process(Packet &out_packet);

    /*
        Necessary to prevent serial stall situations
     */
    #ifdef Q_OS_WIN
    QElapsedTimer _keep_alive_timer;
    void _send_keep_alive();
    #endif

private:
    QPointer<OMVPort> serial;
    double timeout;
    qsizetype max_payload;

    // Protocol state
    uint8_t sequence;
    uint8_t state;
    bool crc_enabled;
    bool seq_enabled;
    bool ack_enabled;

    // Event callback
    std::function<void(uint8_t, uint16_t)> event_callback;

    // Packet simulation
    double drop_rate;

    // Packet buffers for send/recv
    OMVRingBuffer buf;
    QByteArray pbuf;

    // For payload state machine
    qsizetype plength;
};

} // namespace omv
