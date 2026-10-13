/* Copyright (C) 2026 OpenMV, LLC.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Any redistribution, use, or modification in source or binary form
 *    is done solely for personal benefit and not for any commercial
 *    purpose or for monetary gain. For commercial licensing options,
 *    please contact openmv@openmv.io
 *
 * THIS SOFTWARE IS PROVIDED BY THE LICENSOR AND COPYRIGHT OWNER "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE LICENSOR OR COPYRIGHT
 * OWNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "openmvboardinfoview.h"
#include "openmvviewstyle.h"
#include "../openmvtr.h"

namespace OpenMV {
namespace Internal {

// Row labels, in OpenMV Studio's Board Info order.
static const char *ROW_LABELS[] = {
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Board"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Sensor"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Port"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Firmware"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Protocol"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Bootloader"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "CPU ID"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Device ID"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "USB"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Stream Buffer"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Frame Buffer"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Flash"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "RAM"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "GPU"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "NPU"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "ISP"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Video Encoder"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "JPEG"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "DRAM"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "CRC"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "PMU"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Profiler"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "WiFi"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Bluetooth"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "SD Card"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Ethernet"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "USB High Speed"),
    QT_TRANSLATE_NOOP("QtC::OpenMV", "Multicore"),
};

enum BoardInfoRow {
    ROW_BOARD, ROW_SENSOR, ROW_PORT, ROW_FIRMWARE, ROW_PROTOCOL, ROW_BOOTLOADER,
    ROW_CPU_ID, ROW_DEVICE_ID, ROW_USB, ROW_STREAM_BUFFER, ROW_FRAME_BUFFER,
    ROW_FLASH, ROW_RAM, ROW_GPU, ROW_NPU, ROW_ISP, ROW_VENC,
    ROW_JPEG, ROW_DRAM, ROW_CRC, ROW_PMU, ROW_PROFILER, ROW_WIFI, ROW_BT, ROW_SD, ROW_ETH,
    ROW_USB_HS, ROW_MULTICORE, ROW_COUNT
};

static QString versionString(const QVariant &value)
{
    QVariantList list = value.toList();

    if(list.size() < 3)
    {
        return QStringLiteral("--");
    }

    return QStringLiteral("%L1.%L2.%L3").arg(list.at(0).toInt()).arg(list.at(1).toInt()).arg(list.at(2).toInt());
}

static QString yesNo(const QVariantMap &info, const char *key)
{
    return info.value(QLatin1String(key)).toBool() ? Tr::tr("Yes") : Tr::tr("No");
}

static QString textOrEmpty(const QString &text)
{
    return text.isEmpty() ? QStringLiteral("--") : text;
}

OpenMVBoardInfoView::OpenMVBoardInfoView(QWidget *parent) : QStackedWidget(parent)
{
    viewApplyBackground(this);

    // Page 0: a status message, centered and styled like the frame buffer's
    // "No Image" text.
    m_message = new QLabel;
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    addWidget(m_message);

    // Page 1: the scrollable info table.
    QScrollArea *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->viewport()->setAutoFillBackground(false);

    QWidget *container = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 4, 0, 4); // rows inset their own text; hairlines run full-bleed
    layout->setSpacing(0);

    for(size_t i = 0; i < (sizeof(ROW_LABELS) / sizeof(ROW_LABELS[0])); i++)
    {
        QLabel *value = viewValueLabel();
        m_values.append(value);

        QWidget *row = viewRow(Tr::tr(ROW_LABELS[i]), value);
        m_rows.append(row);
        layout->addWidget(row);
    }

    layout->addStretch(1);

    scrollArea->setWidget(container);
    addWidget(scrollArea);

    reset();
}

void OpenMVBoardInfoView::showMessage(const QString &message)
{
    m_message->setText(viewMessageHtml(message));
    setCurrentIndex(0);
}

void OpenMVBoardInfoView::reset()
{
    showMessage(Tr::tr("Connect a camera to view board information"));
}

void OpenMVBoardInfoView::systemInfo(const QVariantMap &info, const QString &board,
                                     const QString &boardId, const QString &sensor, const QString &port,
                                     bool profilerAvailable)
{
    if(info.isEmpty())
    {
        showMessage(Tr::tr("Board information is not available for this camera"));
        return;
    }

    // Helpers for the capability and size rows: visible only when the key
    // exists (the V1 protocol only knows a couple of them).
    auto boolRow = [this, &info] (int row, const char *key) {
        setRow(row, info.contains(QLatin1String(key)), yesNo(info, key));
    };

    auto sizeRow = [this, &info] (int row, const char *key) {
        uint kb = info.value(QLatin1String(key)).toUInt();
        setRow(row, info.contains(QLatin1String(key)),
               kb ? Tr::tr("%L1 KB").arg(kb) : QStringLiteral("--"));
    };

    setRow(ROW_BOARD, true, textOrEmpty(board));
    setRow(ROW_SENSOR, true, textOrEmpty(sensor));
    setRow(ROW_PORT, true, textOrEmpty(port));
    setRow(ROW_FIRMWARE, info.contains(QStringLiteral("firmware_version")),
        versionString(info.value(QStringLiteral("firmware_version"))));
    setRow(ROW_PROTOCOL, info.contains(QStringLiteral("protocol_version")),
        versionString(info.value(QStringLiteral("protocol_version"))));
    setRow(ROW_BOOTLOADER, info.contains(QStringLiteral("bootloader_version")),
        versionString(info.value(QStringLiteral("bootloader_version"))));
    setRow(ROW_CPU_ID, info.contains(QStringLiteral("cpu_id")), QStringLiteral("0x%1").arg(
        QString::number(info.value(QStringLiteral("cpu_id")).toUInt(), 16).toUpper()));
    setRow(ROW_DEVICE_ID, !boardId.isEmpty(), boardId);
    setRow(ROW_USB, info.contains(QStringLiteral("usb_vid")), QStringLiteral("%1:%2").arg(
        info.value(QStringLiteral("usb_vid")).toUInt(), 4, 16, QLatin1Char('0')).arg(
        info.value(QStringLiteral("usb_pid")).toUInt(), 4, 16, QLatin1Char('0')).toUpper());
    sizeRow(ROW_STREAM_BUFFER, "stream_buffer_size_kb");
    sizeRow(ROW_FRAME_BUFFER, "framebuffer_size_kb");
    sizeRow(ROW_FLASH, "flash_size_kb");
    sizeRow(ROW_RAM, "ram_size_kb");
    boolRow(ROW_GPU, "gpu_present");
    boolRow(ROW_NPU, "npu_present");
    boolRow(ROW_ISP, "isp_present");
    boolRow(ROW_VENC, "venc_present");
    boolRow(ROW_JPEG, "jpeg_present");
    boolRow(ROW_DRAM, "dram_present");
    boolRow(ROW_CRC, "crc_present");

    if(info.value(QStringLiteral("pmu_present")).toBool())
    {
        // The V1 protocol knows the PMU exists but not its counter count.
        setRow(ROW_PMU, true, info.contains(QStringLiteral("pmu_eventcnt"))
            ? Tr::tr("Yes (%L1 counters)").arg(info.value(QStringLiteral("pmu_eventcnt")).toUInt())
            : Tr::tr("Yes"));
    }
    else
    {
        setRow(ROW_PMU, info.contains(QStringLiteral("pmu_present")), Tr::tr("No"));
    }

    setRow(ROW_PROFILER, true, profilerAvailable ? Tr::tr("Available") : Tr::tr("Not available"));
    boolRow(ROW_WIFI, "wifi_present");
    boolRow(ROW_BT, "bt_present");
    boolRow(ROW_SD, "sd_present");
    boolRow(ROW_ETH, "eth_present");
    boolRow(ROW_USB_HS, "usb_highspeed");
    boolRow(ROW_MULTICORE, "multicore_present");

    // No hairline under the table's last visible row (which row that is
    // depends on what the protocol reported). isHidden() reflects setRow()'s
    // explicit setVisible() regardless of whether this page is showing yet
    // (this runs before the first setCurrentIndex(1)).
    int lastVisible = -1;

    for(int i = 0; i < m_rows.size(); i++)
    {
        if(!m_rows.at(i)->isHidden())
        {
            lastVisible = i;
        }
    }

    for(int i = 0; i < m_rows.size(); i++)
    {
        viewRowSetLineVisible(m_rows.at(i), i != lastVisible);
    }

    setCurrentIndex(1);
}

void OpenMVBoardInfoView::setRow(int row, bool present, const QString &text)
{
    m_rows.at(row)->setVisible(present);

    if(present)
    {
        m_values.at(row)->setText(text);
    }
}

} // namespace Internal
} // namespace OpenMV
