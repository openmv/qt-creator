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

#include <QtCore>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>

#include "loaderdialog.h"
#include "openmvtr.h"

namespace OpenMV {
namespace Internal {

QMutex dfu_util_working;

QList<QPair<int, int> > dfuVidPidList(const QJsonDocument &settings)
{
    QList<QPair<int, int> > pidvidlist;

    for(const QJsonValue &val : settings.object().value(QStringLiteral("boards")).toArray())
    {
        QJsonObject obj = val.toObject();

        if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu"))
        {
            QStringList pidvid = obj.value(QStringLiteral("bootloaderVidPid")).toString().split(QChar(':'));

            if(pidvid.size() == 2)
            {
                bool okay_pid; int pid = pidvid.at(0).toInt(&okay_pid, 16);
                bool okay_vid; int vid = pidvid.at(1).toInt(&okay_vid, 16);

                if(okay_pid && okay_vid)
                {
                    QPair<int, int> entry(pid, vid);

                    if(!pidvidlist.contains(entry))
                    {
                        pidvidlist.append(entry);
                    }
                }
            }
        }
    }

    return pidvidlist;
}

QList<QString> getDevices()
{
    if (QThread::currentThread() == QCoreApplication::instance()->thread())
    {
        dfu_util_working.lock();
    }
    else
    {
        if(!dfu_util_working.tryLock()) return QList<QString>();
    }

    Utils::FilePath command;
    Utils::Process process;
    std::chrono::seconds timeout(10);
    process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process.setProcessChannelMode(QProcess::MergedChannels);

    if(Utils::HostOsInfo::isWindowsHost())
    {
        command = Core::ICore::resourcePath(QStringLiteral("dfu-util/windows/dfu-util.exe"));
        process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("-l")));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        command = Core::ICore::resourcePath(QStringLiteral("dfu-util/osx/dfu-util"));
        process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("-l")));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            command = Core::ICore::resourcePath(QStringLiteral("dfu-util/linux32/dfu-util"));
            process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("-l")));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            command = Core::ICore::resourcePath(QStringLiteral("dfu-util/linux64/dfu-util"));
            process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("-l")));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            command = Core::ICore::resourcePath(QStringLiteral("dfu-util/arm/dfu-util"));
            process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("-l")));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        {
            command = Core::ICore::resourcePath(QStringLiteral("dfu-util/aarch64/dfu-util"));
            process.setCommand(Utils::CommandLine(command, QStringList() << QStringLiteral("-l")));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);
        }
    }

    dfu_util_working.unlock();

    if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
    {
        QStringList list, cannot, in = process.stdOut().split(QRegularExpression(QStringLiteral("\n|\r\n|\r")), Qt::SkipEmptyParts);

        for(const QString &string : qAsConst(in))
        {
            QRegularExpressionMatch match = QRegularExpression(QStringLiteral("Found DFU: \\[([A-Fa-f0-9:]+)\\].+?alt=0.+?serial=\"(.+?)\"")).match(string);

            if(match.hasMatch())
            {
                list.append(match.captured(1) + QStringLiteral(",") + match.captured(2));
            }

            QRegularExpressionMatch match2 = QRegularExpression(QStringLiteral("Cannot open DFU device ([A-Fa-f0-9:]+)")).match(string);

            if(match2.hasMatch() && (match2.captured(1).toLower() == QStringLiteral("0483:df11")))
            {
                cannot.append(match2.captured(1));
            }
        }

        for(const QString &string : cannot)
        {
            list.append(string + QStringLiteral(",NULL"));
        }

        return list;
    }
    else if(QThread::currentThread() == QCoreApplication::instance()->thread())
    {
        QMessageBox box(QMessageBox::Warning, Tr::tr("Get Devices"), Tr::tr("Query failed!"), QMessageBox::Ok, Core::ICore::dialogParent(),
            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
        box.setDetailedText(command.toString() + QStringLiteral("\n\n") + process.stdOut());
        box.setDefaultButton(QMessageBox::Ok);
        box.setEscapeButton(QMessageBox::Cancel);
        box.exec();

        return QList<QString>();
    }
    else
    {
        return QList<QString>();
    }
}

