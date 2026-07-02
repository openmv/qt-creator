// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "minisplitter.h"

#include "generalsettings.h"

#include <utils/stylehelper.h>
#include <utils/theme/theme.h>

#include <QApplication>
#include <QPaintEvent>
#include <QPainter>
#include <QSplitterHandle>

namespace Core {
namespace Internal {

static QBitmap scaledBitmap(const QBitmap &other, qreal factor)
{
    QTransform trans = QTransform::fromScale(factor, factor);
    return other.transformed(trans);
}

// cursor images / masks taken from qplatformcursor.cpp
static QCursor hsplitCursor(qreal ratio)
{
    static const uchar hsplit_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00,
        0x00, 0x40, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00,
        0x00, 0x41, 0x82, 0x00, 0x80, 0x41, 0x82, 0x01, 0xc0, 0x7f, 0xfe, 0x03,
        0x80, 0x41, 0x82, 0x01, 0x00, 0x41, 0x82, 0x00, 0x00, 0x40, 0x02, 0x00,
        0x00, 0x40, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00,
        0x00, 0x40, 0x02, 0x00, 0x00, 0x40, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uchar hsplitm_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0xe0, 0x07, 0x00, 0x00, 0xe0, 0x07, 0x00, 0x00, 0xe0, 0x07, 0x00,
        0x00, 0xe0, 0x07, 0x00, 0x00, 0xe2, 0x47, 0x00, 0x00, 0xe3, 0xc7, 0x00,
        0x80, 0xe3, 0xc7, 0x01, 0xc0, 0xff, 0xff, 0x03, 0xe0, 0xff, 0xff, 0x07,
        0xc0, 0xff, 0xff, 0x03, 0x80, 0xe3, 0xc7, 0x01, 0x00, 0xe3, 0xc7, 0x00,
        0x00, 0xe2, 0x47, 0x00, 0x00, 0xe0, 0x07, 0x00, 0x00, 0xe0, 0x07, 0x00,
        0x00, 0xe0, 0x07, 0x00, 0x00, 0xe0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static QBitmap cursorImg = QBitmap::fromData({32, 32}, hsplit_bits);
    static QBitmap mask = QBitmap::fromData({32, 32}, hsplitm_bits);
    return QCursor(scaledBitmap(cursorImg, ratio), scaledBitmap(mask, ratio),
                   15 * ratio, 15 * ratio);
}

static QCursor vsplitCursor(qreal ratio)
{
    static const uchar vsplit_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x80, 0x00, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0xe0, 0x03, 0x00,
        0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xff, 0x7f, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x7f, 0x00,
        0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x80, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xe0, 0x03, 0x00,
        0x00, 0xc0, 0x01, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static const uchar vsplitm_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0xc0, 0x01, 0x00, 0x00, 0xe0, 0x03, 0x00, 0x00, 0xf0, 0x07, 0x00,
        0x00, 0xf8, 0x0f, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0xc0, 0x01, 0x00,
        0x00, 0xc0, 0x01, 0x00, 0x80, 0xff, 0xff, 0x00, 0x80, 0xff, 0xff, 0x00,
        0x80, 0xff, 0xff, 0x00, 0x80, 0xff, 0xff, 0x00, 0x80, 0xff, 0xff, 0x00,
        0x80, 0xff, 0xff, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0xc0, 0x01, 0x00,
        0x00, 0xc0, 0x01, 0x00, 0x00, 0xf8, 0x0f, 0x00, 0x00, 0xf0, 0x07, 0x00,
        0x00, 0xe0, 0x03, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0x80, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    static QBitmap cursorImg = QBitmap::fromData({32, 32}, vsplit_bits);
    static QBitmap mask = QBitmap::fromData({32, 32}, vsplitm_bits);
    return QCursor(scaledBitmap(cursorImg, ratio), scaledBitmap(mask, ratio),
                   15 * ratio, 15 * ratio);
}

class MiniSplitterHandle : public QSplitterHandle
{
public:
    MiniSplitterHandle(Qt::Orientation orientation, QSplitter *parent, bool lightColored = false)
            : QSplitterHandle(orientation, parent),
              m_lightColored(lightColored)
    {
        // OPENMV-DIFF //
        // setMask(QRegion(contentsRect()));
        // setAttribute(Qt::WA_MouseNoMask, true);
        // OPENMV-DIFF //
        // No masks: the handle is a non-opaque child, so anything paintEvent leaves unpainted is
        // composited from the parent automatically. The 1px line look comes from painting only
        // contentsRect(). Masks left stale pixels behind whenever they shrank (the uncovered
        // region belonged to nobody who repainted it).
        // OPENMV-DIFF //
    }
protected:
    bool event(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_lightColored;
    // OPENMV-DIFF //
    bool m_hovering = false;
    // OPENMV-DIFF //
};

} // namespace Internal
} // namespace Core

using namespace Core;
using namespace Core::Internal;

bool MiniSplitterHandle::event(QEvent *event)
{
    // OPENMV-DIFF //
    // Hover halo: the handle is wider than the 1px line it draws, so while hovered glow across
    // the full width to make the draggable seam visible -- the cursor change alone is easy to
    // miss. Just a flag + repaint; unpainted areas composite from the parent (no masks).
    if (event->type() == QEvent::HoverEnter) {
        m_hovering = true;
        update();
    } else if (event->type() == QEvent::HoverLeave) {
        m_hovering = false;
        update();
    }
    // OPENMV-DIFF //
    if (generalSettings().provideSplitterCursors()) {
        if (event->type() == QEvent::HoverEnter) {
            const qreal ratio = screen()->devicePixelRatio();
            setCursor(orientation() == Qt::Horizontal ? hsplitCursor(ratio) : vsplitCursor(ratio));
        } else if (event->type() == QEvent::HoverLeave) {
            unsetCursor();
        }
    }
    return QSplitterHandle::event(event);
}

void MiniSplitterHandle::resizeEvent(QResizeEvent *event)
{
    if (orientation() == Qt::Horizontal)
        setContentsMargins(2, 0, 2, 0);
    else
        setContentsMargins(0, 2, 0, 2);
    // OPENMV-DIFF //
    // setMask(QRegion(contentsRect()));
    // QSplitterHandle::resizeEvent(event);
    // OPENMV-DIFF //
    // No mask (see the constructor); the margins above only define contentsRect for painting.
    // Do NOT call QSplitterHandle::resizeEvent here: with a handle wider than 2px it resets the
    // contents margins to zero (Qt's built-in wide-handle mode) -- and since every margin change
    // synthesizes another resize event, the base and this override ping-pong the margins in
    // infinite mutual recursion (stack overflow at startup). Its only other duty is
    // QWidget::resizeEvent, so call that directly.
    QWidget::resizeEvent(event);
    // OPENMV-DIFF //
}

void MiniSplitterHandle::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    // OPENMV-DIFF //
    Q_UNUSED(event)
    // Hover halo: translucent accent across the full handle width with a solid accent line in
    // the center (where the normal 1px line lives). The palette highlight tracks the theme, so
    // it is blue-ish in both dark and light modes.
    if (m_hovering) {
        const QColor accent = palette().color(QPalette::Highlight);
        QColor halo = accent;
        halo.setAlpha(80);
        painter.fillRect(rect(), halo);
        painter.fillRect(contentsRect(), accent);
        return;
    }
    // OPENMV-DIFF //
    // OPENMV-DIFF //
    // const QColor color = Utils::creatorColor(
    // OPENMV-DIFF //
    QColor color = Utils::creatorColor(
    // OPENMV-DIFF //
                m_lightColored ? Utils::Theme::FancyToolBarSeparatorColor
                               : Utils::Theme::SplitterColor);
    // OPENMV-DIFF //
    if (parent()->property("NoDrawToolBarBorders").toBool()) color = Utils::creatorColor(Utils::Theme::BackgroundColorDark);
    // OPENMV-DIFF //
    // OPENMV-DIFF //
    // painter.fillRect(event->rect(), color);
    // OPENMV-DIFF //
    // Paint ONLY the 1px center line; the rest of the (wider) handle stays unpainted and shows
    // the parent's background -- the maskless replacement for the old contentsRect mask.
    painter.fillRect(contentsRect(), color);
    // OPENMV-DIFF //
}

