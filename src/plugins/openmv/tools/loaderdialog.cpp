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

#include "loaderdialog.h"

#include <coreplugin/fancytabwidget.h>
#include <coreplugin/icore.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/hostosinfo.h>
#include <utils/theme/theme.h>

#include "openmvtr.h"

#include "mcp/omvmcpcontext.h"

namespace OpenMV {
namespace Internal {

static int s_loaderDialogActiveCount = 0;

// How much of a tool's console an MCP call keeps: a flash log can run to
// thousands of lines, and the failure paths already attach stdout/stderr.
static const int CONSOLE_HEAD_LINES = 50;
static const int CONSOLE_TAIL_LINES = 150;

bool loaderDialogActive()
{
    return s_loaderDialogActiveCount > 0;
}

LoaderDialog::LoaderDialog(const QString &title,
                           const QString &details,
                           Utils::Process &process,
                           Utils::QtcSettings *settings,
                           const QString &settingsName,
                           QWidget *parent) : QDialog(parent), m_settings(settings),
    m_settingsName(Utils::keyFromString(QStringLiteral(LOADERDIALOG_SETTINGS_GROUP "/") + settingsName)),
    m_title(title), m_lastPercent(-1)
{
    ++s_loaderDialogActiveCount;

    if(McpUi::Call *call = McpUi::currentCall()) call->note(QStringLiteral("[%1] %2").arg(title, details));

    setWindowFlags(windowFlags() | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)) |
                   (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    setAttribute(Qt::WA_ShowWithoutActivating);
    // The external process is pumped by a nested event loop that delivers
    // all events so this dialog's own controls (cancel/details/scroll) keep
    // working. Modality is what then stops that same pump from letting the
    // user drive the rest of the app and re-enter while the tool runs.
    setWindowModality(Qt::ApplicationModal);
    setWindowTitle(details + QStringLiteral(" - ") + title);

    QVBoxLayout *layout = new QVBoxLayout(this);

    m_warningLabel = new QLabel();
    m_warningLabel->setVisible(false);
    layout->addWidget(m_warningLabel);

    connect(&process, &Utils::Process::done, m_warningLabel, [this] {
        m_warningLabel->setVisible(false);
        m_warningLabel->setText(QString());
        if (!m_detailsButton->isChecked()) {
            QTimer::singleShot(0, this, [this] { resize(width(), minimumSizeHint().height()); });
        }
    });

    QHBoxLayout *progressLayout = new QHBoxLayout();

    m_progressBarLabel = new QLabel(Tr::tr("Busy"));
    progressLayout->addWidget(m_progressBarLabel);

    m_progressBar = new QProgressBar();
    m_progressBar->setRange(0, 0);
    progressLayout->addWidget(m_progressBar);

    m_detailsButton = new QPushButton();
    m_detailsButton->setText(Tr::tr("Show Details"));
    m_detailsButton->setCheckable(true);
    progressLayout->addWidget(m_detailsButton);

    layout->addLayout(progressLayout);

    m_plainTextEdit = new QPlainTextEdit();
    m_plainTextEdit->setReadOnly(true);
    m_plainTextEdit->setFont(TextEditor::TextEditorSettings::fontSettings().defaultFixedFontFamily());
    m_plainTextEdit->hide();

    QMainWindow *mainWindow = q_check_ptr(qobject_cast<QMainWindow *>(Core::ICore::mainWindow()));
    Core::Internal::FancyTabWidget *widget = qobject_cast<Core::Internal::FancyTabWidget *>(mainWindow->centralWidget());
    if(!widget) widget = qobject_cast<Core::Internal::FancyTabWidget *>(mainWindow->centralWidget()->layout()->itemAt(1)->widget()); // for tabbededitor
    widget = q_check_ptr(widget);
    m_plainTextEdit->setStyleSheet(widget->styleSheet());

    layout->addWidget(m_plainTextEdit);

    connect(m_detailsButton, &QToolButton::toggled, this, [this, layout] (bool checked) {
        if (!checked) m_maxHeight = height();
        m_detailsButton->setText(checked ? Tr::tr("Hide Details") : Tr::tr("Show Details"));
        m_plainTextEdit->setVisible(checked);
        setSizeGripEnabled(checked);
        if (!checked) {
            layout->addStretch(1);
            QTimer::singleShot(0, this, [this] { resize(width(), minimumSizeHint().height()); });
        } else {;
            layout->removeItem(layout->itemAt(layout->count() - 1)); // remove strech
            QTimer::singleShot(0, this, [this] { resize(width(), m_maxHeight); });
        }
    });

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);

    m_okayButton = box->addButton(QDialogButtonBox::Ok);
    m_okayButton->setVisible(false);
    m_okayButton->setEnabled(false);

