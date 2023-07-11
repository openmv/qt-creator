#ifndef LOADER_DIALOG_H
#define LOADER_DIALOG_H

#include <QtCore>
#include <QtWidgets>

#include <utils/qtcprocess.h>

namespace OpenMV {
namespace Internal {

class LoaderDialog : public QDialog
{
    Q_OBJECT

public:

    explicit LoaderDialog(const QString &title,
                          const QString &details,
                          Utils::QtcProcess &process,
                          QSettings *settings,
                          const QString &settingsName,
                          QWidget *parent = Q_NULLPTR);
    ~LoaderDialog();

    void setProgressBarLabel(const QString &text) { m_progressBarLabel->setText(text); }
    void setProgressBarRange(int min, int max) { m_progressBar->setRange(min, max); }
    void setProgressBarValue(int val) { m_progressBar->setValue(val); }
    void appendPlainText(const QString &text) { m_plainTextEdit->appendPlainText(text); }
    void appendColoredText(const QString &text, bool warning = false);
    QTextCursor textCursor() const { return m_plainTextEdit->textCursor(); }
    void setTextCursor(const QTextCursor &cursor) { m_plainTextEdit->setTextCursor(cursor); }

private:

    QSettings *m_settings;
    QString m_settingsName;
    QLabel *m_progressBarLabel;
    QProgressBar *m_progressBar;
    QToolButton *m_detailsButton;
    QLabel *m_warningLabel;
    QPlainTextEdit *m_plainTextEdit;
    int m_maxHeight;
};

} // namespace Internal
} // namespace OpenMV

#endif // LOADER_DIALOG_H
