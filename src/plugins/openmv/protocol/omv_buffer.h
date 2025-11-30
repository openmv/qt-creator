/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Protocol Ring Buffer
 *
 * This module provides an efficient ring buffer implementation for
 * packet parsing with optimized memory operations using memoryview.
 */

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QByteArrayView>
#include <QtCore/QtGlobal>

namespace omv {

class OMVRingBuffer
{
public:
    /*
        Efficient ring buffer for packet parsing
    */
    explicit OMVRingBuffer(qsizetype size = 4096);

    qsizetype length() const;

    /*
        Add data to buffer - optimized with memoryview
    */
    bool extend(QByteArrayView data);
    bool extend(const QByteArray &data);

    /*
        Peek at data without consuming - returns memoryview when possible
    */
    QByteArrayView peek(qsizetype size) const;

    /*
        Peek at 16-bit value at start of buffer
    */
    bool peek16(quint16 &out) const;

    /*
        Consume count bytes from buffer
    */
    void consume(qsizetype count);

    /*
        Read and consume data
    */
    QByteArrayView read(qsizetype size);

private:
    qsizetype m_size;
    QByteArray m_data;
    qsizetype m_start; // Read position
    qsizetype m_end;   // Write position
    qsizetype m_count; // Number of bytes in buffer

    // Used only for wrap-around peek/read
    mutable QByteArray m_temp;
};

} // namespace omv
