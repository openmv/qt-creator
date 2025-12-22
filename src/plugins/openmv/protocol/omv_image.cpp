/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Image Utilities
 *
 * This module provides image format conversion utilities for OpenMV camera data.
 * Handles conversion from various pixel formats to RGB888 for display purposes.
 */

#include "omv_debug.h"
#include "omv_image.h"

#include <QtGui/QImage>

namespace omv {

static QPixmap _convert_binary(const QByteArray &raw_data,
                               int width,
                               int height,
                               QString *fmt_str)
{
    /*
        Convert grayscale to RGB888
    */
    if (fmt_str) *fmt_str = QStringLiteral("BINARY");

    const qsizetype expected = (((qsizetype(width) + 31) / 32) * 4) * qsizetype(height);
    if (raw_data.size() != expected) {
        omvDebug().noquote()
        << "Binary data size mismatch: expected"
        << expected << ", got" << raw_data.size();
        return QPixmap();
    }

    // Wrap raw bytes as Binary without copying.
    QImage binary(reinterpret_cast<const uchar *>(raw_data.constData()),
                  width,
                  height,
                  ((width + 31) / 32) * 4, // bytesPerLine for Binary
                  QImage::Format_MonoLSB);

    if (binary.isNull()) {
        omvDebug() << "Failed to wrap BINARY image data";
        return QPixmap();
    }

    return QPixmap::fromImage(binary);
}

static QPixmap _convert_grayscale(const QByteArray &raw_data,
                                  int width,
                                  int height,
                                  QString *fmt_str)
{
    /*
        Convert grayscale to RGB888
    */
    if (fmt_str) *fmt_str = QStringLiteral("GRAY");

    const qsizetype expected = qsizetype(width) * qsizetype(height);
    if (raw_data.size() != expected) {
        omvDebug().noquote()
        << "Grayscale data size mismatch: expected"
        << expected << ", got" << raw_data.size();
        return QPixmap();
    }

    // Wrap raw bytes as Grayscale8 without copying.
    QImage gray(reinterpret_cast<const uchar *>(raw_data.constData()),
                width,
                height,
                width, // bytesPerLine for Grayscale8
                QImage::Format_Grayscale8);

    if (gray.isNull()) {
        omvDebug() << "Failed to wrap GRAY image data";
        return QPixmap();
    }

    return QPixmap::fromImage(gray);
}

static QPixmap _convert_rgb565(const QByteArray &raw_data,
                               int width,
                               int height,
                               QString *fmt_str)
{
    /*
        Convert RGB565 to RGB888
    */
    if (fmt_str) *fmt_str = QStringLiteral("RGB565");

    const qsizetype expected = qsizetype(width) * qsizetype(height) * 2;
    if (raw_data.size() != expected) {
        omvDebug().noquote()
        << "RGB565 data size mismatch: expected"
        << expected << ", got" << raw_data.size();
        return QPixmap();
    }

    // Wrap raw bytes as RGB16 (Qt treats this as RGB565) without copying.
    QImage rgb565(reinterpret_cast<const uchar *>(raw_data.constData()),
                  width,
                  height,
                  width * 2, // bytesPerLine for RGB565
                  QImage::Format_RGB16);

    if (rgb565.isNull()) {
        omvDebug() << "Failed to wrap RGB565 image data";
        return QPixmap();
    }

    return QPixmap::fromImage(rgb565);
}

static QPixmap _convert_argb8(const QByteArray &raw_data,
                              int width,
                              int height,
                              QString *fmt_str)
{
    /*
        Convert ARGB8 to RGB888
    */
    if (fmt_str) *fmt_str = QStringLiteral("ARGB8");

    const qsizetype expected = qsizetype(width) * qsizetype(height) * 4;
    if (raw_data.size() != expected) {
        omvDebug().noquote()
        << "ARGB8 data size mismatch: expected"
        << expected << ", got" << raw_data.size();
        return QPixmap();
    }

    // Wrap raw bytes as ARGB8 (Qt treats this as ARGB32) without copying.
    QImage argb8(reinterpret_cast<const uchar *>(raw_data.constData()),
                  width,
                  height,
                  width * 4, // bytesPerLine for ARGB8
                  QImage::Format_ARGB32);

    if (argb8.isNull()) {
        omvDebug() << "Failed to wrap ARGB8 image data";
        return QPixmap();
    }

    return QPixmap::fromImage(argb8);
}

static QPixmap _convert_jpeg(const QByteArray &raw_data,
                             int width,
                             int height,
                             QString *fmt_str)
{
    /*
        Convert JPEG to RGB888
    */
    if (fmt_str) *fmt_str = QStringLiteral("JPEG");

    QImage img;
    if (!img.loadFromData(raw_data, "JPG") && !img.loadFromData(raw_data, "JPEG")) {
        omvDebug() << "JPEG decode error: QImage::loadFromData failed";
        return QPixmap();
    }

    if (width > 0 && height > 0) {
        if (img.width() != width || img.height() != height) {
            omvDebug().noquote()
            << "JPEG decode size mismatch: expected"
            << (width * height * 3)
            << "pixels worth of RGB,"
            << "got image"
            << img.width() << "x" << img.height();
            return QPixmap();
        }
    }

    return QPixmap::fromImage(img);
}

static QPixmap _convert_png(const QByteArray &raw_data,
                            int width,
                            int height,
                            QString *fmt_str)
{
    /*
        Convert PNG to RGB888
    */
    if (fmt_str) *fmt_str = QStringLiteral("PNG");

    QImage img;
    if (!img.loadFromData(raw_data, "PNG")) {
        omvDebug() << "PNG decode error: QImage::loadFromData failed";
        return QPixmap();
    }

    if (width > 0 && height > 0) {
        if (img.width() != width || img.height() != height) {
            omvDebug().noquote()
            << "PNG decode size mismatch: expected"
            << (width * height * 3)
            << "pixels worth of RGB,"
            << "got image"
            << img.width() << "x" << img.height();
            return QPixmap();
        }
    }

    return QPixmap::fromImage(img);
}

QPixmap convert_to_rgb888(const QByteArray &raw_data,
                          int width,
                          int height,
                          uint32_t pixformat,
                          QString *out_format_string)
{
    /*
        Convert various pixel formats to RGB888.
    */
    QString fmt;
    QPixmap pm;

    if (pixformat == PIXFORMAT_BINARY) {
        pm = _convert_binary(raw_data, width, height, &fmt);
    } else if (pixformat == PIXFORMAT_GRAYSCALE) {
        pm = _convert_grayscale(raw_data, width, height, &fmt);
    } else if (pixformat == PIXFORMAT_RGB565) {
        pm = _convert_rgb565(raw_data, width, height, &fmt);
    } else if (pixformat == PIXFORMAT_ARGB8) {
        pm = _convert_argb8(raw_data, width, height, &fmt);
    } else if (pixformat == PIXFORMAT_JPEG) {
        pm = _convert_jpeg(raw_data, width, height, &fmt);
    } else if (pixformat == PIXFORMAT_PNG) {
        pm = _convert_png(raw_data, width, height, &fmt);
    } else {
        // Unknown format - return raw data and let caller handle it
        fmt = QStringLiteral("%1").arg(pixformat, 8, 16, QChar('0')).toUpper();
        omvDebug().noquote().nospace() << "Unknown pixel format: 0x" << fmt;
        pm = QPixmap(); // null pixmap
    }

    if (out_format_string) {
        *out_format_string = fmt;
    }

    return pm;
}

QString get_format_string(uint32_t pixformat)
{
    /*
        Get a human-readable format string from pixel format code
    */
    if (pixformat == PIXFORMAT_BINARY)    return QStringLiteral("BINARY");
    if (pixformat == PIXFORMAT_GRAYSCALE) return QStringLiteral("GRAY");
    if (pixformat == PIXFORMAT_RGB565)    return QStringLiteral("RGB565");
    if (pixformat == PIXFORMAT_ARGB8)     return QStringLiteral("ARGB8");
    if (pixformat == PIXFORMAT_JPEG)      return QStringLiteral("JPEG");
    if (pixformat == PIXFORMAT_PNG)       return QStringLiteral("PNG");

    return QStringLiteral("0x%1").arg(pixformat, 8, 16, QChar('0')).toUpper();
}

} // namespace omv
