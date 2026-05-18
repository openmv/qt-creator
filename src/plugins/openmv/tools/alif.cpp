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
#include <QSerialPort>
#include <QSerialPortInfo>

#include <coreplugin/icore.h>
#include <extensionsystem/pluginmanager.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/qtcprocess.h>
#include <utils/theme/theme.h>

#include "loaderdialog.h"
#include "openmvtr.h"

namespace OpenMV {
namespace Internal {

QMutex alif_tools_working;

static bool copyOperator(const Utils::FilePath &src, const Utils::FilePath &dest, QString *error)
{
    dest.parentDir().ensureWritableDir();

    if (!src.copyFile(dest))
    {
        if (error)
        {
            *error = Tr::tr("Could not copy file \"%1\" to \"%2\".").arg(src.toUserOutput(), dest.toUserOutput());
        }

        return false;
    }

    return true;
}

bool alifSyncTools()
{
    Utils::FilePath tools;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        tools = Core::ICore::resourcePath(QStringLiteral("alif/windows"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        tools = Core::ICore::resourcePath(QStringLiteral("alif/mac"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            tools = Core::ICore::resourcePath(QStringLiteral("alif/linux-x86_64"));
        }
    }

    if (tools.isEmpty())
    {
        return true;
    }

    if (!Core::ICore::allUsersResourcePath(QStringLiteral("alif")).exists())
    {
        QString error;

        if(!Utils::FileUtils::copyRecursively(tools, Core::ICore::allUsersResourcePath(QStringLiteral("alif")), &error, copyOperator))
        {
            QMessageBox::critical(Q_NULLPTR, QString(),
                                  Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
            return false;
        }
    }
    else
    {
        QFile file(Core::ICore::allUsersResourcePath(QStringLiteral("alif/version.json")).toString());

        if (file.open(QFile::ReadOnly))
        {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            file.close();

            int current_version_major = doc.object().value(QStringLiteral("version_major")).toInt();
            int current_version_minor = doc.object().value(QStringLiteral("version_minor")).toInt();
            int current_version_patch = doc.object().value(QStringLiteral("version_patch")).toInt();

            QFile file2(tools.pathAppended("version.json").toString());

            if (file2.open(QFile::ReadOnly))
            {
                QJsonDocument doc2 = QJsonDocument::fromJson(file2.readAll());
                file2.close();

                int new_version_major = doc2.object().value(QStringLiteral("version_major")).toInt();
                int new_version_minor = doc2.object().value(QStringLiteral("version_minor")).toInt();
                int new_version_patch = doc2.object().value(QStringLiteral("version_patch")).toInt();

                if((current_version_major < new_version_major)
                || ((current_version_major == new_version_major) && (current_version_minor < new_version_minor))
                || ((current_version_major == new_version_major) && (current_version_minor == new_version_minor) && (current_version_patch < new_version_patch)))
                {
                    QString error;

                    if(!Core::ICore::allUsersResourcePath(QStringLiteral("alif")).removeRecursively(&error))
                    {
                        QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                        return false;
                    }

                    if(!Utils::FileUtils::copyRecursively(tools, Core::ICore::allUsersResourcePath(QStringLiteral("alif")), &error, copyOperator))
                    {
                        QMessageBox::critical(Q_NULLPTR, QString(),
                                              Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                        return false;
                    }
                }
            }
            else
            {
                QMessageBox::critical(Q_NULLPTR, QString(),
                                      Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                return false;
            }
        }
        else // corrupt
        {
            QString error;

            if(!Core::ICore::allUsersResourcePath(QStringLiteral("alif")).removeRecursively(&error))
            {
                QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                return false;
            }

            if(!Utils::FileUtils::copyRecursively(tools, Core::ICore::allUsersResourcePath(QStringLiteral("alif")), &error, copyOperator))
            {
                QMessageBox::critical(Q_NULLPTR, QString(),
                                      Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                return false;
            }
        }
    }

    return true;
}

QList<QPair<int, int> > alifVidPidList(const QJsonDocument &settings)
{
    QList<QPair<int, int> > vidPidlist;

    for(const QJsonValue &val : settings.object().value(QStringLiteral("boards")).toArray())
    {
        QJsonObject obj = val.toObject();

        if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("alif_tools"))
        {
            QStringList vidPid = obj.value(QStringLiteral("bootloaderVidPid")).toString().split(QChar(':'));

            if(vidPid.size() == 2)
            {
                bool okay_vid; int vid = vidPid.at(0).toInt(&okay_vid, 16);
                bool okay_pid; int pid = vidPid.at(1).toInt(&okay_pid, 16);

                if(okay_vid && okay_pid)
                {
                    QPair<int, int> entry(vid, pid);

                    if(!vidPidlist.contains(entry))
                    {
                        vidPidlist.append(entry);
                    }
                }
            }
        }
    }

    return vidPidlist;
}

QList<QString> alifGetDevices(const QJsonDocument &settings)
{
    QList<QString> devices;

    QList<QPair<int, int> > vidPidlist = alifVidPidList(settings);

    for (const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
    {
        if(Utils::HostOsInfo::isMacHost() && (!info.portName().contains(QStringLiteral("cu"), Qt::CaseInsensitive)))
        {
            continue;
        }

        if (info.hasVendorIdentifier() && info.hasProductIdentifier())
        {
            QPair<int, int> entry(info.vendorIdentifier(), info.productIdentifier());

            if (vidPidlist.contains(entry))
            {
                QString portName = info.portName();

                if(!Utils::HostOsInfo::isWindowsHost())
                {
                    portName.prepend(QStringLiteral("/dev/"));
                }

                devices.append(QString(QStringLiteral("%1:%2,%3")).
                               arg(info.vendorIdentifier(), 4, 16, QChar('0')).
                               arg(info.productIdentifier(), 4, 16, QChar('0')).
                               arg(portName));
            }
        }
    }

    return devices;
}

static bool alifSelectPort(const QString &port)
{
    QFile file(Core::ICore::allUsersResourcePath(QStringLiteral("alif/isp_config_data.cfg")).toString());

    if (file.open(QFile::WriteOnly))
    {
        QTextStream stream(&file);

        stream << "comport " << port << "\n";
        stream << "timeout tx 2\n";
        stream << "timeout rx 0\n";
        stream << "stopbits 1\n";
        stream << "bytesize 8\n";
        stream << "rtscts 0\n";
        stream << "xonxoff 0\n";

        file.close();

        return true;
    }
    else
    {
        return false;
    }
}

static bool alifWriteGlobalCFG(const QJsonObject &obj)
{
    QFile file(Core::ICore::allUsersResourcePath(QStringLiteral("alif/utils/global-cfg.db")).toString());

    if (file.open(QFile::WriteOnly))
    {
        QJsonDocument doc;
        doc.setObject(obj.value(QStringLiteral("global-cfg.db")).toObject());
        QByteArray bytes = doc.toJson();
        bool ok = file.write(bytes) == bytes.size();
        file.close();
        return ok;
    }
    else
    {
        return false;
    }
}

static bool alifUpdateBuild(const QString &originalFirmwareFolder)
{
    QString error;

    if (Core::ICore::allUsersResourcePath(QStringLiteral("alif/build")).exists())
    {
        if(!Core::ICore::allUsersResourcePath(QStringLiteral("alif/build")).removeRecursively(&error))
        {
            return false;
        }
    }

    if (QDir(originalFirmwareFolder).exists())
    {
        if(!Utils::FileUtils::copyRecursively(Utils::FilePath::fromString(originalFirmwareFolder),
                                               Core::ICore::allUsersResourcePath(QStringLiteral("alif/build")), &error, copyOperator))
        {
            return false;
        }
    }
    else
    {
        if(!Utils::FileUtils::copyRecursively(Core::ICore::allUsersResourcePath(QStringLiteral("firmware")).pathAppended(originalFirmwareFolder),
                                               Core::ICore::allUsersResourcePath(QStringLiteral("alif/build")), &error, copyOperator))
        {
            return false;
        }
    }

    QDir buildDir(Core::ICore::allUsersResourcePath(QStringLiteral("alif/build")).toString());

    if (!buildDir.mkpath(QStringLiteral("images")))
    {
        return false;
    }


    for (const QFileInfo &info : buildDir.entryInfoList(QStringList() << QStringLiteral("*.bin") << QStringLiteral("*.sign"), QDir::Files | QDir::NoDotAndDotDot))
    {
        if (!QFile::copy(info.absoluteFilePath(), buildDir.filePath(QStringLiteral("images")).append(QDir::separator()).append(info.fileName())))
        {
            return false;
        }
    }

    return true;
}

bool alifDownloadFirmware(const QString &port, const QString &originalFirmwareFolder, const QJsonObject &obj)
{
    QMutexLocker locker(&alif_tools_working);

    QFile file(Core::ICore::allUsersResourcePath(QStringLiteral("alif/version.json")).toString());

    int current_version_major = 0;
    int current_version_minor = 0;
    int current_version_patch = 0;

    if (file.open(QFile::ReadOnly))
    {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());

        current_version_major = doc.object().value(QStringLiteral("version_major")).toInt();
        current_version_minor = doc.object().value(QStringLiteral("version_minor")).toInt();
        current_version_patch = doc.object().value(QStringLiteral("version_patch")).toInt();

        file.close();
    }

    QJsonObject dfuBootloaderProgramCommand = obj.value(QStringLiteral("dfuBootloaderProgramCommand")).toObject();

    bool result = true;
    Utils::Process process;

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    LoaderDialog *dialog = new LoaderDialog(Tr::tr("Alif Tools"), Tr::tr("Flashing Firmware"), process, settings, QStringLiteral(LAST_LOADERDIALOG_TERMINAL_WINDOW_GEOMETRY),
                                            Core::ICore::dialogParent());

    Utils::Process *processPtr = &process;
    QString stdOutBuffer = QString();
    QString *stdOutBufferPtr = &stdOutBuffer;
    bool stdOutFirstTime = true;
    bool *stdOutFirstTimePtr = &stdOutFirstTime;

    int sesVersionMajor = 0;
    int sesVersionMinor = 0;
    int sesVersionPatch = 0;
    int *sesVersionMajorPtr = &sesVersionMajor;
    int *sesVersionMinorPtr = &sesVersionMinor;
    int *sesVersionPatchPtr = &sesVersionPatch;

    bool enterRecoverymode = false;
    bool enterHardMaintenanceMode = false;
    bool exitHardMaintenanceMode = false;
    bool hardMaintenanceRequired = false;
    bool *enterRecoverymodePtr = &enterRecoverymode;
    bool *enterHardMaintenanceModePtr = &enterHardMaintenanceMode;
    bool *exitHardMaintenanceModePtr = &exitHardMaintenanceMode;
    bool *hardMaintenanceRequiredPtr = &hardMaintenanceRequired;

    bool startGetBootInfo = false;
    bool stopGetBootInfo = false;
    bool appLoaded = false;
    bool *startGetBootInfoPtr = &startGetBootInfo;
    bool *stopGetBootInfoPtr = &stopGetBootInfo;
    bool *appLoadedPtr = &appLoaded;

    QMessageBox *userButtonMessageBox = new QMessageBox(QMessageBox::Information, Tr::tr("Alif Tools"),
        Tr::tr("Please turn on the hard maintenance mode switch, it not enabled, and then press the user button on your OpenMV Cam."),
        QMessageBox::NoButton, dialog,
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

    QObject::connect(&process, &Utils::Process::textOnStandardOutput, dialog,
                     [processPtr, dialog, stdOutBufferPtr, stdOutFirstTimePtr,
                      sesVersionMajorPtr, sesVersionMinorPtr, sesVersionPatchPtr,
                      enterRecoverymodePtr, enterHardMaintenanceModePtr, exitHardMaintenanceModePtr, hardMaintenanceRequiredPtr,
                      startGetBootInfoPtr, stopGetBootInfoPtr, appLoadedPtr,
                      userButtonMessageBox] (const QString &text) {
        stdOutBufferPtr->append(text);
        QStringList list = stdOutBufferPtr->split(QRegularExpression(QStringLiteral("[\r\n]")), Qt::KeepEmptyParts);

        if(list.size())
        {
            *stdOutBufferPtr = list.takeLast();
        }

        while(list.size())
        {
            QString out = list.takeFirst();

            if(out.isEmpty())
            {
                continue;
            }

            if(out.startsWith(QStringLiteral("alif")) || out.startsWith(QStringLiteral("build")))
            {
                QRegularExpressionMatch m = QRegularExpression(QStringLiteral("\\[#*\\s*\\]\\s*(\\d+)%")).match(out);

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

            if(out.startsWith(QStringLiteral("Waiting for Target")))
            {
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

            if(out.contains(QStringLiteral("Connected target is not the default Revision")))
            {
                processPtr->write(QStringLiteral("n\n"));
            }

            if(out.contains(QStringLiteral("Connected target is not the default"))
            || out.contains(QStringLiteral("Do you want to set this part as default? (y/n):"))
            || out.contains(QStringLiteral("Maintenance Mode")))
            {
                continue;
            }

            QRegularExpressionMatch m = QRegularExpression(QStringLiteral("SES\\s+\\w+\\s+v(\\d+)\\.(\\d+)\\.(\\d+)")).match(out);

            if (m.hasMatch())
            {
                *sesVersionMajorPtr = m.captured(1).toInt();
                *sesVersionMinorPtr = m.captured(2).toInt();
                *sesVersionPatchPtr = m.captured(3).toInt();

                continue;
            }

            if (*exitHardMaintenanceModePtr && out.contains(QStringLiteral("1 - Device Control")))
            {
                processPtr->write(QStringLiteral("\n"));
            }

            if (out.contains(QStringLiteral("Device connected in Recovery")))
            {
                *enterRecoverymodePtr = true;
            }

            if (*enterHardMaintenanceModePtr && out.contains(QStringLiteral("1 - Device Control")))
            {
                processPtr->write(QStringLiteral("1\n"));
            }

            if (*enterHardMaintenanceModePtr && out.contains(QStringLiteral("1 - ROM")))
            {
                processPtr->write(QStringLiteral("1\n"));
            }

            if (*exitHardMaintenanceModePtr && out.contains(QStringLiteral("1 - Hard maintenance mode")))
            {
                processPtr->write(QStringLiteral("\n"));
            }

            if (*enterHardMaintenanceModePtr && out.contains(QStringLiteral("1 - Hard maintenance mode")))
            {
                processPtr->write(QStringLiteral("1\n"));
                userButtonMessageBox->show();
                *enterHardMaintenanceModePtr = false;
                *exitHardMaintenanceModePtr = true;
                *hardMaintenanceRequiredPtr = true;
            }

            if (*enterHardMaintenanceModePtr && out.contains(QStringLiteral("1 - Recovery")))
            {
                processPtr->write(QStringLiteral("1\n"));
                *enterHardMaintenanceModePtr = false;
                *hardMaintenanceRequiredPtr = true;
            }

            if (*stopGetBootInfoPtr && out.contains(QStringLiteral("HP_BOOT")))
            {
                *appLoadedPtr = true;
            }

            if (*stopGetBootInfoPtr && out.contains(QStringLiteral("2 - Device Information")))
            {
                processPtr->write(QStringLiteral("\n"));
            }

            if (*startGetBootInfoPtr && out.contains(QStringLiteral("2 - Device Information")))
            {
                processPtr->write(QStringLiteral("2\n"));
            }

            if (*stopGetBootInfoPtr && out.contains(QStringLiteral("1 - Get TOC info")))
            {
                processPtr->write(QStringLiteral("\n"));
            }

            if (*startGetBootInfoPtr && out.contains(QStringLiteral("1 - Get TOC info")))
            {
                processPtr->write(QStringLiteral("1\n"));
                *startGetBootInfoPtr = false;
                *stopGetBootInfoPtr = true;
            }

            if (out.startsWith("\e"))
            {
                continue;
            }

            // More terminal cleanup.
            out.remove(QStringLiteral("\e[?25l"));
            out.remove(QRegularExpression(QStringLiteral("\e\\[94m.*$")));

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

            if(out.isEmpty())
            {
                continue;
            }

            if(out.startsWith(QStringLiteral("alif")) || out.startsWith(QStringLiteral("build")))
            {
                QRegularExpressionMatch m = QRegularExpression(QStringLiteral("\\[#*\\s*\\]\\s*(\\d+)%")).match(out);

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

    Utils::FilePath maintenanceBinary;
    Utils::FilePath updateSystemPackageBinary;
    Utils::FilePath appGenToc;
    Utils::FilePath appWriteMramBinary;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        maintenanceBinary = Core::ICore::allUsersResourcePath(QStringLiteral("alif/maintenance.exe"));
        updateSystemPackageBinary = Core::ICore::allUsersResourcePath(QStringLiteral("alif/updateSystemPackage.exe"));
        appGenToc = Core::ICore::allUsersResourcePath(QStringLiteral("alif/app-gen-toc.exe"));
        appWriteMramBinary = Core::ICore::allUsersResourcePath(QStringLiteral("alif/app-write-mram.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        maintenanceBinary = Core::ICore::allUsersResourcePath(QStringLiteral("alif/maintenance"));
        updateSystemPackageBinary = Core::ICore::allUsersResourcePath(QStringLiteral("alif/updateSystemPackage"));
        appGenToc = Core::ICore::allUsersResourcePath(QStringLiteral("alif/app-gen-toc"));
        appWriteMramBinary = Core::ICore::allUsersResourcePath(QStringLiteral("alif/app-write-mram"));
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            maintenanceBinary = Core::ICore::allUsersResourcePath(QStringLiteral("alif/maintenance"));
            updateSystemPackageBinary = Core::ICore::allUsersResourcePath(QStringLiteral("alif/updateSystemPackage"));
            appGenToc = Core::ICore::allUsersResourcePath(QStringLiteral("alif/app-gen-toc"));
            appWriteMramBinary = Core::ICore::allUsersResourcePath(QStringLiteral("alif/app-write-mram"));
        }
    }

    if(maintenanceBinary.isEmpty() || updateSystemPackageBinary.isEmpty() || appWriteMramBinary.isEmpty() || appGenToc.isEmpty())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Alif Tools"),
            Tr::tr("This feature is not supported on this machine!"));

        result = false;
        goto cleanup;
    }

    if ((!alifSelectPort(port)) || (!alifWriteGlobalCFG(obj)) || (!alifUpdateBuild(originalFirmwareFolder)))
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Alif Tools"),
            Tr::tr("Please close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));

        result = false;
        goto cleanup;
    }

    dialog->show();

    // Get SES Version
    {
        QStringList args = QStringList() << QStringLiteral("-opt") << QStringLiteral("sesbanner");

        QString command = QString(QStringLiteral("%1 %2")).arg(maintenanceBinary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setProcessMode(Utils::ProcessMode::Writer);
        process.setWorkingDirectory(maintenanceBinary.parentDir());
        process.setCommand(Utils::CommandLine(maintenanceBinary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        if(enterRecoverymode
        || ((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally)))
        {
            // Need to recover the board.
            {
                enterHardMaintenanceMode = true;

                QStringList args = QStringList();

                QString command = QString(QStringLiteral("%1 %2")).arg(maintenanceBinary.toString()).arg(args.join(QLatin1Char(' ')));
                dialog->appendColoredText(command);

                std::chrono::seconds timeout(300); // 5 minutes...
                process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
                process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
                process.setProcessMode(Utils::ProcessMode::Writer);
                process.setWorkingDirectory(maintenanceBinary.parentDir());
                process.setCommand(Utils::CommandLine(maintenanceBinary, args));
                process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

                userButtonMessageBox->hide();

                if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
                {
                    QMessageBox box(QMessageBox::Critical, Tr::tr("Alif Tools"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                    box.setDefaultButton(QMessageBox::Ok);
                    box.setEscapeButton(QMessageBox::Cancel);
                    box.exec();

                    result = false;
                    goto cleanup;
                }
                else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
                {
                    result = false;
                    goto cleanup;
                }
            }

            // Wait for chip to boot.

            QElapsedTimer elaspedTimer;
            elaspedTimer.start();

            while(!elaspedTimer.hasExpired(2000))
            {
                QApplication::processEvents();
            }
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    if (!hardMaintenanceRequired) // Check if an app is loaded.
    {
        startGetBootInfo = true;

        QStringList args = QStringList();

        QString command = QString(QStringLiteral("%1 %2")).arg(maintenanceBinary.toString()).arg(args.join(QLatin1Char(' ')));
        dialog->appendColoredText(command);

        std::chrono::seconds timeout(300); // 5 minutes...
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setProcessMode(Utils::ProcessMode::Writer);
        process.setWorkingDirectory(maintenanceBinary.parentDir());
        process.setCommand(Utils::CommandLine(maintenanceBinary, args));
        process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

        userButtonMessageBox->hide();

        if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
        {
            QMessageBox box(QMessageBox::Critical, Tr::tr("Alif Tools"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
            box.setDefaultButton(QMessageBox::Ok);
            box.setEscapeButton(QMessageBox::Cancel);
            box.exec();

            result = false;
            goto cleanup;
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            result = false;
            goto cleanup;
        }
    }

    // THIS IS NOT SAFE - BOARD DIES AFTER ERASE - DEBUG WITH ALIF!
    //
    // if (appLoaded || hardMaintenanceRequired) // Erase Mram
    // {
    //     QStringList args = QStringList() << QStringLiteral("-e") << QStringLiteral("APP");

    //     QString command = QString(QStringLiteral("%1 %2")).arg(appWriteMramBinary.toString()).arg(args.join(QLatin1Char(' ')));
    //     dialog->appendColoredText(command);

    //     std::chrono::seconds timeout(300); // 5 minutes...
    //     process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
    //     process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
    //     process.setProcessMode(Utils::ProcessMode::Writer);
    //     process.setWorkingDirectory(appWriteMramBinary.parentDir());
    //     process.setCommand(Utils::CommandLine(appWriteMramBinary, args));
    //     process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

    //     if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
    //     {
    //         QMessageBox box(QMessageBox::Critical, Tr::tr("Alif Tools"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
    //             Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
    //             (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    //         box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
    //         box.setDefaultButton(QMessageBox::Ok);
    //         box.setEscapeButton(QMessageBox::Cancel);
    //         box.exec();

    //         result = false;
    //         goto cleanup;
    //     }
    //     else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
    //     {
    //         result = false;
    //         goto cleanup;
    //     }

    //     if (QMessageBox::information(Core::ICore::dialogParent(),
    //         Tr::tr("Alif Tools"),
    //         Tr::tr("Please disconnect your OpenMV Cam from your computer, turn off the hard maintenance mode switch, if enabled, reconnect your OpenMV Cam to your computer, and then press Ok."),
    //         QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
    //     != QMessageBox::Ok)
    //     {
    //         result = false;
    //         goto cleanup;
    //     }
    // }

    if (enterRecoverymode)
    {
        QMessageBox::information(Core::ICore::dialogParent(),
                                 Tr::tr("Connect"),
                                 Tr::tr("Please disconnect and then reconnect your OpenMV Cam from your computer and then press Ok.\n\n"
                                        "The camera must be power cycled after after recovery."));

        enterRecoverymode = false;
    }

    if(hardMaintenanceRequired
    || (sesVersionMajor < current_version_major)
    || ((sesVersionMajor == current_version_major) && (sesVersionMinor < current_version_minor))
    || ((sesVersionMajor == current_version_major) && (sesVersionMinor == current_version_minor) && (sesVersionPatch < current_version_patch)))
    {
        // Update System Package
        {
            QStringList args = QStringList();

            QString command = QString(QStringLiteral("%1 %2")).arg(updateSystemPackageBinary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setProcessMode(Utils::ProcessMode::Writer);
            process.setWorkingDirectory(updateSystemPackageBinary.parentDir());
            process.setCommand(Utils::CommandLine(updateSystemPackageBinary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("Alif Tools"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }

        QMessageBox::information(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Please disconnect and then reconnect your OpenMV Cam from your computer and then press Ok.\n\n"
                           "The camera must be power cycled after a system package update."));
    }

    if (!dfuBootloaderProgramCommand.isEmpty())
    {
        // Write Bootloader
        {
            QStringList args = QStringList() << QStringLiteral("-i") <<
                dfuBootloaderProgramCommand.value(QStringLiteral("images")).toString();

            QString command = QString(QStringLiteral("%1 %2")).arg(appWriteMramBinary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setProcessMode(Utils::ProcessMode::Writer);
            process.setWorkingDirectory(appWriteMramBinary.parentDir());
            process.setCommand(Utils::CommandLine(appWriteMramBinary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("Alif Tools"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }
    else
    {
        // App Gen Toc
        {
            QStringList args = QStringList() << QStringLiteral("-f") << QStringLiteral("build/config/alif_cfg.json");

            QString command = QString(QStringLiteral("%1 %2")).arg(appGenToc.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setProcessMode(Utils::ProcessMode::Writer);
            process.setWorkingDirectory(appGenToc.parentDir());
            process.setCommand(Utils::CommandLine(appGenToc, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("Alif Tools"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }

        // App Write Mram
        {
            QStringList args = QStringList();

            QString command = QString(QStringLiteral("%1 %2")).arg(appWriteMramBinary.toString()).arg(args.join(QLatin1Char(' ')));
            dialog->appendColoredText(command);

            std::chrono::seconds timeout(300); // 5 minutes...
            process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
            process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
            process.setProcessMode(Utils::ProcessMode::Writer);
            process.setWorkingDirectory(appWriteMramBinary.parentDir());
            process.setCommand(Utils::CommandLine(appWriteMramBinary, args));
            process.runBlocking(timeout, Utils::EventLoopMode::On, QEventLoop::AllEvents);

            if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("Alif Tools"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                result = false;
                goto cleanup;
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                result = false;
                goto cleanup;
            }
        }
    }

    if ((appLoaded || hardMaintenanceRequired) &&
        QMessageBox::information(Core::ICore::dialogParent(),
                                 Tr::tr("Alif Tools"),
                                 Tr::tr("Please disconnect your OpenMV Cam from your computer, turn off the hard maintenance mode switch, if enabled. "\
                                        "Leave your OpenMV Cam unconnected until instructed to reconnect it."),
                                 QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
        != QMessageBox::Ok)
    {
        result = false;
        goto cleanup;
    }

cleanup:

    delete dialog;

    return result;
}

} // namespace Internal
} // namespace OpenMV
