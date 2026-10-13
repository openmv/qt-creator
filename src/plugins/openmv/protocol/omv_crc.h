/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * CRC implementation for OpenMV Protocol
 *
 * This module provides CRC-16 and CRC-32 calculation using polynomials 0xF94F and 0xFA567D89.
 */

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QByteArrayView>
#include <cstdint>

namespace omv {

/*
    Calculate CRC-16 with polynomial 0xF94F using lookup table.

    Args:
        data: bytes-like object to calculate CRC for
        init_value: initial CRC value (default: 0xFFFF)

    Returns:
        16-bit CRC value as integer
*/
uint16_t crc16(QByteArrayView data, uint16_t init_value = 0xFFFF);

/*
    Calculate CRC-32 with polynomial 0xFA567D89 using lookup table.

    Args:
        data: bytes-like object to calculate CRC for
        init_value: initial CRC value (default: 0xFFFFFFFF)

    Returns:
        32-bit CRC value as integer
*/
uint32_t crc32(QByteArrayView data, uint32_t init_value = 0xFFFFFFFF);

static inline uint16_t crc16(const QByteArray &data, uint16_t init_value = 0xFFFF)
{
    return crc16(QByteArrayView(data), init_value);
}

static inline uint32_t crc32(const QByteArray &data, uint32_t init_value = 0xFFFFFFFF)
{
    return crc32(QByteArrayView(data), init_value);
}

} // namespace omv
