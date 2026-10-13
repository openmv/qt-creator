// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"

#include <QSplitter>

QT_BEGIN_NAMESPACE
class QSplitterHandle;
QT_END_NAMESPACE

namespace Core {

/*! This is a simple helper-class to obtain mac-style 1-pixel wide splitters */
class CORE_EXPORT MiniSplitter : public QSplitter
{
public:
    enum SplitterStyle {Dark, Light};

    MiniSplitter(QWidget *parent = nullptr, SplitterStyle style = Dark);
    MiniSplitter(Qt::Orientation orientation, QWidget *parent = nullptr, SplitterStyle style = Dark);

protected:
    QSplitterHandle *createHandle() override;

private:
    SplitterStyle m_style;
};

class CORE_EXPORT NonResizingSplitter : public MiniSplitter
{
public:
    explicit NonResizingSplitter(QWidget *parent, SplitterStyle style = Light);

protected:
    void resizeEvent(QResizeEvent *ev) override;
};

// OPENMV-DIFF //
// Couple two MiniSplitters' dividers: holding Ctrl (Cmd on macOS) while dragging either one then
// moves both to the same on-screen position along their (shared) axis -- used to line up e.g. the
// editor/serial-terminal and frame-buffer/histogram dividers. Wires each splitter's first handle as
// the other's sibling (both directions). Safe to call repeatedly; a no-op unless both splitters use
// MiniSplitterHandle (i.e. are MiniSplitters).
CORE_EXPORT void coupleSplitterDividers(QSplitter *first, QSplitter *second);
// OPENMV-DIFF //

} // namespace Core
