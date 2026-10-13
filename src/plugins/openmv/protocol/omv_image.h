/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Image Utilities
 *
 * This module provides image format conversion utilities for OpenMV camera data.
 * Handles conversion from various pixel formats to RGB888 for display purposes.
 */

#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtGui/QPixmap>
#include <cstdint>

namespace omv {

// Pixel format constants
static constexpr uint32_t PIXFORMAT_BINARY    = 0x08010000;  // 4 bytes per 32 pixels
static constexpr uint32_t PIXFORMAT_GRAYSCALE = 0x08020001;  // 1 byte per pixel
static constexpr uint32_t PIXFORMAT_RGB565    = 0x0C030002;  // 2 bytes per pixel
static constexpr uint32_t PIXFORMAT_ARGB8     = 0x0C080004;  // 4 bytes per pixel
static constexpr uint32_t PIXFORMAT_JPEG      = 0x06060000;  // Variable size JPEG
static constexpr uint32_t PIXFORMAT_PNG       = 0x06070000;  // Variable size PNG

/*
    Convert various pixel formats to RGB888.

    Args:
        raw_data (bytes): Raw image data
        width (int): Image width
        height (int): Image height
        pixformat (int): Pixel format identifier

    Returns:
        QPixmap: Pixmap for display. Null pixmap on error.

    Note:
        If out_format_string is provided, it receives "GRAY", "RGB565", "JPEG",
        or a hex string for unknown formats.
*/
QPixmap convert_to_rgb888(const QByteArray &raw_data,
                          int width,
                          int height,
                          uint32_t pixformat,
                          QString *out_format_string = nullptr);

/*
    Get a human-readable format string from pixel format code
*/
QString get_format_string(uint32_t pixformat);

} // namespace omv
