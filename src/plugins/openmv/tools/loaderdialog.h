/* Copyright (C) 2023-2024 OpenMV, LLC.
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

#ifndef LOADER_DIALOG_H
#define LOADER_DIALOG_H

#include <QtCore>
#include <QtWidgets>

#include <utils/qtcprocess.h>
#include <utils/qtcsettings.h>

#define LOADERDIALOG_SETTINGS_GROUP "OpenMVLoaderDialog"
#define LAST_LOADERDIALOG_TERMINAL_WINDOW_GEOMETRY "LastLoaderDialogTerminalWindowGeometry"

namespace OpenMV {
namespace Internal {

class LoaderDialog : public QDialog
{
    Q_OBJECT

public:

    explicit LoaderDialog(const QString &title,
                          const QString &details,
                          Utils::Process &process,
                          Utils::QtcSettings *settings,
                          const QString &settingsName,
                          QWidget *parent = Q_NULLPTR);
    ~LoaderDialog();

    void setProgressBarLabel(const QString &text) { m_progressBarLabel->setText(text); }
    void setProgressBarRange(int min, int max) { m_progressBar->setRange(min, max); }
    void setProgressBarValue(int val) { m_progressBar->setValue(val); }
    void appendPlainText(const QString &text) { m_plainTextEdit->appendPlainText(text); }
    void appendColoredText(const QString &text, bool warning = false);
    void enableTextWrapping() { m_plainTextEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth); }
    void disableTextWrapping() { m_plainTextEdit->setLineWrapMode(QPlainTextEdit::NoWrap); }
    void moveScrollToLeft() { m_plainTextEdit->horizontalScrollBar()->setValue(m_plainTextEdit->horizontalScrollBar()->minimum()); }
    void moveScrollToBottom() { m_plainTextEdit->verticalScrollBar()->setValue(m_plainTextEdit->verticalScrollBar()->maximum()); }
    QTextCursor textCursor() const { return m_plainTextEdit->textCursor(); }
    void setTextCursor(const QTextCursor &cursor) { m_plainTextEdit->setTextCursor(cursor); }
    void setOkayButtonVisible(bool enable) { m_okayButton->setVisible(enable); }
    void enableOkayButton(bool enable) { m_okayButton->setEnabled(enable); }

private:

    Utils::QtcSettings *m_settings;
    Utils::Key m_settingsName;
    QLabel *m_progressBarLabel;
    QProgressBar *m_progressBar;
    QPushButton *m_detailsButton;
    QLabel *m_warningLabel;
    QPlainTextEdit *m_plainTextEdit;
    QPushButton *m_okayButton;
    int m_maxHeight;
};

} // namespace Internal
} // namespace OpenMV

#endif // LOADER_DIALOG_H
