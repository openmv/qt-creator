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

// 1px separator in the theme's text color, heavily faded. Its own
// stylesheet wins over the view-wide background rule.
QWidget *viewHairline()
{
    QWidget *line = new QWidget;
    line->setFixedHeight(1);
    line->setAttribute(Qt::WA_StyledBackground);

    QColor color = Utils::creatorTheme()->color(Utils::Theme::TextColorNormal);
    line->setStyleSheet(QStringLiteral("background-color:rgba(%1,%2,%3,25);")
        .arg(color.red()).arg(color.green()).arg(color.blue()));
    return line;
}

QColor viewPlotColor(int color)
{
    if(color == ViewPlotGray)
    {
        QColor gray = Utils::creatorTheme()->color(Utils::Theme::TextColorNormal);
        gray.setAlpha(160);
        return gray;
    }

    static const QColor colors[ViewPlotColorCount] = {
        QColor(0x5b, 0x9c, 0xf5), // blue
        QColor(0xf0, 0x55, 0x55), // red
        QColor(0x4e, 0xc9, 0x62), // green
        QColor(0xfc, 0xc0, 0x2c), // yellow
        QColor(0xc9, 0x6b, 0xf0), // purple
        QColor(0x2c, 0xd5, 0xd5), // cyan
        QColor(0xf5, 0x8b, 0x3c), // orange
    };

    QColor result = colors[qBound(0, color, int(ViewPlotColorCount) - 1)];

    // The dark theme's pastels wash out on the light theme's white.
    if(!Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface))
    {
        result = result.darker(130);
    }

    return result;
}

QPen viewPlotPen(int color)
{
    return QPen(viewPlotColor(color), 1.5);
}

QBrush viewPlotBrush(int color)
{
    QColor fill = viewPlotColor(color);
    fill.setAlpha(51);
    return fill;
}

// Ring `rect` in the highlight colour: square when radius is 0, rounded when
// positive, and a circle when negative (for the round radio indicator).
static void paintHoverGlow(QWidget *widget, const QRect &rect, qreal radius = 0.0, qreal outset = -1.0)
{
    if((!widget->isEnabled()) || (!widget->underMouse()) || rect.isEmpty())
    {
        return;
    }

    // A positive outset grows the ring outside the control, a negative one
    // insets it (the slider needs that: its handle spans the widget's full
    // height, so a ring on that edge would fall on the widget boundary). Only
    // grow as far as there is room on every side -- clipping a side instead
    // leaves a flat edge rather than a ring, and a check box is barely taller
    // than its indicator.
    QRect bounds = widget->rect();
    qreal room = qMin(qMin(rect.left() - bounds.left(), bounds.right() - rect.right()),
                      qMin(rect.top() - bounds.top(), bounds.bottom() - rect.bottom()));
    qreal applied = qMin(outset, room);
    QRectF inner = QRectF(rect).adjusted(-applied, -applied, applied, applied);

    if(inner.isEmpty())
    {
        return;
    }

    QColor glow = Utils::creatorTheme()->color(Utils::Theme::PaletteHighlight);
    QPainter painter(widget);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(glow, 1));
    QRectF ring = inner.adjusted(0.5, 0.5, -0.5, -0.5);

    if(radius < 0.0)
    {
        painter.drawEllipse(ring);
    }
    else if(radius > 0.0)
    {
        painter.drawRoundedRect(ring, radius, radius);
    }
    else
    {
        painter.drawRect(ring);
    }
}

// The ring overlaps the indicator's own border rather than sitting outside it:
// QCommonStyle places the indicator flush with the widget edge and a widget
// cannot paint beyond its own rect, so an outside ring would need the control
// shifted -- and overlapping reads better anyway.
static const qreal GLOW_OUTSET = 0.0;

HoverGlowCheckBox::HoverGlowCheckBox(QWidget *parent) : QCheckBox(parent)
{
    setAttribute(Qt::WA_Hover);
}

void HoverGlowCheckBox::paintEvent(QPaintEvent *event)
{
    QCheckBox::paintEvent(event);
    QStyleOptionButton option;
    initStyleOption(&option);
    paintHoverGlow(this, style()->subElementRect(QStyle::SE_CheckBoxIndicator, &option, this), 0.0, GLOW_OUTSET);
}

void HoverGlowCheckBox::enterEvent(QEnterEvent *event) { QCheckBox::enterEvent(event); update(); }
void HoverGlowCheckBox::leaveEvent(QEvent *event) { QCheckBox::leaveEvent(event); update(); }

HoverGlowRadioButton::HoverGlowRadioButton(const QString &text, QWidget *parent) : QRadioButton(text, parent)
{
    setAttribute(Qt::WA_Hover);
}

void HoverGlowRadioButton::paintEvent(QPaintEvent *event)
{
    QRadioButton::paintEvent(event);
    QStyleOptionButton option;
    initStyleOption(&option);
    // Circular, to follow the round indicator (the checkbox's is square).
    paintHoverGlow(this, style()->subElementRect(QStyle::SE_RadioButtonIndicator, &option, this), -1.0, GLOW_OUTSET);
}

void HoverGlowRadioButton::enterEvent(QEnterEvent *event) { QRadioButton::enterEvent(event); update(); }
void HoverGlowRadioButton::leaveEvent(QEvent *event) { QRadioButton::leaveEvent(event); update(); }

