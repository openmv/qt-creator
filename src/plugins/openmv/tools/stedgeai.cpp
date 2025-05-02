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

#include <QTextCodec>

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

#define LAST_STEDGEAI_COMPILIER_OPTIMIZE_STATE "LastStedgeaiCompilierOptimizeState"
#define LAST_STEDGEAI_COMPILIER_ADVANCED_STATE "LastStedgeaiCompilierAdvancedState"
#define LAST_STEDGEAI_COMPILIER_OPTIONS_STRING "LastStedgeaiCompilierOptionsString"
#define LAST_STEDGEAI_COMPILIER_OPTIONS_STRING_ATONN "LastStedgeaiCompilierOptionsStringAtonn"
#define LAST_STEDGEAI_COMPILIER_OPTIONS_STRING_RELOC "LastStedgeaiCompilierOptionsStringReloc"

namespace OpenMV {
namespace Internal {

QString stedgeaiCompile(const QString &model, const QJsonObject &stedgeaiSettings, Utils::QtcSettings *settings)
{
    QTemporaryDir tempDir;

    if(!tempDir.isValid())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("STEdgeAI Compilier"),
            tempDir.errorString());

        return QString();
    }

    QFile jsonFile(QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::allUsersResourcePath().
                                                            pathAppended(QStringLiteral("firmware")).
                                                            pathAppended(stedgeaiSettings.value(QStringLiteral("jsonFilePath")).toString()).toString())));

    if (!jsonFile.copy(tempDir.path() + QDir::separator() + QFileInfo(jsonFile).fileName()))
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("STEdgeAI Compilier"),
            Tr::tr("Failed to copy JSON file!"));

        return QString();
    }

    QFile mpoolFile(QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::allUsersResourcePath().
                                                             pathAppended(QStringLiteral("firmware")).
                                                             pathAppended(stedgeaiSettings.value(QStringLiteral("mpoolFilePath")).toString()).toString())));

    if (!mpoolFile.copy(tempDir.path() + QDir::separator() + QFileInfo(mpoolFile).fileName()))
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("STEdgeAI Compilier"),
            Tr::tr("Failed to copy MPOOL file!"));

        return QString();
    }

    QDialog *dialog2 = new QDialog(Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog2->setWindowTitle(Tr::tr("STEdgeAI Compilier"));
    QFormLayout *layout2 = new QFormLayout(dialog2);
    layout2->setVerticalSpacing(0);

    layout2->addRow(new QLabel(Tr::tr("Please specify the compiler settings:")));
    layout2->addItem(new QSpacerItem(0, 6));

    QComboBox *combo2 = new QComboBox();
    combo2->addItem(Tr::tr("Maximum Optimization"));
    combo2->addItem(Tr::tr("Medium Optimization"));
    combo2->addItem(Tr::tr("Low Optimization"));
    combo2->addItem(Tr::tr("No Optimization"));
    combo2->setCurrentIndex(settings->value(LAST_STEDGEAI_COMPILIER_OPTIMIZE_STATE, 0).toInt());
    layout2->addWidget(combo2);
    layout2->addItem(new QSpacerItem(0, 6));

    QVBoxLayout *layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    QWidget *widget = new QWidget;
    widget->setLayout(layout);
    widget->setVisible(settings->value(LAST_STEDGEAI_COMPILIER_ADVANCED_STATE, false).toBool());

    Utils::FancyLineEdit *lineEdit = new Utils::FancyLineEdit();
    lineEdit->setPlaceholderText(Tr::tr("--verbosity 2"));
    lineEdit->setHistoryCompleter(LAST_STEDGEAI_COMPILIER_OPTIONS_STRING, true);
    QLabel *label = new QLabel(Tr::tr("<a href=\"%1\">STEdgeAI Core CLI Options</a>").
        arg(Core::ICore::resourcePath(QStringLiteral("stedgeai/Documentation/command_line_interface.html")).toString()));
    label->setTextFormat(Qt::RichText);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    layout->addWidget(label);
    layout->addItem(new QSpacerItem(0, 6));
    layout->addWidget(lineEdit);
    layout->addItem(new QSpacerItem(0, 6));
    Utils::FancyLineEdit *lineEdit2 = new Utils::FancyLineEdit();
    lineEdit2->setPlaceholderText(Tr::tr("--mapping-recap"));
    lineEdit2->setHistoryCompleter(LAST_STEDGEAI_COMPILIER_OPTIONS_STRING_ATONN, true);
    QLabel *label2 = new QLabel(Tr::tr("<a href=\"%1\">STEdgeAI Neural-ART CLI Options</a>").
        arg(Core::ICore::resourcePath(QStringLiteral("stedgeai/Documentation/stneuralart_neural_art_compiler.html")).toString()));
    label2->setTextFormat(Qt::RichText);
    label2->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label2->setOpenExternalLinks(true);
    layout->addWidget(label2);
    layout->addItem(new QSpacerItem(0, 6));
    layout->addWidget(lineEdit2);
    layout->addItem(new QSpacerItem(0, 6));
    Utils::FancyLineEdit *lineEdit3 = new Utils::FancyLineEdit();
    lineEdit3->setPlaceholderText(Tr::tr("--verbosity 2"));
    lineEdit3->setHistoryCompleter(LAST_STEDGEAI_COMPILIER_OPTIONS_STRING_RELOC, true);
    QLabel *label3 = new QLabel(Tr::tr("<a href=\"%1\">STEdgeAI Relocation CLI Options</a>").
        arg(Core::ICore::resourcePath(QStringLiteral("stedgeai/scripts/N6_reloc/README.md")).toString()));
    label3->setTextFormat(Qt::RichText);
    label3->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label3->setOpenExternalLinks(true);
    layout->addWidget(label3);
    layout->addItem(new QSpacerItem(0, 6));
    layout->addWidget(lineEdit3);
    layout->addItem(new QSpacerItem(0, 6));
    layout2->addRow(widget);

    QHBoxLayout *layout3 = new QHBoxLayout;
    layout3->setContentsMargins(0, 0, 0, 0);
    QWidget *widget2 = new QWidget;
    widget2->setLayout(layout3);

    QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Advanced"));
    checkBox2->setChecked(settings->value(LAST_STEDGEAI_COMPILIER_ADVANCED_STATE, false).toBool());
    layout3->addWidget(checkBox2);

    QDialogButtonBox *box2 = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout3->addSpacing(160);
    layout3->addWidget(box2);
    layout2->addRow(widget2);

    QObject::connect(box2, &QDialogButtonBox::accepted, dialog2, &QDialog::accept);
    QObject::connect(box2, &QDialogButtonBox::rejected, dialog2, &QDialog::reject);
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

    settings->setValue(LAST_STEDGEAI_COMPILIER_OPTIMIZE_STATE, combo2->currentIndex());
    settings->setValue(LAST_STEDGEAI_COMPILIER_ADVANCED_STATE, checkBox2->isChecked());
    settings->setValue(LAST_STEDGEAI_COMPILIER_OPTIONS_STRING, lineEdit->text());
    settings->setValue(LAST_STEDGEAI_COMPILIER_OPTIONS_STRING_ATONN, lineEdit2->text());
    settings->setValue(LAST_STEDGEAI_COMPILIER_OPTIONS_STRING_RELOC, lineEdit3->text());

    QStringList stedgeaiArgs, stedgeaiAtonnArgs, relocArgs;

    if (combo2->currentIndex() == 0)
    {
        stedgeaiAtonnArgs << QString(QStringLiteral("--optimization 3")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }
    else if (combo2->currentIndex() == 1)
    {
        stedgeaiAtonnArgs << QString(QStringLiteral("--optimization 2")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }
    else if (combo2->currentIndex() == 2)
    {
        stedgeaiAtonnArgs << QString(QStringLiteral("--optimization 1")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }
    else
    {
        stedgeaiAtonnArgs << QString(QStringLiteral("--optimization 0")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }

    if (checkBox2->isChecked())
    {
        stedgeaiArgs << lineEdit->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        stedgeaiAtonnArgs << lineEdit2->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        relocArgs << lineEdit3->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }

    QFile jsonFile2(tempDir.path() + QDir::separator() + QFileInfo(jsonFile).fileName());

    if (!jsonFile2.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("STEdgeAI Compilier"),
            Tr::tr("Failed to open JSON file!"));

        delete dialog2;
        return QString();
    }

    QString jsonFileContent = QString::fromUtf8(jsonFile2.readAll()).
        replace(QStringLiteral("%"), stedgeaiAtonnArgs.join(QLatin1Char(' ')));
    jsonFile2.close();

    if (!jsonFile2.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("STEdgeAI Compilier"),
            Tr::tr("Failed to open JSON file!"));

        delete dialog2;
        return QString();
    }

    jsonFile2.write(jsonFileContent.toUtf8());
    jsonFile2.close();

    delete dialog2;
    tempDir.setAutoRemove(false);

    QString command;
    Utils::Process process;
    LoaderDialog *dialog = new LoaderDialog(Tr::tr("STEdgeAI"), Tr::tr("Compiling"), process, settings,
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

            if(out.startsWith(QStringLiteral("PASS:")))
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

            if(out.startsWith(QStringLiteral("PASS:")))
            {
                continue;
            }

            dialog->appendColoredText(out);

            dialog->moveScrollToLeft();
            dialog->moveScrollToBottom();
        }
    });

    for (const QString &arg : stedgeaiSettings.value(QStringLiteral("args")).toVariant().toStringList())
    {
        stedgeaiArgs.append(arg.split(QLatin1Char(' '), Qt::SkipEmptyParts));
    }

    Utils::FilePath stedgeai_core_dir, binary, python, gccPath;
    Utils::Environment env = process.environment();

    if(Utils::HostOsInfo::isWindowsHost())
    {
        stedgeai_core_dir = Core::ICore::resourcePath(QStringLiteral("stedgeai/Utilities/windows"));
        binary = stedgeai_core_dir.pathAppended(QStringLiteral("stedgeai.exe"));
        python = stedgeai_core_dir.pathAppended(QStringLiteral("python.exe"));
        gccPath = stedgeai_core_dir.pathAppended(QStringLiteral("mingw64/bin"));
        env.appendOrSet("USERPROFILE", Utils::Environment::systemEnvironment().value(QStringLiteral("USERPROFILE")));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        stedgeai_core_dir = Core::ICore::resourcePath(QStringLiteral("stedgeai/Utilities/mac"));
        binary = stedgeai_core_dir.pathAppended(QStringLiteral("stedgeai"));
        python = stedgeai_core_dir.pathAppended(QStringLiteral("python"));
        env.appendOrSet("HOME", Utils::Environment::systemEnvironment().value(QStringLiteral("HOME")));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            stedgeai_core_dir = Core::ICore::resourcePath(QStringLiteral("stedgeai/Utilities/linux"));
            binary = stedgeai_core_dir.pathAppended(QStringLiteral("stedgeai"));
            python = stedgeai_core_dir.pathAppended(QStringLiteral("python"));
            env.appendOrSet("HOME", Utils::Environment::systemEnvironment().value(QStringLiteral("HOME")));
        }
    }

    QStringList args = QStringList() <<
                       QStringLiteral("generate") <<
                       QStringLiteral("--model") <<
                       QDir::toNativeSeparators(QDir::cleanPath(model)) <<
                       QStringLiteral("--relocatable") <<
                       stedgeaiArgs;

    if(stedgeai_core_dir.isEmpty() || binary.isEmpty() || python.isEmpty() || gccPath.isEmpty())
    {
        QMessageBox::warning(Core::ICore::dialogParent(),
            Tr::tr("STEdgeAI Compilier"),
            Tr::tr("The STEdgeAI Compilier is not supported on this platform."));

        delete dialog;
        return model;
    }

    command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
    dialog->appendColoredText(command);

    dialog->show();
    dialog->moveScrollToLeft();
    dialog->moveScrollToBottom();
    std::chrono::seconds timeout(300); // 5 minutes...
    process.setStdOutCodec(QTextCodec::codecForName("UTF-8"));
    process.setStdErrCodec(QTextCodec::codecForName("UTF-8"));
    process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process.setCommand(Utils::CommandLine(binary, args));
    env.prependOrSet("PYTHONIOENCODING", QStringLiteral("utf-8"));
    env.prependOrSet("PATH", stedgeai_core_dir.path());
    env.prependOrSet("PATH", gccPath.path());
    env.prependOrSet("PATH", Core::ICore::resourcePath(QStringLiteral("arm/bin")).toString());
    env.prependOrSet("STEDGEAI_CORE_DIR", Core::ICore::resourcePath(QStringLiteral("stedgeai")).toString());
    process.setEnvironment(env);
    process.setWorkingDirectory(Utils::FilePath::fromString(tempDir.path()));
    process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

    if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
    {
        args = QStringList() <<
               QStringLiteral("-u") <<
               Core::ICore::resourcePath(QStringLiteral("stedgeai")).pathAppended(QStringLiteral("scripts/N6_reloc/npu_driver.py")).toString() <<
               relocArgs;

        command = QString(QStringLiteral("%1 %2")).arg(python.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        process.setCommand(Utils::CommandLine(python, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);
    }

    QString result, outputPath = tempDir.path() + QDir::separator() + QStringLiteral("build/network_rel.bin");

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
