/* Copyright (C) 2023-2025 OpenMV, LLC.
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

#include <QtCore>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/environment.h>
#include <utils/hostosinfo.h>
#include <utils/fancylineedit.h>
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>

#include "loaderdialog.h"
#include "openmvtr.h"

#define LAST_MPY_COMPILIER_OPTIMIZE_STATE "LastMPYCompilierOptimizeState"
#define LAST_MPY_COMPILIER_ADVANCED_STATE "LastMPYCompilierAdvancedState"
#define LAST_MPY_COMPILIER_OPTIONS_STRING "LastMPYCompilierOptionsString"

namespace OpenMV {
namespace Internal {

extern QByteArray loadFilter(const QByteArray &data, bool stripComments = true);

QString mpyCompile(const QString &script, const QJsonObject &mpySettings, Utils::QtcSettings *settings)
{
    QTemporaryDir tempDir;

    if(!tempDir.isValid())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Copy Script"),
            tempDir.errorString());

        return QString();
    }

    QDialog *dialog2 = new QDialog(Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog2->setWindowTitle(Tr::tr("Copy Script"));
    QFormLayout *layout2 = new QFormLayout(dialog2);
    layout2->setVerticalSpacing(0);

    layout2->addRow(new QLabel(Tr::tr("What do you want to do?")));
    layout2->addItem(new QSpacerItem(0, 6));

    QComboBox *combo2 = new QComboBox();
    combo2->addItem(Tr::tr("Just copy the script"));
    combo2->addItem(Tr::tr("Copy and clean whitespace"));
    combo2->addItem(Tr::tr("Copy and clean whitespace/comments"));
    combo2->addItem(Tr::tr("Compile to bytecode"));
    combo2->setCurrentIndex(settings->value(LAST_MPY_COMPILIER_OPTIMIZE_STATE, 0).toInt());
    layout2->addRow(combo2);
    layout2->addItem(new QSpacerItem(0, 6));

    QVBoxLayout *layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    QWidget *widget = new QWidget;
    widget->setLayout(layout);
    widget->setVisible(settings->value(LAST_MPY_COMPILIER_ADVANCED_STATE, false).toBool());

    Utils::FancyLineEdit *lineEdit = new Utils::FancyLineEdit();
    lineEdit->setHistoryCompleter(LAST_MPY_COMPILIER_OPTIONS_STRING, true);
    QLabel *label = new QLabel(Tr::tr("<a href=\"https://pypi.org/project/mpy-cross/\">MPY Cross Compilier CLI Options</a>"));
    label->setTextFormat(Qt::RichText);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    layout->addWidget(label);
    layout->addItem(new QSpacerItem(0, 6));
    layout->addWidget(lineEdit);
    layout->addItem(new QSpacerItem(0, 6));
    layout2->addRow(widget);

    QHBoxLayout *layout3 = new QHBoxLayout;
    layout3->setContentsMargins(0, 0, 0, 0);
    QWidget *widget2 = new QWidget;
    widget2->setLayout(layout3);

    QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Advanced"));
    checkBox2->setChecked(settings->value(LAST_MPY_COMPILIER_ADVANCED_STATE, false).toBool());
    checkBox2->setVisible(settings->value(LAST_MPY_COMPILIER_OPTIMIZE_STATE, 0).toInt() == 3);
    layout3->addWidget(checkBox2);

    QDialogButtonBox *box2 = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout3->addSpacing(160);
    layout3->addWidget(box2);
    layout2->addRow(widget2);

    QObject::connect(box2, &QDialogButtonBox::accepted, dialog2, &QDialog::accept);
    QObject::connect(box2, &QDialogButtonBox::rejected, dialog2, &QDialog::reject);
    QObject::connect(combo2, &QComboBox::currentIndexChanged, dialog2, [dialog2, checkBox2] (int index) {
        checkBox2->setVisible(index == 3);
        QTimer::singleShot(0, dialog2, [dialog2] { dialog2->adjustSize(); });
    });
    QObject::connect(checkBox2, &QCheckBox::toggled, dialog2, [dialog2, widget] (bool checked) {
        widget->setVisible(checked);
        QTimer::singleShot(0, dialog2, [dialog2] { dialog2->adjustSize(); });
    });

    bool ok = dialog2->exec() == QDialog::Accepted;

    if (!ok)
    {
        delete dialog2;
        return QString();
    }

    settings->setValue(LAST_MPY_COMPILIER_OPTIMIZE_STATE, combo2->currentIndex());
    settings->setValue(LAST_MPY_COMPILIER_ADVANCED_STATE, checkBox2->isChecked());
    settings->setValue(LAST_MPY_COMPILIER_OPTIONS_STRING, lineEdit->text());

    QStringList mpyArgs;

    if (combo2->currentIndex() == 0)
    {
        delete dialog2;
        return script;
    }
    else if (combo2->currentIndex() == 1 || combo2->currentIndex() == 2)
    {
        QFile file(script);

        if (!file.open(QIODevice::ReadOnly))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Copy Script"),
                file.errorString());

            delete dialog2;
            return QString();
        }

        QByteArray scriptData = loadFilter(file.readAll(), combo2->currentIndex() == 2);
        file.close();

        QFile outFile(tempDir.path() + QDir::separator() + QFileInfo(script).completeBaseName() + QStringLiteral(".mpy"));

        if((!outFile.open(QIODevice::WriteOnly)) || (outFile.write(scriptData) != scriptData.size()))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Copy Script"),
                outFile.errorString());

            delete dialog2;
            return QString();
        }

        outFile.close();

        delete dialog2;
        tempDir.setAutoRemove(false);
        return QFileInfo(outFile).filePath();
    }

    if (checkBox2->isChecked())
    {
        mpyArgs << lineEdit->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }

    delete dialog2;
    tempDir.setAutoRemove(false);

    QString command;
    Utils::Process process;
    LoaderDialog *dialog = new LoaderDialog(Tr::tr("MPY Cross Compilier"), Tr::tr("Compiling"), process, settings,
                                            QStringLiteral(LAST_LOADERDIALOG_TERMINAL_WINDOW_GEOMETRY),
                                            Core::ICore::dialogParent());
    dialog->disableTextWrapping();
    dialog->setOkayButtonVisible(true);

    QString stdOutBuffer = QString();
    QString *stdOutBufferPtr = &stdOutBuffer;

    QObject::connect(&process, &Utils::Process::textOnStandardOutput, dialog, [dialog, stdOutBufferPtr] (const QString &text) {
        stdOutBufferPtr->append(text);
        QStringList list = stdOutBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdOutBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if (out.trimmed().isEmpty())
            {
                continue;
            }

            dialog->appendPlainText(out);

            dialog->moveScrollToLeft();
            dialog->moveScrollToBottom();
        }
    });

    QString stdErrBuffer = QString();
    QString *stdErrBufferPtr = &stdErrBuffer;

    QObject::connect(&process, &Utils::Process::textOnStandardError, dialog, [dialog, stdErrBufferPtr] (const QString &text) {
        stdErrBufferPtr->append(text);
        QStringList list = stdErrBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdErrBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if (out.trimmed().isEmpty())
            {
                continue;
            }

            dialog->appendColoredText(out);

            dialog->moveScrollToLeft();
            dialog->moveScrollToBottom();
        }
    });

    for (const QString &arg : mpySettings.value(QStringLiteral("args")).toVariant().toStringList())
    {
        mpyArgs.append(arg.split(QLatin1Char(' '), Qt::SkipEmptyParts));
    }

    Utils::FilePath pythonPath, binary;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        pythonPath = Core::ICore::resourcePath(QStringLiteral("mpy-cross/windows"));
        binary = Core::ICore::resourcePath(QStringLiteral("python/win/python.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        pythonPath = Core::ICore::resourcePath(QStringLiteral("mpy-cross/mac"));
        binary = Core::ICore::resourcePath(QStringLiteral("python/mac/bin/python"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            pythonPath = Core::ICore::resourcePath(QStringLiteral("mpy-cross/linux-x86_64"));
            binary = Core::ICore::resourcePath(QStringLiteral("python/linux-x86_64/bin/python"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            pythonPath = Core::ICore::resourcePath(QStringLiteral("mpy-cross/linux-arm"));
            binary = Core::ICore::resourcePath(QStringLiteral("python/linux-arm/bin/python"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        {
            pythonPath = Core::ICore::resourcePath(QStringLiteral("mpy-cross/linux-aarch64"));
            binary = Core::ICore::resourcePath(QStringLiteral("python/linux-arm64/bin/python"));
        }
    }

    QString result, outputPath = tempDir.path() + QDir::separator() + QFileInfo(script).completeBaseName() + QStringLiteral(".mpy");

    QStringList args = QStringList() <<
                       QStringLiteral("-u") <<
                       QStringLiteral("-m") <<
                       QStringLiteral("mpy_cross") <<
                       mpyArgs <<
                       QStringLiteral("-o") <<
                       QDir::toNativeSeparators(QDir::cleanPath(outputPath)) <<
                       QDir::toNativeSeparators(QDir::cleanPath(script));

    if(pythonPath.isEmpty() || binary.isEmpty())
    {
        QMessageBox::warning(Core::ICore::dialogParent(),
            Tr::tr("MPY Cross Compilier"),
            Tr::tr("The MPY Cross Compilier is not supported on this platform."));

        delete dialog;
        return script;
    }

    command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
    dialog->appendColoredText(command);

    dialog->show();
    dialog->moveScrollToLeft();
    dialog->moveScrollToBottom();
    std::chrono::seconds timeout(300); // 5 minutes...
    process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process.setCommand(Utils::CommandLine(binary, args));
    Utils::Environment env = process.environment();
    env.appendOrSet("PYTHONPATH", pythonPath.path());
    process.setEnvironment(env);
    process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

    if (process.result() == Utils::ProcessResult::FinishedWithSuccess && QFileInfo(outputPath).exists())
    {
        dialog->appendColoredText(Tr::tr("Success - Press Ok to close the window"), true);
        dialog->enableOkayButton(true);
        result = outputPath;
    }
    else
    {
        dialog->appendColoredText(Tr::tr("Failure - Press Cancel to close the window"), true);
    }

    dialog->moveScrollToLeft();
    dialog->moveScrollToBottom();

    bool rejected = dialog->wasRejected();

    if (!rejected)
    {
        rejected = dialog->exec() == QDialog::Rejected;
    }

    delete dialog;
    return rejected ? QString() : result;
}

} // namespace Internal
} // namespace OpenMV