/*!
    \class Core::MiniSplitter
    \inheaderfile coreplugin/minisplitter.h
    \inmodule QtCreator

    \brief The MiniSplitter class is a simple helper-class to obtain
    \macos style 1-pixel wide splitters.
*/

/*!
    \enum Core::MiniSplitter::SplitterStyle
    This enum value holds the splitter style.

    \value Dark  Dark style.
    \value Light Light style.
*/

QSplitterHandle *MiniSplitter::createHandle()
{
    return new MiniSplitterHandle(orientation(), this, m_style == Light);
}

MiniSplitter::MiniSplitter(QWidget *parent, SplitterStyle style)
    : QSplitter(parent),
      m_style(style)
{
    // OPENMV-DIFF //
    // setHandleWidth(1);
    // OPENMV-DIFF //
    // 5px handle, drawn as a 1px line via the handle's contents-margins/mask trick: 5x easier
    // to hit, and gives the hover halo pixels to glow in.
    setHandleWidth(5);
    // OPENMV-DIFF //
    setChildrenCollapsible(false);
    setProperty(Utils::StyleHelper::C_MINI_SPLITTER, true);
}

MiniSplitter::MiniSplitter(Qt::Orientation orientation, QWidget *parent, SplitterStyle style)
    : QSplitter(orientation, parent),
      m_style(style)
{
    // OPENMV-DIFF //
    // setHandleWidth(1);
    // OPENMV-DIFF //
    setHandleWidth(5);  // see above
    // OPENMV-DIFF //
    setChildrenCollapsible(false);
    setProperty(Utils::StyleHelper::C_MINI_SPLITTER, true);
}

/*!
    \class Core::NonResizingSplitter
    \inheaderfile coreplugin/minisplitter.h
    \inmodule QtCreator

    \brief The NonResizingSplitter class is a MiniSplitter that keeps its
    first widget's size fixed when it is resized.
*/

/*!
    Constructs a non-resizing splitter with \a parent and \a style.

    The default style is MiniSplitter::Light.
*/
NonResizingSplitter::NonResizingSplitter(QWidget *parent, SplitterStyle style)
    : MiniSplitter(parent, style)
{
}

/*!
    \internal
*/
void NonResizingSplitter::resizeEvent(QResizeEvent *ev)
{
    // bypass QSplitter magic
    int leftSplitWidth = qMin(sizes().at(0), ev->size().width());
    int rightSplitWidth = qMax(0, ev->size().width() - leftSplitWidth);
    setSizes(QList<int>() << leftSplitWidth << rightSplitWidth);
    QWidget::resizeEvent(ev);
}