    connect(box, &QDialogButtonBox::accepted, this, &LoaderDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &LoaderDialog::reject);

    layout->addWidget(box);

    layout->addStretch(1);

    m_wasRejected = false;
    QObject::connect(this, &LoaderDialog::rejected, [this, &process] {
        process.kill();
        m_wasRejected = true;
    });

    if(m_settings->contains(m_settingsName))
    {
        QByteArray in = m_settings->value(m_settingsName).toByteArray();
        QDataStream s(&in, QIODeviceBase::ReadOnly);
        QByteArray g; s >> g;
        bool c; s >> c;
        s >> m_maxHeight;

        restoreGeometry(g);
        m_detailsButton->setChecked(c); // if c is false then this does nothing since there was no toggle

        if (!c) QTimer::singleShot(0, this, [this] { resize(width(), minimumSizeHint().height()); });
    }
    else
    {
        m_detailsButton->setChecked(true);
        resize(640, minimumSizeHint().height());
        m_maxHeight = 480;
    }
}

LoaderDialog::~LoaderDialog()
{
    if (s_loaderDialogActiveCount > 0) --s_loaderDialogActiveCount;

    if(McpUi::Call *call = McpUi::currentCall())
    {
        // Hand the agent the tool's console, elided in the middle if long. The
        // window geometry is a human preference and is not saved from here.
        QStringList lines = m_console;

        if(lines.size() > (CONSOLE_HEAD_LINES + CONSOLE_TAIL_LINES))
        {
            const int dropped = lines.size() - CONSOLE_HEAD_LINES - CONSOLE_TAIL_LINES;
            lines = lines.mid(0, CONSOLE_HEAD_LINES)
                  << QStringLiteral("... (%1 lines elided) ...").arg(dropped)
                  << lines.mid(lines.size() - CONSOLE_TAIL_LINES);
        }

        call->note(QStringLiteral("[%1] console (%2 lines):").arg(m_title).arg(m_console.size()));
        for(const QString &line : std::as_const(lines)) call->note(QStringLiteral("  ") + line);
        return;
    }

    QByteArray out;
    QDataStream s(&out, QIODeviceBase::WriteOnly);
    s << saveGeometry();
    s << m_detailsButton->isChecked();
    s << (m_detailsButton->isChecked() ? height() : m_maxHeight);

    m_settings->setValue(m_settingsName, out);
}

void LoaderDialog::setProgressBarLabel(const QString &text)
{
    m_progressBarLabel->setText(text);
    if(McpUi::Call *call = McpUi::currentCall()) call->note(QStringLiteral("[%1] %2").arg(m_title, text));
}

void LoaderDialog::setProgressBarValue(int val)
{
    m_progressBar->setValue(val);

    if(McpUi::Call *call = McpUi::currentCall())
    {
        // Log the percentage in steps of ten so a slow flash is visible but
        // a fast one does not flood the call's log.
        const int range = m_progressBar->maximum() - m_progressBar->minimum();
        const int percent = (range > 0) ? (((val - m_progressBar->minimum()) * 100) / range) : -1;

        if((percent >= 0) && ((percent / 10) != (m_lastPercent / 10)))
        {
            m_lastPercent = percent;
            call->note(QStringLiteral("[%1] %2%").arg(m_title).arg(percent));
        }
    }
}

void LoaderDialog::appendPlainText(const QString &text)
{
    m_plainTextEdit->appendPlainText(text);
    if(McpUi::currentCall()) m_console.append(text);
}

void LoaderDialog::waitForClose()
{
    if(m_wasRejected) return;

    if(McpUi::currentCall())
    {
        if(m_okayButton->isEnabled()) accept(); else reject();
        return;
    }

    QEventLoop loop;
    connect(this, &QDialog::finished, &loop, &QEventLoop::quit);
    loop.exec();
}

void LoaderDialog::setVisible(bool visible)
{
    if(McpUi::currentCall()) visible = false;
    QDialog::setVisible(visible);
}

void LoaderDialog::appendColoredText(const QString &text, bool warning)
{
    if(McpUi::currentCall()) m_console.append(text);

    m_plainTextEdit->appendHtml(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                           arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface)
                               ? (warning ? QStringLiteral("lightseagreen") : QStringLiteral("lightblue"))
                               : (warning ? QStringLiteral("seagreen") : QStringLiteral("blue"))).
                           arg(text));

    if(warning)
    {
        m_warningLabel->setVisible(true);
        m_warningLabel->setText(QString(QStringLiteral("<p style=\"color:%1\">%2</p>")).
                                arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface)
                                    ? QStringLiteral("lightseagreen")
                                    : QStringLiteral("seagreen")).
                                arg(text));
    }
}

} // namespace Internal
} // namespace OpenMV