void downloadFirmware(const QString &details,
                      QString &command, Utils::Process &process,
                      const QString &path, const QString &device, const QString &moreArgs,
                      bool uploadInstead, int uploadSize,
                      const QString &extraMessage)
{
    QStringList list;

    for(const QString &d : getDevices())
    {
        QStringList vidpidserial = d.split(QStringLiteral(","));

        if((vidpidserial.at(0).toLower() == device.toLower()) && (vidpidserial.at(1) == QStringLiteral("NULL")))
        {
            list.append(d);
        }
    }

    QMutexLocker locker(&dfu_util_working);

    if((!uploadInstead) && (!list.isEmpty()))
    {
        // DfuSe/PyDfu do not offer a way to actually target the correct DFU device. So, if there's anything in the list
        // we are just going to run blindly and hope the user only hooked up one device.

        if(Utils::HostOsInfo::isWindowsHost() && path.endsWith("dfu", Qt::CaseInsensitive))
        {
            Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
            settings->beginGroup(LOADERDIALOG_SETTINGS_GROUP);
            LoaderDialog *dialog = new LoaderDialog(Tr::tr("DfuSe"), details, process, settings, QStringLiteral(LAST_LOADERDIALOG_TERMINAL_WINDOW_GEOMETRY),
                                                    Core::ICore::dialogParent());

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

                    if(out.startsWith(QStringLiteral("Target")))
                    {
                        QRegularExpressionMatch m = QRegularExpression(QStringLiteral("Target \\d+: Upgrading - (\\w+) Phase \\((\\d+)\\)")).match(out);

                        if(m.hasMatch())
                        {
                            dialog->setProgressBarLabel(m.captured(1) == QStringLiteral("Erase") ? Tr::tr("Erasing...") : Tr::tr("Downloading..."));
                            int p = m.captured(2).toInt();
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

                    dialog->appendPlainText(out);
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

                    if(out.startsWith(QStringLiteral("Target")))
                    {
                        QRegularExpressionMatch m = QRegularExpression(QStringLiteral("Target \\d+: Upgrading - (\\w+) Phase \\((\\d+)\\)")).match(out);

                        if(m.hasMatch())
                        {
                            dialog->setProgressBarLabel(m.captured(1) == QStringLiteral("Erase") ? Tr::tr("Erasing...") : Tr::tr("Downloading..."));
                            int p = m.captured(2).toInt();
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

                    dialog->appendColoredText(out);
                }
            });

            Utils::FilePath binary = Core::ICore::resourcePath(QStringLiteral("dfuse/DfuSeCommand.exe"));
            QStringList args = QStringList() <<
                               QStringLiteral("-c") <<
                               QStringLiteral("-d") <<
                               QStringLiteral("--v") <<
                               QStringLiteral("--o") <<
                               QStringLiteral("--fn") <<
                               QDir::toNativeSeparators(QDir::cleanPath(path));

            command = QStringLiteral("%1 %2").arg(binary.toString(), args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            dialog->show();
            std::chrono::seconds timeout(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            delete dialog;
            settings->endGroup();
        }
        else
        {
            Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
            settings->beginGroup(LOADERDIALOG_SETTINGS_GROUP);
            LoaderDialog *dialog = new LoaderDialog(Tr::tr("PyDfu"), details, process, settings, QStringLiteral(LAST_LOADERDIALOG_TERMINAL_WINDOW_GEOMETRY),
                                                    Core::ICore::dialogParent());

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

                    if(out.startsWith(QStringLiteral("0x")))
                    {
                        QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\d+)%")).match(out);

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

                    dialog->appendPlainText(out);
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

                    if(out.startsWith(QStringLiteral("0x")))
                    {
                        QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\d+)%")).match(out);

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

                    dialog->appendColoredText(out);
                }
            });

            Utils::FilePath binary = Utils::FilePath::fromString(QStringLiteral("python3"));
            QStringList args = QStringList() <<
                               Core::ICore::resourcePath(QStringLiteral("pydfu/pydfu.py")).toString() <<
                               QStringLiteral("-u") <<
                               QDir::toNativeSeparators(QDir::cleanPath(path));

            command = QStringLiteral("%1 %2").arg(binary.toString(), args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            dialog->show();
            std::chrono::seconds timeout(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setCommand(Utils::CommandLine(binary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            delete dialog;
            settings->endGroup();
        }

        return;
    }

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(LOADERDIALOG_SETTINGS_GROUP);
    LoaderDialog *dialog = new LoaderDialog(Tr::tr("DFU Util"), details, process, settings, QStringLiteral(LAST_LOADERDIALOG_TERMINAL_WINDOW_GEOMETRY),
                                            Core::ICore::dialogParent());

    QString stdOutBuffer = QString();
    QString *stdOutBufferPtr = &stdOutBuffer;
    bool stdOutFirstTime = true;
    bool *stdOutFirstTimePtr = &stdOutFirstTime;

    QObject::connect(&process, &Utils::Process::textOnStandardOutput, dialog, [dialog, uploadInstead, uploadSize, stdOutBufferPtr, stdOutFirstTimePtr] (const QString &text) {
        stdOutBufferPtr->append(text);
        QStringList list = stdOutBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdOutBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if(out.startsWith(QStringLiteral("Erase")) || out.startsWith(QStringLiteral("Download")) || out.startsWith(QStringLiteral("Upload")))
            {
                QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\w+)\\s+\\[=*\\s*\\]\\s+(\\d+)%\\s+(\\d+)\\s+bytes")).match(out);

                if(m.hasMatch())
                {
                    dialog->setProgressBarLabel(m.captured(1) == QStringLiteral("Erase") ? Tr::tr("Erasing...") : (uploadInstead ? Tr::tr("Uploading...") : Tr::tr("Downloading...")));
                    int p = m.captured(2).toInt();
                    if (uploadInstead && uploadSize) p = (m.captured(3).toLongLong() * 100) / uploadSize;
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

            dialog->appendPlainText(out);
        }
    });

    QString stdErrBuffer = QString();
    QString *stdErrBufferPtr = &stdErrBuffer;
    bool stdErrFirstTime = true;
    bool *stdErrFirstTimePtr = &stdErrFirstTime;

    QObject::connect(&process, &Utils::Process::textOnStandardError, dialog, [dialog, uploadInstead, uploadSize, stdErrBufferPtr, stdErrFirstTimePtr] (const QString &text) {
        stdErrBufferPtr->append(text);
        QStringList list = stdErrBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdErrBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if(out.startsWith(QStringLiteral("Erase")) || out.startsWith(QStringLiteral("Download")) || out.startsWith(QStringLiteral("Upload")))
            {
                QRegularExpressionMatch m = QRegularExpression(QStringLiteral("(\\w+)\\s+\\[=*\\s*\\]\\s+(\\d+)%\\s+(\\d+)\\s+bytes")).match(out);

                if(m.hasMatch())
                {
                    dialog->setProgressBarLabel(m.captured(1) == QStringLiteral("Erase") ? Tr::tr("Erasing...") : (uploadInstead ? Tr::tr("Uploading...") : Tr::tr("Downloading...")));
                    int p = m.captured(2).toInt();
                    if (uploadInstead && uploadSize) p = (m.captured(3).toLongLong() * 100) / uploadSize;
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

            dialog->appendColoredText(out);
        }
    });

    Utils::FilePath binary;
    QStringList args = QStringList() <<
                       QStringLiteral("-w") <<
                       QStringLiteral("-d") <<
                       QString(QStringLiteral(",%1")).arg(device) <<
                       moreArgs.split(QLatin1Char(' ')) <<
                       (uploadInstead ? QStringLiteral("-U") : QStringLiteral("-D")) <<
                       QDir::toNativeSeparators(QDir::cleanPath(path));

    if(Utils::HostOsInfo::isWindowsHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("dfu-util/windows/dfu-util.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        binary = Core::ICore::resourcePath(QStringLiteral("dfu-util/osx/dfu-util"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("i386"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("dfu-util/linux32/dfu-util"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("dfu-util/linux64/dfu-util"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("dfu-util/arm/dfu-util"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        {
            binary = Core::ICore::resourcePath(QStringLiteral("dfu-util/aarch64/dfu-util"));
        }
    }

    std::chrono::seconds timeout(300); // 5 minutes...

    if (QFileInfo(path).size() > (4 * 1024 * 1024))
    {
        timeout *= (QFileInfo(path).size() / (4 * 1024 * 1024));

        switch (QDateTime::currentSecsSinceEpoch() % 4)
        {
            case 0: {
                dialog->appendColoredText(Tr::tr("This may take a while, coffee break?"), true);
                break;
            }
            case 1: {
                dialog->appendColoredText(Tr::tr("This may take a while, snack break?"), true);
                break;
            }
            case 2: {
                dialog->appendColoredText(Tr::tr("This may take a while, stretch time?"), true);
                break;
            }
            case 3: {
                dialog->appendColoredText(Tr::tr("This may take a while, water break?"), true);
                break;
            }
        }
    }

    if (!extraMessage.isEmpty())
    {
        dialog->appendColoredText(extraMessage, true);
    }

    command = QStringLiteral("%1 %2").arg(binary.toString(), args.join(QLatin1Char(' ')));
    dialog->appendColoredText(command);

    dialog->show();
    process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    process.setCommand(Utils::CommandLine(binary, args));
    process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

    delete dialog;
    settings->endGroup();
}

} // namespace Internal
} // namespace OpenMV
