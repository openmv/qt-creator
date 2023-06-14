// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "core_global.h"

#include <QStringList>

// OPENMV-DIFF //
#include "messageoutputwindow.h"
// OPENMV-DIFF //

QT_BEGIN_NAMESPACE
class QFont;
QT_END_NAMESPACE

namespace Core::MessageManager {

CORE_EXPORT void setFont(const QFont &font);
CORE_EXPORT void setWheelZoomEnabled(bool enabled);

CORE_EXPORT void writeSilently(const QString &message);
CORE_EXPORT void writeFlashing(const QString &message);
CORE_EXPORT void writeDisrupting(const QString &message);

CORE_EXPORT void writeSilently(const QStringList &messages);
CORE_EXPORT void writeFlashing(const QStringList &messages);
CORE_EXPORT void writeDisrupting(const QStringList &messages);

void init();
void destroy();

    static void writeSilently(const QString &message);
    static void writeFlashing(const QString &message);
    static void writeDisrupting(const QString &message);

    static void writeSilently(const QStringList &messages);
    static void writeFlashing(const QStringList &messages);
    static void writeDisrupting(const QStringList &messages);

    // OPENMV-DIFF //
    static OutputWindow *outputWindow();
public slots:
    static void printData(const QByteArray &data);
    static void grayOutOldContent();
    static void popup();
    static void flash();
    // OPENMV-DIFF //

private:
    MessageManager();
    ~MessageManager() override;

    static void init();
    friend class Core::Internal::MainWindow;
};

} // namespace Core::MessageManager
