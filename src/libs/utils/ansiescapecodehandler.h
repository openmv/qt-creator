// Copyright (C) 2016 Petar Perisin <petar.perisin@gmail.com>
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include <QTextCharFormat>

namespace Utils {

class QTCREATOR_UTILS_EXPORT FormattedText {
public:
    FormattedText() = default;
    FormattedText(const QString &txt, const QTextCharFormat &fmt = QTextCharFormat()) :
        text(txt), format(fmt)
    { }

    QString text;
    QTextCharFormat format;
};

class QTCREATOR_UTILS_EXPORT AnsiEscapeCodeHandler
{
public:
    QList<FormattedText> parseText(const FormattedText &input);
    void endFormatScope();
    // OPENMV-DIFF //
    QStringList getEscapeCodes()
    {
        if(m_escapeCodes.isEmpty()) return QStringList();
        return m_escapeCodes.takeFirst();
    }
    // OPENMV-DIFF //

private:
    void setFormatScope(const QTextCharFormat &charFormat);

    bool            m_previousFormatClosed = true;
    bool            m_waitingForTerminator = false;
    QString         m_alternateTerminator;
    QTextCharFormat m_previousFormat;
    QString         m_pendingText;
    // OPENMV-DIFF //
    QList<QStringList> m_escapeCodes;
    // OPENMV-DIFF //
};

} // namespace Utils
