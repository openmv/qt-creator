/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Protocol Ring Buffer
 *
 * This module provides an efficient ring buffer implementation for
 * packet parsing with optimized memory operations using memoryview.
 */

#include "omv_buffer.h"
#include <cstring>

namespace omv {

/*
    Efficient ring buffer for packet parsing
*/
OMVRingBuffer::OMVRingBuffer(qsizetype size)
    : m_size(size),
    m_data(size, char(0)),
    m_start(0),
    m_end(0),
    m_count(0),
    m_temp(size, char(0))
{
}

qsizetype OMVRingBuffer::length() const
{
    return m_count;
}

/*
    Add data to buffer - optimized with memoryview
*/
bool OMVRingBuffer::extend(QByteArrayView data)
{
    const qsizetype data_len = data.size();
    if (data_len > (m_size - m_count)) {
        return false; // Buffer overflow
    }

    const qsizetype space_to_end = m_size - m_end;
    char *dst = m_data.data();

    if (data_len <= space_to_end) {
        memcpy(dst + m_end, data.data(), size_t(data_len));
        m_end = (m_end + data_len) % m_size;
    } else {
        memcpy(dst + m_end, data.data(), size_t(space_to_end));
        const qsizetype remaining = data_len - space_to_end;
        memcpy(dst, data.data() + space_to_end, size_t(remaining));
        m_end = remaining;
    }

    m_count += data_len;
    return true;
}

bool OMVRingBuffer::extend(const QByteArray &data)
{
    return extend(QByteArrayView(data));
}

/*
    Peek at data without consuming - returns memoryview when possible
*/
QByteArrayView OMVRingBuffer::peek(qsizetype size) const
{
    if (size > m_count) {
        return QByteArrayView();
    }

    const char *src = m_data.constData();

    if (m_start + size <= m_size) {
        return QByteArrayView(src + m_start, size);
    }

    const qsizetype first_part = m_size - m_start;
    char *out = m_temp.data();

    memcpy(out, src + m_start, size_t(first_part));
    memcpy(out + first_part, src, size_t(size - first_part));

    return QByteArrayView(out, size);
}

/*
    Peek at 16-bit value at start of buffer
*/
bool OMVRingBuffer::peek16(quint16 &out) const
{
    if (m_count < 2) {
        return false;
    }

    const uint8_t *src = reinterpret_cast<const uint8_t *>(m_data.constData());

    if (m_start + 2 <= m_size) {
        out = quint16(src[m_start] | (src[m_start + 1] << 8));
    } else {
        const uint8_t b0 = src[m_start];
        const uint8_t b1 = src[(m_start + 1) % m_size];
        out = quint16(b0 | (b1 << 8));
    }

    return true;
}

/*
    Consume count bytes from buffer
*/
void OMVRingBuffer::consume(qsizetype count)
{
    if (count > m_count) {
        count = m_count;
    }

    m_start = (m_start + count) % m_size;
    m_count -= count;
}

/*
    Read and consume data
*/
QByteArrayView OMVRingBuffer::read(qsizetype size)
{
    QByteArrayView view = peek(size);
    if (!view.isEmpty() || size == 0) {
        consume(size);
    }
    return view;
}

} // namespace omv
