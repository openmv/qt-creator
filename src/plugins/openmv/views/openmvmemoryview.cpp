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

#include "openmvmemoryview.h"
#include "openmvviewstyle.h"
#include "openmvtr.h"

#include <utils/theme/theme.h>

namespace OpenMV {
namespace Internal {

// Accent colors from the shared plot palette (theme-adjusted): blue for
// usage, red for the peak marker.
static QColor usedColor()
{
    return viewPlotColor(ViewPlotBlue);
}

static QColor peakColor()
{
    return viewPlotColor(ViewPlotRed);
}

static QString formatBytes(quint32 bytes)
{
    if(bytes >= (1024 * 1024))
    {
        return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + QStringLiteral(" MB");
    }

    if(bytes >= 1024)
    {
        return QString::number(bytes / 1024.0, 'f', 1) + QStringLiteral(" KB");
    }

    return QString::number(bytes) + QStringLiteral(" B");
}

static QString decodeUmaFlags(uint flags)
{
    static const QPair<uint, const char *> names[] = {
        { 1u << 0, "FAST" },
        { 1u << 1, "ITCM" },
        { 1u << 2, "DTCM" },
        { 1u << 3, "DMA_D1" },
        { 1u << 4, "DMA_D2" },
        { 1u << 5, "DMA_D3" },
        { 1u << 6, "TRANSIENT" },
    };

    QStringList list;

    for(size_t i = 0; i < (sizeof(names) / sizeof(names[0])); i++)
    {
        if(flags & names[i].first)
        {
            list.append(QLatin1String(names[i].second));
        }
    }

    return list.join(QLatin1Char('|'));
}

///////////////////////////////////////////////////////////////////////////////

OpenMVMemoryGraph::OpenMVMemoryGraph(QWidget *parent) : QWidget(parent),
    m_history(Q_NULLPTR),
    m_peak(0)
{
    setFixedHeight(80);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void OpenMVMemoryGraph::setHistory(const QVector<QPair<quint32, quint32> > *history, quint32 peak)
{
    m_history = history;
    m_peak = peak;
    QWidget::update();
}

void OpenMVMemoryGraph::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();
    int h = height();

    painter.fillRect(rect(), Utils::creatorTheme()->color(Utils::Theme::BackgroundColorNormal));

    if((!m_history) || (m_history->size() < 2))
    {
        return;
    }

    quint32 maxTotal = 0;

    for(int i = 0; i < m_history->size(); i++)
    {
        maxTotal = qMax(maxTotal, m_history->at(i).second);
    }

    if(!maxTotal)
    {
        return;
    }

    bool dark = Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface);
    QColor faint = Utils::creatorTheme()->color(Utils::Theme::TextColorNormal);

    // Grid lines at 25%, 50%, 75%
    faint.setAlpha(dark ? 13 : 30);
    painter.setPen(QPen(faint, 1));

    for(int q = 1; q <= 3; q++)
    {
        qreal y = h - ((q / 4.0) * h);
        painter.drawLine(QPointF(0, y), QPointF(w, y));
    }

    // Total line (dimmed) - shows pool growth/shrink over the window.
    faint.setAlpha(dark ? 38 : 64);
    painter.setPen(QPen(faint, 1));

    QPolygonF totalLine;

    for(int i = 0; i < m_history->size(); i++)
    {
        totalLine.append(QPointF((qreal(i) / (HISTORY_MAX - 1)) * w,
                                 h - ((qreal(m_history->at(i).second) / maxTotal) * h)));
    }

    painter.drawPolyline(totalLine);

    // Used area (fill) + line.
    QPolygonF usedLine;

    for(int i = 0; i < m_history->size(); i++)
    {
        usedLine.append(QPointF((qreal(i) / (HISTORY_MAX - 1)) * w,
                                h - ((qreal(m_history->at(i).first) / maxTotal) * h)));
    }

    QPolygonF usedFill = usedLine;
    usedFill.append(QPointF(usedLine.last().x(), h));
    usedFill.append(QPointF(usedLine.first().x(), h));

    QColor used = usedColor();
    QColor usedFillColor = used;
    usedFillColor.setAlpha(51);

    painter.setPen(Qt::NoPen);
    painter.setBrush(usedFillColor);
    painter.drawPolygon(usedFill);

