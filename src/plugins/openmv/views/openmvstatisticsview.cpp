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

#include "openmvstatisticsview.h"
#include "openmvviewstyle.h"
#include "../openmvtr.h"

namespace OpenMV {
namespace Internal {

// Device counters: map keys and display labels, in OpenMV Studio's order
// (which is also the PROTO_STATS wire order).
static const struct { const char *key; const char *label; } DEVICE_ROWS[] = {
    { "sent",                QT_TRANSLATE_NOOP("QtC::OpenMV", "Sent") },
    { "received",            QT_TRANSLATE_NOOP("QtC::OpenMV", "Received") },
    { "checksum",            QT_TRANSLATE_NOOP("QtC::OpenMV", "Checksum Errors") },
    { "sequence",            QT_TRANSLATE_NOOP("QtC::OpenMV", "Sequence Errors") },
    { "retransmit",          QT_TRANSLATE_NOOP("QtC::OpenMV", "Retransmits") },
    { "transport",           QT_TRANSLATE_NOOP("QtC::OpenMV", "Transport Errors") },
    { "sent_events",         QT_TRANSLATE_NOOP("QtC::OpenMV", "Events Sent") },
    { "max_ack_queue_depth", QT_TRANSLATE_NOOP("QtC::OpenMV", "Max ACK Queue") },
    // V1-protocol counter; V2 doesn't report it, so its row hides there.
    { "sent_images",         QT_TRANSLATE_NOOP("QtC::OpenMV", "Images Sent") },
};

// Host (IDE-side transport) counters.
static const struct { const char *key; const char *label; } HOST_ROWS[] = {
    { "sent",     QT_TRANSLATE_NOOP("QtC::OpenMV", "Sent") },
    { "received", QT_TRANSLATE_NOOP("QtC::OpenMV", "Received") },
    { "checksum", QT_TRANSLATE_NOOP("QtC::OpenMV", "Checksum Errors") },
    { "sequence", QT_TRANSLATE_NOOP("QtC::OpenMV", "Sequence Errors") },
};

OpenMVStatisticsView::OpenMVStatisticsView(QWidget *parent) : QStackedWidget(parent)
{
    viewApplyBackground(this);

    // Page 0: a status message, centered and styled like the frame buffer's
    // "No Image" text.
    m_message = new QLabel;
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    addWidget(m_message);

    // Page 1: the scrollable stats list. Section order follows OpenMV
    // Studio's Statistics tab (device counters, then channels), with the
    // IDE's own transport counters appended.
    QScrollArea *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->viewport()->setAutoFillBackground(false);

    QWidget *container = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 4, 0, 4); // rows inset their own text; hairlines run full-bleed
    layout->setSpacing(0);

    layout->addWidget(viewSectionLabel(Tr::tr("Device")));

    for(size_t i = 0; i < (sizeof(DEVICE_ROWS) / sizeof(DEVICE_ROWS[0])); i++)
    {
        QLabel *value = viewValueLabel();
        m_deviceValues.append(value);

        QWidget *row = viewRow(Tr::tr(DEVICE_ROWS[i].label), value);
        m_deviceRows.append(row);
        layout->addWidget(row);
    }

    m_channelsHeader = viewSectionLabel(Tr::tr("Channels"));
    layout->addWidget(m_channelsHeader);

    m_channelsLayout = new QVBoxLayout;
    m_channelsLayout->setContentsMargins(0, 0, 0, 0);
    m_channelsLayout->setSpacing(0);
    layout->addLayout(m_channelsLayout);

    layout->addWidget(viewSectionLabel(Tr::tr("Host")));

    for(size_t i = 0; i < (sizeof(HOST_ROWS) / sizeof(HOST_ROWS[0])); i++)
    {
        QLabel *value = viewValueLabel();
        m_hostValues.append(value);

        QWidget *row = viewRow(Tr::tr(HOST_ROWS[i].label), value);
        m_hostRows.append(row);
        layout->addWidget(row);
    }

    layout->addStretch(1);

    scrollArea->setWidget(container);
    addWidget(scrollArea);

