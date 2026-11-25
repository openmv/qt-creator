// Copyright (C) 2026 OpenMV, LLC.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

// OPENMV-DIFF //
// Header-only, macOS only: used by the AI chat tabs and the Shell pane's tabs.

#include <QtGlobal>

#ifdef Q_OS_MAC

#include "theme/theme.h"

#include <QIcon>
#include <QPainter>
#include <QProxyStyle>
#include <QStyleOption>
#include <QTabBar>
#include <QAbstractButton>
#include <QChildEvent>

namespace Utils {

// The macOS style lays a document-mode tab's label out symmetrically (room for
// a close button on both sides, text centred in what is left), so a long name
// clips at both ends even when the tab is wide enough. Lay it out left to
// right instead: icon, text, close button, like the editor tabs on the other
// platforms.
class MacDocumentTabStyle : public QProxyStyle
{
public:
    MacDocumentTabStyle() : QProxyStyle() {} // no base: the application style, not owned

    // Style a tab bar and the close buttons it creates for its tabs (a style
    // does not reach a widget's children by itself).
    static void applyTo(QTabBar *tabBar)
    {
        auto *style = new MacDocumentTabStyle;
        style->setParent(tabBar);
        tabBar->setStyle(style);
        tabBar->installEventFilter(style);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::ChildPolished) {
            auto *child = static_cast<QChildEvent *>(event)->child();
            if (auto *button = qobject_cast<QAbstractButton *>(child)) {
                if (button->style() != this)
                    button->setStyle(this);
            }
        }
        return QProxyStyle::eventFilter(watched, event);
    }

public:

    int styleHint(StyleHint hint, const QStyleOption *option = nullptr, const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override
    {
        if (hint == QStyle::SH_TabBar_Alignment)
            return Qt::AlignLeft;
        // Close buttons on the right, as on the other platforms (macOS: left).
        if (hint == QStyle::SH_TabBar_CloseButtonPosition)
            return QTabBar::RightSide;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override
    {
        // The editor and chat tabs' 20x20 close button.
        if (metric == PM_TabCloseIndicatorWidth || metric == PM_TabCloseIndicatorHeight)
            return 20;
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    // A tab bar's own close buttons drawn like the editor tabs': the same
    // icon, always shown, with the theme's hover tint.
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override
    {
        if (element != PE_IndicatorTabClose) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
            return;
        }
        if ((option->state & State_MouseOver) && (option->state & State_Enabled))
            painter->fillRect(option->rect, creatorColor(Theme::BackgroundColorHover));
        const bool dark = creatorTheme() && creatorTheme()->flag(Theme::DarkUserInterface);
        const QIcon icon(dark ? QLatin1String(":/icons/close_button_light_grey.png")
                              : QLatin1String(":/icons/close_button_dark.png"));
        icon.paint(painter, option->rect, Qt::AlignCenter);
    }

    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override
    {
        const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option);
        if (element == CE_TabBarTabShape && tab) {
            // The native shape ends in a 1px separator along the tab's right
            // edge, which the other platforms don't have; the tab bar draws the
            // current tab's own side lines itself.
            painter->save();
            painter->setClipRect(tab->rect.adjusted(0, 0, -1, 0), Qt::IntersectClip);
            QProxyStyle::drawControl(element, option, painter, widget);
            painter->restore();
            // The current tab's sides, so it stands apart from the tabs next to
            // it (what the AI chat tab bar draws for itself).
            if (tab->state & State_Selected) {
                painter->save();
                painter->setPen(creatorColor(Theme::SplitterColor));
                const QRect r = tab->rect;
                if (tab->position != QStyleOptionTab::Beginning
                    && tab->position != QStyleOptionTab::OnlyOneTab)
                    painter->drawLine(r.left(), r.top(), r.left(), r.bottom());
                painter->drawLine(r.right(), r.top(), r.right(), r.bottom());
                painter->restore();
            }
            return;
        }
        if (element != CE_TabBarTabLabel || !tab) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }
        const QRect r = tab->rect;
        int left = r.left() + 8;
        int right = r.right() - 8;
        if (tab->leftButtonSize.width() > 0)
            left += tab->leftButtonSize.width() + 4;
        if (tab->rightButtonSize.width() > 0)
            right -= tab->rightButtonSize.width() + 4;
        if (!tab->icon.isNull()) {
            const QSize iconSize = tab->iconSize.isValid() ? tab->iconSize : QSize(16, 16);
            const QRect iconRect(left, r.center().y() - iconSize.height() / 2, iconSize.width(), iconSize.height());
            tab->icon.paint(painter, iconRect, Qt::AlignCenter,
                            (tab->state & State_Enabled) ? QIcon::Normal : QIcon::Disabled,
                            (tab->state & State_Selected) ? QIcon::On : QIcon::Off);
            left += iconSize.width() + 4;
        }
        const QRect textRect(left, r.top(), qMax(0, right - left + 1), r.height());
        // The theme's text colour (what the editor tabs use), not the palette's
        // window text, which the native style does not use for tabs either.
        QPalette palette = tab->palette;
        palette.setColor(QPalette::WindowText, Utils::creatorColor(Utils::Theme::TextColorNormal));
        proxy()->drawItemText(painter, textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextShowMnemonic,
                              palette, tab->state & State_Enabled, tab->text, QPalette::WindowText);
    }
};

} // namespace Utils

#endif // Q_OS_MAC
// OPENMV-DIFF //