    painter.setPen(QPen(used, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(usedLine);

    // Peak line (dashed) with a small label, skipped when it would sit on
    // the top edge of the graph.
    if(m_peak && (m_peak < (maxTotal * 0.95)))
    {
        qreal peakY = h - ((qreal(m_peak) / maxTotal) * h);

        QColor peakLine = peakColor();
        peakLine.setAlpha(dark ? 128 : 200);

        QPen peakPen(peakLine, 1, Qt::DashLine);
        painter.setPen(peakPen);
        painter.drawLine(QPointF(0, peakY), QPointF(w, peakY));

        QColor peakText = peakColor();
        peakText.setAlpha(dark ? 178 : 255);

        QFont small = font();
        small.setPointSizeF(qMax(6.0, small.pointSizeF() - 2.0));
        painter.setFont(small);
        painter.setPen(peakText);
        painter.drawText(QPointF(w - painter.fontMetrics().horizontalAdvance(Tr::tr("peak")) - 6,
                                 peakY - 3), Tr::tr("peak"));
    }
}

///////////////////////////////////////////////////////////////////////////////

OpenMVMemoryCard::OpenMVMemoryCard(QWidget *parent) : QFrame(parent)
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    // Text insets horizontally on its own below; the graph runs full-bleed
    // to the view edges, like the histogram's plots.
    layout->setContentsMargins(0, 4, 0, 4);
    layout->setSpacing(4);

    // All text is mouse-selectable so figures can be copied out. The header
    // bolds via stylesheet (an explicit QFont would freeze its size and stop
    // parent font changes from propagating).
    auto makeSelectable = [](QLabel *label) -> QLabel * {
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        return label;
    };

    m_header = makeSelectable(new QLabel);
    m_header->setStyleSheet(QStringLiteral("font-weight: bold"));
    m_header->setContentsMargins(6, 0, 6, 0);
    layout->addWidget(m_header);

    m_graph = new OpenMVMemoryGraph;
    layout->addWidget(m_graph);

    QGridLayout *grid = new QGridLayout;
    grid->setContentsMargins(6, 0, 6, 0);
    grid->setSpacing(2);

    grid->addWidget(makeSelectable(new QLabel(Tr::tr("Used / Total"))), 0, 0);
    m_usedValue = makeSelectable(new QLabel);
    m_usedValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(m_usedValue, 0, 1);

    grid->addWidget(makeSelectable(new QLabel(Tr::tr("Free"))), 1, 0);
    m_freeValue = makeSelectable(new QLabel);
    m_freeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(m_freeValue, 1, 1);

    m_persistLabel = makeSelectable(new QLabel(Tr::tr("Persist")));
    grid->addWidget(m_persistLabel, 2, 0);
    m_persistValue = makeSelectable(new QLabel);
    m_persistValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(m_persistValue, 2, 1);

    m_peakLabel = makeSelectable(new QLabel(Tr::tr("Peak")));
    grid->addWidget(m_peakLabel, 3, 0);
    m_peakValue = makeSelectable(new QLabel);
    m_peakValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(m_peakValue, 3, 1);

    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);
}

void OpenMVMemoryCard::setData(const QVariantMap &entry, int umaIndex,
                              const QVector<QPair<quint32, quint32> > *history, quint32 peak)
{
    bool isGC = entry.value(QStringLiteral("mem_type")).toString() == QStringLiteral("gc");

    if(isGC)
    {
        m_header->setText(Tr::tr("GC Heap"));
    }
    else
    {
        QString flags = decodeUmaFlags(entry.value(QStringLiteral("flags")).toUInt());
        m_header->setText(flags.isEmpty()
            ? Tr::tr("UMA Pool %L1").arg(umaIndex)
            : Tr::tr("UMA Pool %L1 (%L2)").arg(umaIndex).arg(flags));
    }

    quint32 total = entry.value(QStringLiteral("total")).toUInt();
    quint32 used = entry.value(QStringLiteral("used")).toUInt() +
                   entry.value(QStringLiteral("persist")).toUInt();
    int pct = total ? qRound((qreal(used) / total) * 100) : 0;

    m_usedValue->setText(Tr::tr("%L1 / %L2 (%L3%)")
        .arg(formatBytes(used)).arg(formatBytes(total)).arg(pct));
    m_freeValue->setText(formatBytes(entry.value(QStringLiteral("free")).toUInt()));

    // The firmware only tracks persist for UMA pools. It doesn't track a GC
    // peak either (the field is 0), so the GC card reports the highest usage
    // this view has observed since connecting -- the graph's peak line.
    m_persistLabel->setVisible(!isGC);
    m_persistValue->setVisible(!isGC);

    if(isGC)
    {
        m_peakValue->setText(formatBytes(peak));
        m_peakValue->setToolTip(Tr::tr("Highest usage observed while connected"));
    }
    else
    {
        m_persistValue->setText(formatBytes(entry.value(QStringLiteral("persist")).toUInt()));
        m_peakValue->setText(formatBytes(entry.value(QStringLiteral("peak")).toUInt()));
        m_peakValue->setToolTip(QString());
    }

    m_graph->setHistory(history, peak);
}

///////////////////////////////////////////////////////////////////////////////

OpenMVMemoryView::OpenMVMemoryView(QWidget *parent) : QStackedWidget(parent)
{
    viewApplyBackground(this);

    // Page 0: a status message, centered and styled like the frame buffer's
    // "No Image" text.
    m_message = new QLabel;
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    addWidget(m_message);

    // Page 1: the scrollable list of pool cards.
    QScrollArea *scrollArea = new QScrollArea;
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->viewport()->setAutoFillBackground(false);

    QWidget *container = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 4, 0, 4); // cards inset their own text
    layout->setSpacing(4);

