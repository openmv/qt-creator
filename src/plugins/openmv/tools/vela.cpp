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
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>

#include "loaderdialog.h"
#include "openmvtr.h"

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

    tempDir.setAutoRemove(false);

    QString command;
    Utils::Process process;
    LoaderDialog *dialog = new LoaderDialog(Tr::tr("Vela"), Tr::tr("Compiling"), process, settings, QStringLiteral(LAST_LOADERDIALOG_TERMINAL_WINDOW_GEOMETRY),
                                            Core::ICore::dialogParent());
    dialog->disableTextWrapping();
    dialog->setOkayButtonVisible(true);
    dialog->enableOkayButton(true);

    QString stdOutBuffer = QString();
    QString *stdOutBufferPtr = &stdOutBuffer;
    bool stdOutFirstTime = true;
    bool *stdOutFirstTimePtr = &stdOutFirstTime;

    QObject::connect(&process, &Utils::Process::textOnStandardOutput, dialog, [dialog, stdOutBufferPtr, stdOutFirstTimePtr] (const QString &text) {
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
        }
    });

    QString stdErrBuffer = QString();
    QString *stdErrBufferPtr = &stdErrBuffer;
    bool stdErrFirstTime = true;
    bool *stdErrFirstTimePtr = &stdErrFirstTime;

    QObject::connect(&process, &Utils::Process::textOnStandardError, dialog, [dialog, stdErrBufferPtr, stdErrFirstTimePtr] (const QString &text) {
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

    QStringList velaArgs;

    for (const QString &arg : velaSettings.value(QStringLiteral("args")).toVariant().toStringList())
    {
        velaArgs.append(arg.split(QLatin1Char(' '), Qt::SkipEmptyParts));
    }

    Utils::FilePath binaryPath, binary;
    QStringList args = QStringList() <<
                       velaArgs <<
                       QStringLiteral("--config") <<
                       QDir::toNativeSeparators(QDir::cleanPath(Core::ICore::userResourcePath().
                            pathAppended(QStringLiteral("firmware")).
                            pathAppended(velaSettings.value(QStringLiteral("iniFilePath")).toString()).toString())) <<
                       QStringLiteral("--verbose-performance") <<
                       QStringLiteral("--verbose-cycle-estimate") <<
                       QStringLiteral("--output-dir") <<
                       QDir::toNativeSeparators(QDir::cleanPath(tempDir.path())) <<
                       QDir::toNativeSeparators(QDir::cleanPath(model));

    if(Utils::HostOsInfo::isWindowsHost())
    {
        binaryPath = Core::ICore::resourcePath(QStringLiteral("vela/windows"));
        binary = binaryPath.pathAppended(QStringLiteral("bin/vela.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        binaryPath = Core::ICore::resourcePath(QStringLiteral("vela/osx"));
        binary = binaryPath.pathAppended(QStringLiteral("bin/vela"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            binaryPath = Core::ICore::resourcePath(QStringLiteral("vela/linux32"));
            binary = binaryPath.pathAppended(QStringLiteral("bin/vela"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            binaryPath = Core::ICore::resourcePath(QStringLiteral("vela/linux64"));
            binary = binaryPath.pathAppended(QStringLiteral("bin/vela"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            binaryPath = Core::ICore::resourcePath(QStringLiteral("vela/arm"));
            binary = binaryPath.pathAppended(QStringLiteral("bin/vela"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        {
            binaryPath = Core::ICore::resourcePath(QStringLiteral("vela/aarch64"));
            binary = binaryPath.pathAppended(QStringLiteral("bin/vela"));
        }
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
    env.appendOrSet("PYTHONPATH", binaryPath.path());
    process.setEnvironment(env);
    process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);
    dialog->appendColoredText(Tr::tr("Finished - Press Ok to close the window"), true);
    dialog->moveScrollToLeft();
    dialog->moveScrollToBottom();

    dialog->exec();
    delete dialog;

    return tempDir.path() + QDir::separator() + QFileInfo(model).baseName() + QStringLiteral("_vela.tflite");
}

} // namespace Internal
} // namespace OpenMV
