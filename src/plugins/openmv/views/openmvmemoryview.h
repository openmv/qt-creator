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

// Memory view for the histogram pane's view selector: one card per memory
// pool (the MicroPython GC heap plus each UMA pool) with a rolling usage
// graph, a peak marker, and used/free/persist/peak figures. Fed by
// OpenMVPluginIO::memoryStats() polling (SYS_MEMORY, protocol >= 1.0.2);
// an empty result means the connected camera does not support it.

#ifndef OPENMVMEMORYVIEW_H
#define OPENMVMEMORYVIEW_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

namespace OpenMV {
namespace Internal {

class OpenMVMemoryGraph : public QWidget
{
    Q_OBJECT

public:

    explicit OpenMVMemoryGraph(QWidget *parent = Q_NULLPTR);

    void setHistory(const QVector<QPair<quint32, quint32> > *history, quint32 peak);

    static const int HISTORY_MAX = 120;

protected:

    virtual void paintEvent(QPaintEvent *event);

private:

    const QVector<QPair<quint32, quint32> > *m_history; // (used, total) samples
    quint32 m_peak;
};

class OpenMVMemoryCard : public QFrame
{
    Q_OBJECT

public:

    explicit OpenMVMemoryCard(QWidget *parent = Q_NULLPTR);

    void setData(const QVariantMap &entry, int umaIndex,
                const QVector<QPair<quint32, quint32> > *history, quint32 peak);

private:

    QLabel *m_header;
    OpenMVMemoryGraph *m_graph;
    QLabel *m_usedValue;
    QLabel *m_freeValue;
    QLabel *m_persistLabel;
    QLabel *m_persistValue;
    QLabel *m_peakLabel;
    QLabel *m_peakValue;
};

// A two-page stack: a big centered status message (styled like the frame
// buffer's "No Image" text) or the scrollable list of pool cards.
class OpenMVMemoryView : public QStackedWidget
{
    Q_OBJECT

public:

    explicit OpenMVMemoryView(QWidget *parent = Q_NULLPTR);

public slots:

    // An empty list shows the "not supported" message when previously
    // connected data was flowing; reset() shows the "connect" message.
    void memoryStats(const QVariantList &entries);
    void reset();

private:

    void clearCards();
    void showMessage(const QString &message);

    QLabel *m_message;
    QVBoxLayout *m_cardsLayout;
    QList<OpenMVMemoryCard *> m_cards;
    QList<QVector<QPair<quint32, quint32> > > m_histories; // (used, total)
    QList<quint32> m_peaks; // highest used seen this session, per pool
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVMEMORYVIEW_H