    m_cardsLayout = new QVBoxLayout;
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(4);
    layout->addLayout(m_cardsLayout);

    layout->addStretch(1);

    scrollArea->setWidget(container);
    addWidget(scrollArea);

    reset();
}

void OpenMVMemoryView::clearCards()
{
    // Cards and their separator lines both live in m_cardsLayout.
    while(QLayoutItem *item = m_cardsLayout->takeAt(0))
    {
        delete item->widget();
        delete item;
    }

    m_cards.clear();
    m_histories.clear();
    m_peaks.clear();
}

void OpenMVMemoryView::showMessage(const QString &message)
{
    clearCards();

    m_message->setText(viewMessageHtml(message));
    setCurrentIndex(0);
}

void OpenMVMemoryView::reset()
{
    showMessage(Tr::tr("Connect a camera to view memory usage"));
}

void OpenMVMemoryView::memoryStats(const QVariantList &entries)
{
    if(entries.isEmpty())
    {
        showMessage(Tr::tr("Memory usage is not available for this camera"));
        return;
    }

    if(m_cards.size() != entries.size())
    {
        clearCards();

        for(int i = 0; i < entries.size(); i++)
        {
            if(i)
            {
                // Faint 1px separator between pools instead of per-card
                // borders, full-bleed like the other views' hairlines.
                QWidget *line = new QWidget;
                line->setFixedHeight(1);
                line->setAutoFillBackground(true);

                QColor color = palette().color(QPalette::Text);
                color.setAlpha(38);

                QPalette linePalette = line->palette();
                linePalette.setColor(QPalette::Window, color);
                line->setPalette(linePalette);

                QWidget *divider = new QWidget;
                QHBoxLayout *dividerLayout = new QHBoxLayout(divider);
                dividerLayout->setContentsMargins(0, 0, 0, 0);
                dividerLayout->addWidget(line);
                m_cardsLayout->addWidget(divider);
            }

            OpenMVMemoryCard *card = new OpenMVMemoryCard;
            m_cardsLayout->addWidget(card);
            m_cards.append(card);
            m_histories.append(QVector<QPair<quint32, quint32> >());
            m_peaks.append(0);
        }
    }

    setCurrentIndex(1);

    int umaIndex = 0;

    for(int i = 0; i < entries.size(); i++)
    {
        QVariantMap entry = entries.at(i).toMap();

        quint32 used = entry.value(QStringLiteral("used")).toUInt() +
                       entry.value(QStringLiteral("persist")).toUInt();
        quint32 total = entry.value(QStringLiteral("total")).toUInt();

        m_histories[i].append(qMakePair(used, total));

        if(m_histories.at(i).size() > OpenMVMemoryGraph::HISTORY_MAX)
        {
            m_histories[i].removeFirst();
        }

        m_peaks[i] = qMax(m_peaks.at(i), used);

        bool isGC = entry.value(QStringLiteral("mem_type")).toString() == QStringLiteral("gc");
        m_cards.at(i)->setData(entry, isGC ? 0 : umaIndex, &m_histories.at(i), m_peaks.at(i));

        if(!isGC)
        {
            umaIndex++;
        }
    }
}

} // namespace Internal
} // namespace OpenMV
