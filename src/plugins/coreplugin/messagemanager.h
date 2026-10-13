// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"

#include <QStringList>

QT_BEGIN_NAMESPACE
class QFont;
QT_END_NAMESPACE

namespace Core {

// OPENMV-DIFF //
// Upstream 20.0.2 defines the General Messages pane class privately inside
// messagemanager.cpp; the old coreplugin/messageoutputwindow.h is gone, so
// forward-declare Core::OutputWindow instead of including it.
class OutputWindow;
// OPENMV-DIFF //

namespace MessageManager {

CORE_EXPORT void setFont(const QFont &font);
CORE_EXPORT void setWheelZoomEnabled(bool enabled);
CORE_EXPORT void popup();

CORE_EXPORT void clearLinesPrefixedWith(const QString &prefix, bool deleteTrailingLineBreak);

CORE_EXPORT void writeSilently(const QString &message);
CORE_EXPORT void writeFlashing(const QString &message);
CORE_EXPORT void writeDisrupting(const QString &message);

CORE_EXPORT void writeSilently(const QStringList &messages);
CORE_EXPORT void writeFlashing(const QStringList &messages);
CORE_EXPORT void writeDisrupting(const QStringList &messages);

// OPENMV-DIFF //
// init()/destroy() and popup() are dropped: upstream 20.0.2 manages the pane's
// lifetime via Utils::shutdownGuard() and now declares popup() above itself.
CORE_EXPORT OutputWindow *outputWindow();
CORE_EXPORT void printData(const QByteArray &data);
CORE_EXPORT void grayOutOldContent();
CORE_EXPORT void flash();
// OPENMV-DIFF //

} // namespace MessageManager
} // namespace Core
