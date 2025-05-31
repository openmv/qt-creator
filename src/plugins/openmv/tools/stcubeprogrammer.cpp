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
#include <utils/ansiescapecodehandler.h>
#include <utils/environment.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>

#include "loaderdialog.h"
#include "openmvtr.h"

namespace OpenMV {
namespace Internal {

QMutex st_cube_programmer_working;

void stCubeProgrammerDownloadFirmware(const QString &details, QString &command, Utils::Process &process, const QString &path)
{
    QMutexLocker locker(&st_cube_programmer_working);

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(LOADERDIALOG_SETTINGS_GROUP);
    LoaderDialog *dialog = new LoaderDialog(Tr::tr("STM32 Programmer"), details, process, settings, QStringLiteral(LAST_LOADERDIALOG_TERMINAL_WINDOW_GEOMETRY),
                                            Core::ICore::dialogParent());

    QString stdOutBuffer = QString();
    QString *stdOutBufferPtr = &stdOutBuffer;
    bool stdOutFirstTime = true;
    bool *stdOutFirstTimePtr = &stdOutFirstTime;
    Utils::AnsiEscapeCodeHandler stdOutHandler;
    Utils::AnsiEscapeCodeHandler *stdOutHandlerPtr = &stdOutHandler;

    QObject::connect(&process, &Utils::Process::textOnStandardOutput, dialog, [dialog, stdOutBufferPtr, stdOutFirstTimePtr, stdOutHandlerPtr] (const QString &text) {
        stdOutBufferPtr->append(text);
        QStringList list = stdOutBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdOutBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QList<Utils::FormattedText> outList = stdOutHandlerPtr->parseText(Utils::FormattedText(list.takeFirst()));

            while(outList.size())
            {
                QString txt = outList.takeFirst().text;

                if(txt.startsWith(QStringLiteral("±")) || txt.startsWith(QStringLiteral("Û")))
                {
                    continue;
                }

                if(txt.startsWith(QStringLiteral("[")))
                {
                    QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\d+)%")).match(txt);

                    if(m.hasMatch())
                    {
                        dialog->setProgressBarLabel(Tr::tr("Downloading..."));
                        int p = m.captured(1).toInt();
                        dialog->setProgressBarRange(0, 100);
                        dialog->setProgressBarValue(p);
                    }

                    if(!*stdOutFirstTimePtr)
                    {
                        QTextCursor cursor = dialog->textCursor();
                        cursor.movePosition(QTextCursor::End);
                        cursor.select(QTextCursor::BlockUnderCursor);
                        cursor.removeSelectedText();
                        dialog->setTextCursor(cursor);
                    }

                    *stdOutFirstTimePtr = false;
                }
                dialog->appendPlainText(txt);
            }
        }
    });

    QString stdErrBuffer = QString();
    QString *stdErrBufferPtr = &stdErrBuffer;
    bool stdErrFirstTime = true;
    bool *stdErrFirstTimePtr = &stdErrFirstTime;
    Utils::AnsiEscapeCodeHandler stdErrHandler;
    Utils::AnsiEscapeCodeHandler *stdErrHandlerPtr = &stdErrHandler;

    QObject::connect(&process, &Utils::Process::textOnStandardError, dialog, [dialog, stdErrBufferPtr, stdErrFirstTimePtr, stdErrHandlerPtr] (const QString &text) {
        stdErrBufferPtr->append(text);
        QStringList list = stdErrBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdErrBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QList<Utils::FormattedText> outList = stdErrHandlerPtr->parseText(Utils::FormattedText(list.takeFirst()));

            while(outList.size())
            {
                QString txt = outList.takeFirst().text;

                if(txt.startsWith(QStringLiteral("±")) || txt.startsWith(QStringLiteral("Û")))
                {
                    continue;
                }

                if(txt.startsWith(QStringLiteral("[")))
                {
                    QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\d+)%")).match(txt);

                    if(m.hasMatch())
                    {
                        dialog->setProgressBarLabel(Tr::tr("Downloading..."));
                        int p = m.captured(1).toInt();
                        dialog->setProgressBarRange(0, 100);
                        dialog->setProgressBarValue(p);
                    }

                    if(!*stdErrFirstTimePtr)
                    {
                        QTextCursor cursor = dialog->textCursor();
                        cursor.movePosition(QTextCursor::End);
                        cursor.select(QTextCursor::BlockUnderCursor);
                        cursor.removeSelectedText();
                        dialog->setTextCursor(cursor);
                    }

                    *stdErrFirstTimePtr = false;
                }

                dialog->appendPlainText(txt);
            }
        }
    });

    Utils::FilePath binary;
    QStringList args = QStringList() <<
                       QStringLiteral("-c") <<
                       QStringLiteral("port=USB1") <<
                       QStringLiteral("-d") <<
                       QDir::toNativeSeparators(QDir::cleanPath(path));

    if(Utils::HostOsInfo::isWindowsHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("stcubeprogrammer/windows/STM32_Programmer_CLI.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("stcubeprogrammer/mac/bin/STM32_Programmer_CLI"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("stcubeprogrammer/linux64/bin/STM32_Programmer_CLI"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("stcubeprogrammer/aarch64/bin/STM32_Programmer_CLI"));
        }
    }

    Utils::Environment env = process.environment();
    env.prependOrSet("STM32_PRG_PATH", QFileInfo(binary.toString()).path());

    if(binary.isEmpty())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("STM32 Programmer"),
            Tr::tr("STM32 Programmer is not supported on this platform."));

        delete dialog;
        settings->endGroup();
    }
    else
    {
        command = QString(QStringLiteral("%1 %2")).arg(binary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        process.setEnvironment(env);

        dialog->show();
        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(binary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        delete dialog;
        settings->endGroup();
    }
}

} // namespace Internal
} // namespace OpenMV