HoverGlowSlider::HoverGlowSlider(Qt::Orientation orientation, QWidget *parent) : QSlider(orientation, parent)
{
    setAttribute(Qt::WA_Hover);
}

void HoverGlowSlider::paintEvent(QPaintEvent *event)
{
    QSlider::paintEvent(event);
    QStyleOptionSlider option;
    initStyleOption(&option);
    // Rounded here, unlike the square indicators, to follow the handle's shape.
    paintHoverGlow(this, style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this), 3.0);
}

void HoverGlowSlider::enterEvent(QEnterEvent *event) { QSlider::enterEvent(event); update(); }
void HoverGlowSlider::leaveEvent(QEvent *event) { QSlider::leaveEvent(event); update(); }

HoverGlowSpinBox::HoverGlowSpinBox(QWidget *parent) : QDoubleSpinBox(parent)
{
    setAttribute(Qt::WA_Hover);
    setMouseTracking(true);   // so the shade follows between the two buttons
}

void HoverGlowSpinBox::paintEvent(QPaintEvent *event)
{
    QDoubleSpinBox::paintEvent(event);

    if((!isEnabled()) || (!underMouse()))
    {
        return;
    }

    // Take the button strip as whatever sits right of the edit field, then
    // split it in half. SC_SpinBoxUp/Down report rects that do not match what
    // the style paints on every theme, which shaded the whole control.
    QStyleOptionSpinBox option;
    initStyleOption(&option);
    QRect buttons = rect();
    buttons.setLeft(style()->subControlRect(QStyle::CC_SpinBox, &option,
                                            QStyle::SC_SpinBoxEditField, this).right() + 1);
    QPoint pos = mapFromGlobal(QCursor::pos());

    if((buttons.width() <= 0) || (!buttons.contains(pos)))
    {
        return;
    }

    QRect target = buttons;

    if(pos.y() < buttons.center().y())
    {
        target.setBottom(buttons.center().y());
    }
    else
    {
        target.setTop(buttons.center().y() + 1);
    }

    // Shading with the text colour darkens on light themes and lightens on dark
    // ones; the alpha keeps the arrow underneath readable.
    QColor shade = Utils::creatorTheme()->color(Utils::Theme::TextColorNormal);
    shade.setAlpha(40);
    QPainter painter(this);
    painter.fillRect(target.adjusted(1, 1, -1, -1), shade);
}

void HoverGlowSpinBox::enterEvent(QEnterEvent *event) { QDoubleSpinBox::enterEvent(event); update(); }
void HoverGlowSpinBox::leaveEvent(QEvent *event) { QDoubleSpinBox::leaveEvent(event); update(); }
void HoverGlowSpinBox::mouseMoveEvent(QMouseEvent *event) { QDoubleSpinBox::mouseMoveEvent(event); update(); }

void viewApplyBackground(QWidget *view)
{
    // Colour the pane through the palette, not a stylesheet. A stylesheet
    // anywhere in the ancestry forces every child into QStyleSheetStyle, which
    // drops native hover (slider handle, check/radio indicators), native
    // disabled greying, and native combobox drop-down / context-menu rendering.
    // The app uses ManhattanStyle (palette-driven, no global stylesheet), so an
    // explicit palette here propagates to the pane's children and overrides the
    // white WindowText the dark-toolbar hierarchy otherwise hands down (which is
    // why the histogram-era code reached for a stylesheet).
    Utils::Theme *t = Utils::creatorTheme();
    QColor bg = t->color(Utils::Theme::BackgroundColorNormal);
    QColor fg = t->color(Utils::Theme::TextColorNormal);

    QPalette pal = view->palette();
    pal.setColor(QPalette::Window, bg);
    pal.setColor(QPalette::Base, bg);
    pal.setColor(QPalette::Button, bg);
    pal.setColor(QPalette::WindowText, fg);
    pal.setColor(QPalette::Text, fg);
    pal.setColor(QPalette::ButtonText, fg);
    view->setPalette(pal);
    view->setBackgroundRole(QPalette::Window);
    view->setAutoFillBackground(true);
}

QWidget *viewSectionLabel(const QString &text)
{
    QLabel *label = new QLabel(text);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setStyleSheet(QStringLiteral("font-weight: bold"));
    // Text insets from the view edge; the band and hairline below run
    // full-bleed (the view containers have no horizontal margins, like the
    // histogram).
    label->setContentsMargins(6, 4, 6, 3);

    // A markedly darker underline than the row hairlines sets headers
    // apart without eating horizontal space the way indentation would.
    QWidget *line = new QWidget;
    line->setFixedHeight(1);
    line->setAttribute(Qt::WA_StyledBackground);

    QColor color = Utils::creatorTheme()->color(Utils::Theme::TextColorNormal);
    line->setStyleSheet(QStringLiteral("background-color:rgba(%1,%2,%3,90);")
        .arg(color.red()).arg(color.green()).arg(color.blue()));

    QWidget *section = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(section);
    layout->setContentsMargins(0, 6, 0, 0); // air above each section
    layout->setSpacing(0);
    layout->addWidget(label);
    layout->addWidget(line);
    return section;
}

QLabel *viewNameLabel(const QString &text)
{
    // Full-strength text like the histogram's labels; the monospace values
    // alone set the two columns apart.
    QLabel *label = new QLabel(text);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
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
    contentLayout->setContentsMargins(6, 3, 6, 3); // text inset; the hairline runs full-bleed
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
    rowLayout->addWidget(viewHairline());
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
