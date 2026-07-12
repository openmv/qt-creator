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

// Statistics view for the histogram pane's view selector: the device's
// protocol counters and per-channel event totals/rates (section order
// mirrors OpenMV Studio's Statistics tab), plus the IDE's own transport
// counters. Fed by OpenMVPluginIO::getProtocolStats() polling.

#ifndef OPENMVSTATISTICSVIEW_H
#define OPENMVSTATISTICSVIEW_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

namespace OpenMV {
namespace Internal {

// A two-page stack: a big centered status message (styled like the frame
// buffer's "No Image" text) or the stats list.
class OpenMVStatisticsView : public QStackedWidget
{
    Q_OBJECT

public:

    explicit OpenMVStatisticsView(QWidget *parent = Q_NULLPTR);

public slots:

    // Empty host and device maps show the "not available" message; reset()
    // shows the "connect" message and drops the rate tracking.
    void protocolStats(const QVariantMap &host, const QVariantMap &device,
                       const QVariantList &channels);
    void reset();

private:

    void showMessage(const QString &message);
    void rebuildChannelRows(const QVariantList &channels);

    QLabel *m_message;
    QList<QWidget *> m_deviceRows;  // one per device counter, in wire order
    QList<QLabel *> m_deviceValues;
    QList<QWidget *> m_hostRows;    // one per host counter
    QList<QLabel *> m_hostValues;

    QWidget *m_channelsHeader;
    QVBoxLayout *m_channelsLayout;
    QString m_channelsSchema;             // "id:name|..." of the built rows
    QList<QLabel *> m_channelValues;      // count label per channel row
    QList<QLabel *> m_channelRates;       // rate label per channel row
    QMap<int, quint32> m_lastEvents;      // per-channel totals at last update
    QElapsedTimer m_lastUpdate;           // for event rates
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVSTATISTICSVIEW_H
