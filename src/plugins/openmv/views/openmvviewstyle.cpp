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

#include "openmvviewstyle.h"

#include <utils/theme/theme.h>

namespace OpenMV {
namespace Internal {

// 1px separator in the text color, heavily faded.
static QWidget *hairline()
{
    QWidget *line = new QWidget;
    line->setFixedHeight(1);
    line->setAutoFillBackground(true);

    QColor color = line->palette().color(QPalette::Text);
    color.setAlpha(25);

    QPalette palette = line->palette();
    palette.setColor(QPalette::Window, color);
    line->setPalette(palette);
    return line;
}

// Fade a label towards the background (Studio's tertiary text).
static void mute(QLabel *label)
{
    QPalette palette = label->palette();
    QColor color = palette.color(QPalette::Text);
    color.setAlpha(150);
    palette.setColor(QPalette::WindowText, color);
    palette.setColor(QPalette::Text, color);
    label->setPalette(palette);
}

QWidget *viewSectionLabel(const QString &text)
{
    QLabel *label = new QLabel(text);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(QStringLiteral("font-weight: bold"));
    label->setContentsMargins(4, 4, 4, 2);

    QWidget *section = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(label);
    layout->addWidget(hairline());
    return section;
}

QLabel *viewNameLabel(const QString &text)
{
    QLabel *label = new QLabel(text);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mute(label);
    return label;
}

QLabel *viewValueLabel()
{
    QLabel *label = new QLabel(QStringLiteral("--"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    // Values render in the monospace font at the UI size (Studio's code
    // font), which visually separates them from the muted names.
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(label->font().pointSizeF());
    label->setFont(mono);
    return label;
}

QWidget *viewRow(QWidget *name, const QList<QWidget *> &values)
{
    QWidget *content = new QWidget;
    QHBoxLayout *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(4, 3, 4, 3);
    contentLayout->setSpacing(8);
    contentLayout->addWidget(name);
    contentLayout->addStretch(1);

    for(QWidget *value : values)
    {
        contentLayout->addWidget(value);
    }

    QWidget *row = new QWidget;
    QVBoxLayout *rowLayout = new QVBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);
    rowLayout->addWidget(content);
    rowLayout->addWidget(hairline());
    return row;
}

QWidget *viewRow(const QString &name, const QList<QWidget *> &values)
{
    return viewRow(viewNameLabel(name), values);
}

QWidget *viewRow(const QString &name, QWidget *value)
{
    return viewRow(name, QList<QWidget *>() << value);
}

void viewRowSetLineVisible(QWidget *row, bool visible)
{
    // The hairline is the second item of the row's vertical layout.
    QLayoutItem *item = row->layout()->itemAt(1);

    if(item && item->widget())
    {
        item->widget()->setVisible(visible);
    }
}

QString viewMessageHtml(const QString &message)
{
    // The same markup the frame buffer uses for its "No Image" placeholder.
    return QStringLiteral("<html><body style=\"color:%1;font-size:14px\">"
        "<div align=\"center\">"
        "<div style=\"font-size:20px\">%2</div>"
        "</div>"
        "</body></html>").arg(Utils::creatorTheme()->color(Utils::Theme::TextColorDisabled).name(), message);
}

} // namespace Internal
} // namespace OpenMV
