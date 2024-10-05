// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/infobar.h>

#include <QIcon>
#include <QWidget>

#include <QPropertyAnimation>

// OPENMV-DIFF //
#include "core_global.h"
#include "minisplitter.h"
#include <QToolButton>
// OPENMV-DIFF //

QT_BEGIN_NAMESPACE
class QPainter;
class QStackedLayout;
class QStatusBar;
QT_END_NAMESPACE

namespace Core {
namespace Internal {

// OPENMV-DIFF //
// class FancyTab : public QObject
// OPENMV-DIFF //
class CORE_EXPORT FancyTab : public QObject
// OPENMV-DIFF //
{
    Q_OBJECT

    Q_PROPERTY(qreal fader READ fader WRITE setFader)

public:
    FancyTab(QWidget *parentTabBar)
        : QObject(parentTabBar)
        , m_tabbar(parentTabBar)
    {
        m_animator.setPropertyName("fader");
        m_animator.setTargetObject(this);
    }

    qreal fader() const { return m_fader; }
    void setFader(qreal qreal);

    void fadeIn();
    void fadeOut();

    QIcon icon;
    QString text;
    QString toolTip;
    bool enabled = false;
    bool hasMenu = false;

private:
    QPropertyAnimation m_animator;
    QWidget *m_tabbar;
    qreal m_fader = 0;
};

// OPENMV-DIFF //
// class FancyTabBar : public QWidget
// OPENMV-DIFF //
class CORE_EXPORT FancyTabBar : public QWidget
// OPENMV-DIFF //
{
    Q_OBJECT

public:
    FancyTabBar(QWidget *parent = nullptr);

    bool event(QEvent *event) override;

    void paintEvent(QPaintEvent *event) override;
    void paintTab(QPainter *painter, int tabIndex) const;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    bool validIndex(int index) const { return index >= 0 && index < m_tabs.count(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    void setTabEnabled(int index, bool enable);
    bool isTabEnabled(int index) const;

    void insertTab(int index, const QIcon &icon, const QString &label, bool hasMenu)
    {
        auto tab = new FancyTab(this);
        tab->icon = icon;
        tab->text = label;
        tab->hasMenu = hasMenu;
        m_tabs.insert(index, tab);
        if (m_currentIndex >= index)
            ++m_currentIndex;
        updateGeometry();
    }
    void setEnabled(int index, bool enabled);
    void removeTab(int index)
    {
        FancyTab *tab = m_tabs.takeAt(index);
        delete tab;
        updateGeometry();
    }
    void setCurrentIndex(int index);
    int currentIndex() const { return m_currentIndex; }

    void setTabToolTip(int index, const QString &toolTip) { m_tabs[index]->toolTip = toolTip; }
    QString tabToolTip(int index) const { return m_tabs.at(index)->toolTip; }

    void setIconsOnly(bool iconOnly);

    int count() const { return m_tabs.count(); }
    QRect tabRect(int index) const;

signals:
    void currentAboutToChange(int index);
    void currentChanged(int index);
    void menuTriggered(int index, QMouseEvent *event);

private:
    QRect m_hoverRect;
    int m_hoverIndex = -1;
    int m_currentIndex = -1;
    bool m_iconsOnly = false;
    QList<FancyTab *> m_tabs;
    QSize tabSizeHint(bool minimum = false) const;
};

// OPENMV-DIFF //
// class FancyTabWidget : public QWidget
// OPENMV-DIFF //
class CORE_EXPORT FancyTabWidget : public QWidget
// OPENMV-DIFF //
{
    Q_OBJECT

public:
    FancyTabWidget(QWidget *parent = nullptr);

    void insertTab(int index, QWidget *tab, const QIcon &icon, const QString &label, bool hasMenu);
    void removeTab(int index);
    void setBackgroundBrush(const QBrush &brush);
    void addCornerWidget(QWidget *widget);
    void insertCornerWidget(int pos, QWidget *widget);
    int cornerWidgetCount() const;
    void setTabToolTip(int index, const QString &toolTip);

    void paintEvent(QPaintEvent *event) override;

    int currentIndex() const;
    QStatusBar *statusBar() const;
    Utils::InfoBar *infoBar();

    void setTabEnabled(int index, bool enable);
    bool isTabEnabled(int index) const;

    void setIconsOnly(bool iconsOnly);

    bool isSelectionWidgetVisible() const;

signals:
    void currentAboutToShow(int index);
    void currentChanged(int index);
    void menuTriggered(int index, QMouseEvent *event);
    void topAreaClicked(Qt::MouseButton button, Qt::KeyboardModifiers modifiers);

public slots:
    void setCurrentIndex(int index);
    void setSelectionWidgetVisible(bool visible);

private:
    void showWidget(int index);

    FancyTabBar *m_tabBar;
    QWidget *m_cornerWidgetContainer;
    QStackedLayout *m_modesStack;
    QWidget *m_selectionWidget;
    QStatusBar *m_statusBar;
    Utils::InfoBarDisplay m_infoBarDisplay;
    Utils::InfoBar m_infoBar;

// OPENMV-DIFF //
public:
    MiniSplitter *m_msplitter;
    MiniSplitter *m_hsplitter;
    MiniSplitter *m_vsplitter;
    QToolButton *m_topDrawer;
    QToolButton *m_bottomDrawer;
    QToolButton *m_leftDrawer;
    QToolButton *m_rightDrawer;
    QString m_styleSheet, m_highDPIStyleSheet;
    qreal m_devicePixelRatio;
// OPENMV-DIFF //
};

} // namespace Internal
} // namespace Core