    reset();
}

void OpenMVStatisticsView::showMessage(const QString &message)
{
    m_message->setText(viewMessageHtml(message));
    setCurrentIndex(0);
}

void OpenMVStatisticsView::reset()
{
    m_lastEvents.clear();
    m_lastUpdate.invalidate();
    showMessage(Tr::tr("Connect a camera to view statistics"));
}

void OpenMVStatisticsView::rebuildChannelRows(const QVariantList &channels)
{
    while(QLayoutItem *item = m_channelsLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }

    m_channelValues.clear();
    m_channelRates.clear();

    for(int i = 0; i < channels.size(); i++)
    {
        QVariantMap channel = channels.at(i).toMap();

        QLabel *count = viewValueLabel();
        m_channelValues.append(count);

        // Muted secondary rate column with a stable width so counts don't
        // shift as rates change.
        QLabel *rate = viewNameLabel(QString());
        rate->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rate->setMinimumWidth(rate->fontMetrics().horizontalAdvance(QStringLiteral("00000/s")));
        m_channelRates.append(rate);

        m_channelsLayout->addWidget(viewRow(Tr::tr("%1 events").arg(
            channel.value(QStringLiteral("name")).toString()),
            QList<QWidget *>() << count << rate));
    }
}

void OpenMVStatisticsView::protocolStats(const QVariantMap &host, const QVariantMap &device,
                                         const QVariantList &channels)
{
    if(host.isEmpty() && device.isEmpty())
    {
        showMessage(Tr::tr("Statistics are not available for this camera"));
        return;
    }

    // Rows show only for keys the protocol reports (the V1 protocol has a
    // different, smaller set than V2); an empty map (transient device-query
    // failure) leaves the section's last values alone.
    for(size_t i = 0; i < (sizeof(DEVICE_ROWS) / sizeof(DEVICE_ROWS[0])); i++)
    {
        if(!device.isEmpty())
        {
            bool present = device.contains(QLatin1String(DEVICE_ROWS[i].key));
            m_deviceRows.at(int(i))->setVisible(present);

            if(present)
            {
                m_deviceValues.at(int(i))->setText(QStringLiteral("%L1").arg(
                    device.value(QLatin1String(DEVICE_ROWS[i].key)).toUInt()));
            }
        }
    }

    for(size_t i = 0; i < (sizeof(HOST_ROWS) / sizeof(HOST_ROWS[0])); i++)
    {
        if(!host.isEmpty())
        {
            bool present = host.contains(QLatin1String(HOST_ROWS[i].key));
            m_hostRows.at(int(i))->setVisible(present);

            if(present)
            {
                m_hostValues.at(int(i))->setText(QStringLiteral("%L1").arg(
                    host.value(QLatin1String(HOST_ROWS[i].key)).toUInt()));
            }
        }
    }

    // Rebuild the channel rows when the channel set changes; otherwise patch
    // counts in place and derive event rates from the previous update.
    QStringList schemaParts;

    for(int i = 0; i < channels.size(); i++)
    {
        QVariantMap channel = channels.at(i).toMap();
        schemaParts.append(QStringLiteral("%1:%2").arg(
            channel.value(QStringLiteral("id")).toInt()).arg(
            channel.value(QStringLiteral("name")).toString()));
    }

    QString schema = schemaParts.join(QLatin1Char('|'));

    if(schema != m_channelsSchema)
    {
        m_channelsSchema = schema;
        rebuildChannelRows(channels);
        m_lastEvents.clear();
        m_lastUpdate.invalidate();
    }

    m_channelsHeader->setVisible(!channels.isEmpty());

    qreal dt = (m_lastUpdate.isValid() ? m_lastUpdate.elapsed() : 0) / 1000.0;

    for(int i = 0; i < channels.size(); i++)
    {
        QVariantMap channel = channels.at(i).toMap();
        int id = channel.value(QStringLiteral("id")).toInt();
        quint32 events = channel.value(QStringLiteral("events")).toUInt();

        m_channelValues.at(i)->setText(QStringLiteral("%L1").arg(events));

        if((dt > 0) && m_lastEvents.contains(id))
        {
            m_channelRates.at(i)->setText(Tr::tr("%L1/s").arg(
                qRound((events - m_lastEvents.value(id)) / dt)));
        }

        m_lastEvents.insert(id, events);
    }

    m_lastUpdate.restart();

    // No hairline under the table's last visible row. isHidden() reflects the
    // explicit per-row visibility regardless of whether this page is showing
    // yet. Rows are gathered in display order: device, channels, host.
    QList<QWidget *> allRows = m_deviceRows;

    for(int i = 0; i < m_channelsLayout->count(); i++)
    {
        if(QWidget *widget = m_channelsLayout->itemAt(i)->widget())
        {
            allRows.append(widget);
        }
    }

    allRows += m_hostRows;

    int lastVisible = -1;

    for(int i = 0; i < allRows.size(); i++)
    {
        if(!allRows.at(i)->isHidden())
        {
            lastVisible = i;
        }
    }

    for(int i = 0; i < allRows.size(); i++)
    {
        viewRowSetLineVisible(allRows.at(i), i != lastVisible);
    }

    setCurrentIndex(1);
}

} // namespace Internal
} // namespace OpenMV
