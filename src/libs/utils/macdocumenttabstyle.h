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

    int styleHint(StyleHint hint, const QStyleOption *option = nullptr, const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override
    {
        if (hint == QStyle::SH_TabBar_Alignment)
            return Qt::AlignLeft;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
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
