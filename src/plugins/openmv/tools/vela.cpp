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

#define LAST_VELA_COMPILIER_OPTIMIZE_STATE "LastVelaCompilierOptimizeState"
#define LAST_VELA_COMPILIER_ADVANCED_STATE "LastVelaCompilierAdvancedState"
#define LAST_VELA_COMPILIER_OPTIONS_STRING "LastVelaCompilierOptionsString"

namespace OpenMV {
namespace Internal {

QString velaCompile(const QString &model, const QJsonObject &velaSettings, Utils::QtcSettings *settings)
{
    QTemporaryDir tempDir;

    if(!tempDir.isValid())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Vela Compilier"),
            tempDir.errorString());

        return QString();
    }

    QDialog *dialog2 = new QDialog(Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog2->setWindowTitle(Tr::tr("Vela Compilier"));
    QFormLayout *layout2 = new QFormLayout(dialog2);
    layout2->setVerticalSpacing(0);

    layout2->addRow(new QLabel(Tr::tr("Please specify the compiler settings:")));
    layout2->addItem(new QSpacerItem(0, 6));

    QComboBox *combo2 = new QComboBox();
    combo2->addItem(Tr::tr("Optimize for Performance"));
    combo2->addItem(Tr::tr("Optimize for Size"));
    combo2->setCurrentIndex(settings->value(LAST_VELA_COMPILIER_OPTIMIZE_STATE, 0).toInt());
    layout2->addRow(combo2);
    layout2->addItem(new QSpacerItem(0, 6));

    QVBoxLayout *layout = new QVBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    QWidget *widget = new QWidget;
    widget->setLayout(layout);
    widget->setVisible(settings->value(LAST_VELA_COMPILIER_ADVANCED_STATE, false).toBool());

    Utils::FancyLineEdit *lineEdit = new Utils::FancyLineEdit();
    lineEdit->setPlaceholderText(Tr::tr("--verbose-progress"));
    lineEdit->setHistoryCompleter(LAST_VELA_COMPILIER_OPTIONS_STRING, true);
    QLabel *label = new QLabel(Tr::tr("<a href=\"https://gitlab.arm.com/artificial-intelligence/ethos-u/ethos-u-vela/-/blob/main/OPTIONS.md\">Vela Compilier CLI Options</a>"));
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
    checkBox2->setChecked(settings->value(LAST_VELA_COMPILIER_ADVANCED_STATE, false).toBool());
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

    settings->setValue(LAST_VELA_COMPILIER_OPTIMIZE_STATE, combo2->currentIndex());
    settings->setValue(LAST_VELA_COMPILIER_ADVANCED_STATE, checkBox2->isChecked());
    settings->setValue(LAST_VELA_COMPILIER_OPTIONS_STRING, lineEdit->text());

    QStringList velaArgs;

    if (combo2->currentIndex() == 0)
    {
        velaArgs << QString(QStringLiteral("--optimise Performance")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }
    else
    {
        velaArgs << QString(QStringLiteral("--optimise Size")).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }

    if (checkBox2->isChecked())
    {
        velaArgs << lineEdit->text().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    }

    delete dialog2;
    tempDir.setAutoRemove(false);

    bool finishedOk = false;
    bool *finishedOkPtr = &finishedOk;

    QString command;
    Utils::Process process;
    LoaderDialog *dialog = new LoaderDialog(Tr::tr("Vela"), Tr::tr("Compiling"), process, settings,
                                            QStringLiteral(LAST_LOADERDIALOG_TERMINAL_WINDOW_GEOMETRY),
                                            Core::ICore::dialogParent());
    dialog->disableTextWrapping();
    dialog->setOkayButtonVisible(true);

    QString stdOutBuffer = QString();
    QString *stdOutBufferPtr = &stdOutBuffer;

    QRegularExpression ramRegex(QStringLiteral("Total SRAM used\\s+([\\d.]+)\\s+(TiB|GiB|MiB|KiB|B)"));
    int ramSize = 0, *ramSizePtr = &ramSize;
    QString ramString = QString(), *ramStringPtr = &ramString;

    QObject::connect(&process, &Utils::Process::textOnStandardOutput, dialog,
                     [dialog, stdOutBufferPtr, velaSettings, ramRegex, finishedOkPtr, ramSizePtr, ramStringPtr]
                     (const QString &text) {
        stdOutBufferPtr->append(text);
        QStringList list = stdOutBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdOutBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();
            dialog->appendPlainText(out);
            dialog->moveScrollToLeft();
            dialog->moveScrollToBottom();

            if (out.contains(QStringLiteral("Batch Inference time")))
            {
                int heapSize = velaSettings.value(QStringLiteral("heapSize")).toInt();
                float heapUsed = ((float)(*ramSizePtr) / (float)(heapSize)) * 100;

                if (heapUsed > 100.0f)
                {
                    *ramStringPtr = QString(QStringLiteral("ERROR: Total Heap Required: %1%!!!")).arg(heapUsed, 0, 'f', 2);
                }
                else if (heapUsed > 90.f)
                {
                    *ramStringPtr = QString(QStringLiteral("WARNING: Total Heap Required: %1%")).arg(heapUsed, 0, 'f', 2);
                    *finishedOkPtr = true;
                }
                else
                {
                    *ramStringPtr = QString(QStringLiteral("Total Heap Required: %1%")).arg(heapUsed, 0, 'f', 2);
                    *finishedOkPtr = true;
                }
            }

            QRegularExpressionMatch match = ramRegex.match(out);

            if (match.hasMatch())
            {
                int ram = qRound(match.captured(1).toFloat());
                QString unit = match.captured(2);

                if (unit == QStringLiteral("TiB"))
                {
                    *ramSizePtr = ram * 1024 * 1024 * 1024 * 1024;
                }
                else if (unit == QStringLiteral("GiB"))
                {
                    *ramSizePtr = ram * 1024 * 1024 * 1024;
                }
                else if (unit == QStringLiteral("MiB"))
                {
                    *ramSizePtr = ram * 1024 * 1024;
                }
                else if (unit == QStringLiteral("KiB"))
                {
                    *ramSizePtr = ram * 1024;
                }
                else
                {
                    *ramSizePtr = ram;
                }
            }
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
            dialog->appendColoredText(out);
            dialog->moveScrollToLeft();
            dialog->moveScrollToBottom();
        }
    });

    for (const QString &arg : velaSettings.value(QStringLiteral("args")).toVariant().toStringList())
    {
        velaArgs.append(arg.split(QLatin1Char(' '), Qt::SkipEmptyParts));
    }

    Utils::FilePath pythonPath, binary;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        pythonPath = Core::ICore::resourcePath(QStringLiteral("vela/windows"));
        binary = Core::ICore::resourcePath(QStringLiteral("python/win/python.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        // x86 macs are not supported for this tool.
        if (Utils::HostOsInfo::hostArchitecture() == Utils::OsArchArm64)
        {
            pythonPath = Core::ICore::resourcePath(QStringLiteral("vela/mac-aarch64"));
            binary = Core::ICore::resourcePath(QStringLiteral("python/mac-aarch64/bin/python"));
        }
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            pythonPath = Core::ICore::resourcePath(QStringLiteral("vela/linux64"));
            binary = Core::ICore::resourcePath(QStringLiteral("python/linux-x86_64/bin/python"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        {
            pythonPath = Core::ICore::resourcePath(QStringLiteral("vela/aarch64"));
            binary = Core::ICore::resourcePath(QStringLiteral("python/linux-arm64/bin/python"));
        }
    }

    QStringList args = QStringList() <<
                       QStringLiteral("-u") <<
                       QStringLiteral("-m") <<
                       QStringLiteral("ethosu.vela") <<
                       velaArgs <<
                       QStringLiteral("--config") <<
                       QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::allUsersResourcePath().
                            pathAppended(QStringLiteral("firmware")).
                            pathAppended(velaSettings.value(QStringLiteral("iniFilePath")).toString()).toString())) <<
                       QStringLiteral("--verbose-performance") <<
                       QStringLiteral("--verbose-cycle-estimate") <<
                       QStringLiteral("--output-dir") <<
                       QDir::toNativeSeparators(QDir::cleanPath(tempDir.path())) <<
                       QDir::toNativeSeparators(QDir::cleanPath(model));

    if(pythonPath.isEmpty() || binary.isEmpty())
    {
        QMessageBox::warning(Core::ICore::dialogParent(),
            Tr::tr("Vela Compilier"),
            Tr::tr("The Vela Compilier is not supported on this platform."));

        delete dialog;
        return model;
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

    QString result, outputPath = tempDir.path() + QDir::separator() + QFileInfo(model).completeBaseName() + QStringLiteral("_vela.tflite");

    dialog->appendColoredText(*ramStringPtr);

    if (finishedOk && QFileInfo(outputPath).exists())
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
