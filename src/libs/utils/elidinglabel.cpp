// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "elidinglabel.h"

#include <QFontMetrics>
#include <QPainter>
#include <QStyle>

// OPENMV-DIFF //
#include <QStyleOption>
#include <utils/hostosinfo.h>
#include <utils/theme/theme.h>
// OPENMV-DIFF //

/*!
    \class Utils::ElidingLabel

    \brief The ElidingLabel class is a label suitable for displaying elided
    text.
*/

namespace Utils {

ElidingLabel::ElidingLabel(QWidget *parent)
    : ElidingLabel({}, parent)
{
}

ElidingLabel::ElidingLabel(const QString &text, QWidget *parent)
    : QLabel(text, parent)
{
    setElideMode(Qt::ElideRight);
}

Qt::TextElideMode ElidingLabel::elideMode() const
{
    return m_elideMode;
}

void ElidingLabel::setElideMode(const Qt::TextElideMode &elideMode)
{
    m_elideMode = elideMode;
    if (elideMode == Qt::ElideNone)
        updateToolTip({});

    // OPENMV-DIFF //
    // setSizePolicy(QSizePolicy(
    //                   m_elideMode == Qt::ElideNone ? QSizePolicy::Preferred : QSizePolicy::Ignored,
    //                   QSizePolicy::Preferred,
    //                   QSizePolicy::Label));
    // OPENMV-DIFF //
    update();
}

QString ElidingLabel::additionalToolTip()
{
    return m_additionalToolTip;
}

void ElidingLabel::setAdditionalToolTip(const QString &additionalToolTip)
{
    m_additionalToolTip = additionalToolTip;
}

void ElidingLabel::paintEvent(QPaintEvent *)
{
    if (m_elideMode == Qt::ElideNone) {
        QLabel::paintEvent(nullptr);
        updateToolTip({});
        return;
    }

    const int m = margin();
    QRect contents = contentsRect().adjusted(m, m, -m, -m);
    QFontMetrics fm = fontMetrics();
    QString txt = text();
    if (txt.length() > 4 && fm.horizontalAdvance(txt) > contents.width()) {
        // OPENMV-DIFF //
        // updateToolTip(txt);
        // OPENMV-DIFF //
        txt = fm.elidedText(txt, m_elideMode, contents.width());
    } else {
        // OPENMV-DIFF //
        // updateToolTip(QString());
        // OPENMV-DIFF //
    }
    int flags = QStyle::visualAlignment(layoutDirection(), alignment()) | Qt::TextSingleLine;

    QPainter painter(this);
    drawFrame(&painter);
    painter.drawText(contents, flags, txt);
}

void ElidingLabel::updateToolTip(const QString &elidedText)
{
    if (m_additionalToolTip.isEmpty()) {
        setToolTip(elidedText);
        return;
    }

    if (elidedText.isEmpty()) {
        setToolTip(m_additionalToolTip);
        return;
    }

    setToolTip(elidedText + m_additionalToolTipSeparator + m_additionalToolTip);
}

QString ElidingLabel::additionalToolTipSeparator() const
{
    return m_additionalToolTipSeparator;
}

void ElidingLabel::setAdditionalToolTipSeparator(const QString &newAdditionalToolTipSeparator)
{
    m_additionalToolTipSeparator = newAdditionalToolTipSeparator;
}

// OPENMV-DIFF //
void ElidingToolButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)

    const QFontMetrics fm = fontMetrics();
    const int baseLine = (height() - fm.height() + 1) / 2 + fm.ascent();

    QPainter p(this);

    QStyleOption styleOption;
    styleOption.initFrom(this);
    const bool hovered = !Utils::HostOsInfo::isMacHost() && (styleOption.state & QStyle::State_MouseOver);

    if(isEnabled())
    {
        Utils::Theme::Color c = Utils::Theme::BackgroundColorDark;

        if (hovered)
            c = Utils::Theme::BackgroundColorHover;
        else if (isDown() || isChecked())
            c = Utils::Theme::BackgroundColorSelected;

        if (c != Utils::Theme::BackgroundColorDark)
            p.fillRect(rect(), Utils::creatorTheme()->color(c));

        p.setFont(font());
        p.setPen(Utils::creatorTheme()->color(Utils::Theme::OutputPaneToggleButtonTextColorChecked));

        if (!isChecked())
            p.setPen(Utils::creatorTheme()->color(Utils::Theme::OutputPaneToggleButtonTextColorUnchecked));
    }
    else
    {
        p.setPen(Utils::creatorTheme()->color(Utils::Theme::TextColorDisabled));
    }

    p.drawText(4, baseLine - 1, fm.elidedText(text(), Qt::ElideRight, width() - 5));
}
// OPENMV-DIFF //

} // namespace Utils
