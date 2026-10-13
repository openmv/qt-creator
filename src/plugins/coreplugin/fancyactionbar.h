// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QToolButton>

// OPENMV-DIFF //
#include "core_global.h"
// OPENMV-DIFF //

QT_BEGIN_NAMESPACE
class QVBoxLayout;
QT_END_NAMESPACE

namespace Core {
namespace Internal {

// OPENMV-DIFF //
// class FancyToolButton : public QToolButton
// OPENMV-DIFF //
class CORE_EXPORT FancyToolButton : public QToolButton
// OPENMV-DIFF //
{
    Q_OBJECT

    Q_PROPERTY(qreal fader READ fader WRITE setFader)

public:
    // OPENMV-DIFF //
    // FancyToolButton(QAction *action, QWidget *parent = nullptr);
    // OPENMV-DIFF //
    FancyToolButton(QAction *action, const QIcon &customIcon, QWidget *parent = nullptr);
    // OPENMV-DIFF //

    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *e) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    qreal fader() const { return m_fader; }
    void setFader(qreal value)
    {
        m_fader = value;
        update();
    }

    void setIconsOnly(bool iconsOnly);

    static void hoverOverlay(QPainter *painter, const QRect &spanRect);

private:
    void actionChanged();

    qreal m_fader = 0;
    bool m_iconsOnly = false;
    // OPENMV-DIFF //
    QIcon m_customIcon;
    // OPENMV-DIFF //
};

// OPENMV-DIFF //
// class FancyActionBar : public QWidget
// OPENMV-DIFF //
class CORE_EXPORT FancyActionBar : public QWidget
// OPENMV-DIFF //
{
    Q_OBJECT

public:
    FancyActionBar(QWidget *parent = nullptr);

    void paintEvent(QPaintEvent *event) override;
    // OPENMV-DIFF //
    // void insertAction(int index, QAction *action);
    // OPENMV-DIFF //
    void insertAction(int index, QAction *action, const QIcon &customIcon = QIcon());
    // OPENMV-DIFF //
    void addProjectSelector(QAction *action);
    QLayout *actionsLayout() const;
    QSize minimumSizeHint() const override;
    void setIconsOnly(bool iconsOnly);

private:
    QVBoxLayout *m_actionsLayout;
    bool m_iconsOnly = false;
};

} // namespace Internal
} // namespace Core
