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

#include "openmvplugin.h"
#include "openmvtr.h"
#include "openmvpluginconnect.h"

namespace OpenMV {
namespace Internal {

void OpenMVPlugin::openmvDFUBootloader(bool forceFlashFSErase,
                                       bool justEraseFlashFs,
                                       bool installTheLatestDevelopmentFirmware,
                                       const QString &firmwarePath,
                                       const QString &selectedDfuDevice,
                                       OpenMVROMFSAccess romfsAccess,
                                       const QString &extraMessage)
{
    if(selectedDfuDevice.isEmpty())
    {
        QEventLoop loop;

        connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                &loop, &QEventLoop::quit);

        flushPortPath();

        // We want to enter the bootloader here and not have it exit on us by forcing the bootloader.
        // However, until the V2 protocol, there was a bug in the bootloader that could cause it not
        // to actually exit after DFU detach. On the V2 protocol we can detect the bootloader verison
        // and fallback to normal reset if needed.
        m_iodevice->sysReset(m_iodevice->v2ProtocolEnabled());
        m_iodevice->close();

        loop.exec();

        QElapsedTimer elaspedTimer;
        elaspedTimer.start();

        while(!elaspedTimer.hasExpired(100))
        {
            QApplication::processEvents();
        }
    }

    QString selectedDfuDeviceVidPid = selectedDfuDevice.isEmpty() ? QString() : selectedDfuDevice.split(QStringLiteral(",")).first();
    QString selectedDfuDeviceSerialNumber = selectedDfuDevice.isEmpty() ? QString() : selectedDfuDevice.split(QStringLiteral(",")).last();

    QString boardTypeToDfuDeviceVidPid;
    QStringList eraseCommands, programCommandsCmd, programCommandsPath;
    QStringList resetROMFSCommandsCmd, resetROMFSCommandsPath;
    QStringList binProgramCommands, binProgramPaths;
    QList<int> binProgramSizes;

    QString firmwarePathFileName = QFileInfo(firmwarePath).fileName();

    if(selectedDfuDevice.isEmpty())
    {
        bool foundMatch = false;

        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            QJsonObject obj = val.toObject();

            if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu"))
            {
                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();
                QStringList avidpid = obj.value(QStringLiteral("boardVidPid")).toString().split(QLatin1Char(':'));
                QStringList bvidpid = obj.value(QStringLiteral("bootloaderVidPid")).toString().split(QLatin1Char(':'));

                if((m_boardType == obj.value(QStringLiteral("boardType")).toString())
                && (((m_boardVID == avidpid.first().toInt(nullptr, 16)) && (m_boardPID == avidpid.last().toInt(nullptr, 16)))
                || ((m_boardVID == bvidpid.first().toInt(nullptr, 16)) && (m_boardPID == bvidpid.last().toInt(nullptr, 16)))))
                {
                    boardTypeToDfuDeviceVidPid = obj.value(QStringLiteral("bootloaderVidPid")).toString();

                    QJsonArray eraseCommandsArray = bootloaderSettings.value(QStringLiteral("eraseCommands")).toArray();
                    for(const QJsonValue &command : eraseCommandsArray)
                    {
                        eraseCommands.append(command.toString());
                    }

                    QJsonArray extraProgramCommandsArray = bootloaderSettings.value(QStringLiteral("programCommands")).toArray();
                    for(const QJsonValue &command : extraProgramCommandsArray)
                    {
                        // if(romfsAccess == OPENMV_ROMFS_NONE && programCommandsPath.contains(QStringLiteral(".img"), Qt::CaseInsensitive))
                        // {
                        //     continue;
                        // }

                        QJsonObject obj2 = command.toObject();
                        programCommandsCmd.append(obj2.value(QStringLiteral("cmd")).toString());
                        programCommandsPath.append(obj2.value(QStringLiteral("path")).toString());
                    }

                    QJsonArray resetROMFSCommandsArray = bootloaderSettings.value(QStringLiteral("resetROMFSCommands")).toArray();
                    for(const QJsonValue &command : resetROMFSCommandsArray)
                    {
                        QJsonObject obj2 = command.toObject();
                        resetROMFSCommandsCmd.append(obj2.value(QStringLiteral("cmd")).toString());
                        resetROMFSCommandsPath.append(obj2.value(QStringLiteral("path")).toString());
                    }

                    if (firmwarePathFileName.endsWith(QStringLiteral("lst")))
                    {
                        QFile file(firmwarePath);

                        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                        {
                            QStringList lines;
                            QTextStream in(&file);
                            while (!in.atEnd()) lines.append(in.readLine());
                            file.close();

                            for (const QJsonValue &cmd : bootloaderSettings.value(QStringLiteral("binProgamCommands")).toArray())
                            {
                                for (const QString &line : lines)
                                {
                                    if (QFileInfo(line).fileName().toLower() == cmd.toObject().value(QStringLiteral("name")).toString().toLower())
                                    {
                                        binProgramCommands.append(cmd.toObject().value(QStringLiteral("cmd")).toString());
                                        binProgramPaths.append(QFileInfo(firmwarePath).path() + QDir::separator() + line);
                                        binProgramSizes.append(cmd.toObject().value(QStringLiteral("size")).toInt());
                                    }
                                }
                            }
                        }
                    }
                    else
                    {
                        for (const QJsonValue &cmd : bootloaderSettings.value(QStringLiteral("binProgamCommands")).toArray())
                        {
                            if (firmwarePathFileName.toLower() == cmd.toObject().value(QStringLiteral("name")).toString().toLower())
                            {
                                binProgramCommands.append(cmd.toObject().value(QStringLiteral("cmd")).toString());
                                binProgramPaths.append(firmwarePath);
                                binProgramSizes.append(cmd.toObject().value(QStringLiteral("size")).toInt());
                            }
                        }
                    }

                    foundMatch = true;
                    break;
                }
            }
        }

        if(!foundMatch)
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("No DFU settings for the selected board type!") + QString(QStringLiteral("\n\nVID: %1, PID: %2")).arg(m_boardVID).arg(m_boardPID));

            CONNECT_END();
        }

        if (binProgramCommands.isEmpty() && QFileInfo(firmwarePath).exists())
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("No matching interface for the selected file name!") + QString(QStringLiteral("\n\nVID: %1, PID: %2")).arg(m_boardVID).arg(m_boardPID));

            CONNECT_END();
        }
    }
    else
    {
        bool foundMatch = false;

        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            QJsonObject obj = val.toObject();

            if((selectedDfuDeviceVidPid.toLower() == obj.value(QStringLiteral("bootloaderVidPid")).toString().toLower())
            && (obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu")))
            {
                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();

                QJsonArray eraseCommandsArray = bootloaderSettings.value(QStringLiteral("eraseCommands")).toArray();
                for(const QJsonValue &command : eraseCommandsArray)
                {
                    eraseCommands.append(command.toString());
                }

                QJsonArray extraProgramCommandsArray = bootloaderSettings.value(QStringLiteral("programCommands")).toArray();
                for(const QJsonValue &command : extraProgramCommandsArray)
                {
                    // if(romfsAccess == OPENMV_ROMFS_NONE && programCommandsPath.contains(QStringLiteral(".img"), Qt::CaseInsensitive))
                    // {
                    //     continue;
                    // }

                    QJsonObject obj2 = command.toObject();
                    programCommandsCmd.append(obj2.value(QStringLiteral("cmd")).toString());
                    programCommandsPath.append(obj2.value(QStringLiteral("path")).toString());
                }

                QJsonArray resetROMFSCommandsArray = bootloaderSettings.value(QStringLiteral("resetROMFSCommands")).toArray();
                for(const QJsonValue &command : resetROMFSCommandsArray)
                {
                    QJsonObject obj2 = command.toObject();
                    resetROMFSCommandsCmd.append(obj2.value(QStringLiteral("cmd")).toString());
                    resetROMFSCommandsPath.append(obj2.value(QStringLiteral("path")).toString());
                }

                if (firmwarePathFileName.endsWith(QStringLiteral("lst")))
                {
                    QFile file(firmwarePath);

                    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                    {
                        QStringList lines;
                        QTextStream in(&file);
                        while (!in.atEnd()) lines.append(in.readLine());
                        file.close();

                        for (const QJsonValue &cmd : bootloaderSettings.value(QStringLiteral("binProgamCommands")).toArray())
                        {
                            for (const QString &line : lines)
                            {
                                if (QFileInfo(line).fileName().toLower() == cmd.toObject().value(QStringLiteral("name")).toString().toLower())
                                {
                                    binProgramCommands.append(cmd.toObject().value(QStringLiteral("cmd")).toString());
                                    binProgramPaths.append(QFileInfo(firmwarePath).path() + QDir::separator() + line);
                                    binProgramSizes.append(cmd.toObject().value(QStringLiteral("size")).toInt());
                                }
                            }
                        }
                    }
                }
                else
                {
                    for (const QJsonValue &cmd : bootloaderSettings.value(QStringLiteral("binProgamCommands")).toArray())
                    {
                        if (firmwarePathFileName.toLower() == cmd.toObject().value(QStringLiteral("name")).toString().toLower())
                        {
                            binProgramCommands.append(cmd.toObject().value(QStringLiteral("cmd")).toString());
                            binProgramPaths.append(firmwarePath);
                            binProgramSizes.append(cmd.toObject().value(QStringLiteral("size")).toInt());
                        }
                    }
                }

                foundMatch = true;
                break;
            }
        }

        if(!foundMatch)
        {
            QStringList dfuDeviceVidPidList = selectedDfuDeviceVidPid.split(QLatin1Char(':'));

            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("No DFU settings for the selected device!") + QString(QStringLiteral("\n\nVID: %1, PID: %2"))
                                  .arg(dfuDeviceVidPidList.first().toInt(nullptr, 16))
                                  .arg(dfuDeviceVidPidList.last().toInt(nullptr, 16)));

            CONNECT_END();
        }

        if (binProgramCommands.isEmpty() && QFileInfo(firmwarePath).exists())
        {
            QStringList dfuDeviceVidPidList = selectedDfuDeviceVidPid.split(QLatin1Char(':'));

            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("No matching interface for the selected file name!") + QString(QStringLiteral("\n\nVID: %1, PID: %2"))
                                  .arg(dfuDeviceVidPidList.first().toInt(nullptr, 16))
                                  .arg(dfuDeviceVidPidList.last().toInt(nullptr, 16)));

            CONNECT_END();
        }
    }

    QString dfuDeviceVidPid = selectedDfuDevice.isEmpty() ? boardTypeToDfuDeviceVidPid : selectedDfuDeviceVidPid;
    QString dfuDeviceSerial = QString(QStringLiteral(" -S %1")).arg(selectedDfuDevice.isEmpty()
        ? QStringLiteral("NULL")
        : selectedDfuDeviceSerialNumber);

    if(dfuDeviceSerial == QStringLiteral(" -S NULL"))
    {
        dfuDeviceSerial = QString();
    }

    if(forceFlashFSErase)
    {
        QTemporaryFile file;

        if(file.open())
        {
            if(file.write(QByteArray(FLASH_SECTOR_ERASE, 0)) == FLASH_SECTOR_ERASE)
            {
                file.setAutoRemove(false);
                file.close();

                QString command;
                Utils::Process process;

                for(int i = 0, j = eraseCommands.size(); i < j; i++)
                {
                    downloadFirmware(Tr::tr("Erasing Disk"), command, process,
                                     QFileInfo(file).canonicalFilePath(),
                                     dfuDeviceVidPid, eraseCommands.at(i) +
                                     ((justEraseFlashFs && ((i + 1) == j)) ? QStringLiteral(" --reset") : QStringLiteral("")) + dfuDeviceSerial);

                    if(((i + 1) != j) && (process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
                    {
                        QMessageBox box(QMessageBox::Critical, Tr::tr("Connect"), Tr::tr("Timeout Error!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                        box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                        box.setDefaultButton(QMessageBox::Ok);
                        box.setEscapeButton(QMessageBox::Cancel);
                        box.exec();

                        CONNECT_END();
                    }
                    else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
                    {
                        CONNECT_END();
                    }
                }

                if(justEraseFlashFs)
                {
                    if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        QString(QStringLiteral("%1%2%3%4")).arg(Tr::tr("Onboard Data Flash Erased!\n\n"))
                        .arg(Tr::tr("Your OpenMV Cam will start running its built-in self-test if no sd card is attached... this may take a while.\n\n"))
                        .arg(Tr::tr("Click OK when your OpenMV Cam's RGB LED starts blinking blue - which indicates the self-test is complete."))
                        .arg(Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                                "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open).")));

                    RECONNECT_WAIT_END();
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Error: %L1!").arg(file.errorString()));

                CONNECT_END();
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("Error: %L1!").arg(file.errorString()));

            CONNECT_END();
        }
    }

    QString command;
    Utils::Process process;

    if (!binProgramCommands.isEmpty())
    {
        if (romfsAccess == OPENMV_ROMFS_READ)
        {
            downloadFirmware(Tr::tr("Read ROMFS"), command, process,
                             QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)),
                             dfuDeviceVidPid, binProgramCommands.first() + QStringLiteral(" --reset") + dfuDeviceSerial,
                             true, binProgramSizes.first());

            if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                CONNECT_END();
            }

            CONNECT_END();
        }
        else if (romfsAccess == OPENMV_ROMFS_WRITE)
        {
            for(int i = 0, j = binProgramCommands.size(); i < j; i++)
            {
                downloadFirmware(Tr::tr("Write ROMFS"), command, process,
                                 QDir::toNativeSeparators(QDir::cleanPath(binProgramPaths.at(i))),
                                 dfuDeviceVidPid, binProgramCommands.at(i) +
                                 (((i + 1) == j) ? QStringLiteral(" --reset") : QStringLiteral("")) + dfuDeviceSerial);

                if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
                {
                    CONNECT_END();
                }

                if((i + 1) == j)
                {
                    CONNECT_END();
                }
            }
        }
        else
        {
            if (romfsAccess == OPENMV_ROMFS_RESET)
            {
                for(int i = 0, j = resetROMFSCommandsCmd.size(); i < j; i++)
                {
                    QString path = Core::ICore::allUsersResourcePath(QStringLiteral("firmware")).pathAppended(resetROMFSCommandsPath.at(i)).toString();

                    if (installTheLatestDevelopmentFirmware)
                    {
                        path = QFileInfo(firmwarePath).path() + QDir::separator() + QFileInfo(path).fileName();
                    }

                    downloadFirmware(Tr::tr("Flashing Firmware"), command, process,
                                     path,
                                     dfuDeviceVidPid, resetROMFSCommandsCmd.at(i) + dfuDeviceSerial);

                    if(((i + 1) != j) && (process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
                    {
                        QMessageBox box(QMessageBox::Critical, Tr::tr("Connect"), Tr::tr("DFU firmware update failed!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                        box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                        box.setDefaultButton(QMessageBox::Ok);
                        box.setEscapeButton(QMessageBox::Cancel);
                        box.exec();

                        CONNECT_END();
                    }
                    else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
                    {
                        CONNECT_END();
                    }
                }
            }

            for(int i = 0, j = binProgramCommands.size(); i < j; i++)
            {
                downloadFirmware(Tr::tr("Flashing Firmware"), command, process,
                                 QDir::toNativeSeparators(QDir::cleanPath(binProgramPaths.at(i))),
                                 dfuDeviceVidPid, binProgramCommands.at(i) +
                                 (((i + 1) == j) ? QStringLiteral(" --reset") : QStringLiteral("")) + dfuDeviceSerial);

                if(((i + 1) != j) && (process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
                {
                    QMessageBox box(QMessageBox::Critical, Tr::tr("Connect"), Tr::tr("DFU firmware update failed!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                    box.setDefaultButton(QMessageBox::Ok);
                    box.setEscapeButton(QMessageBox::Cancel);
                    box.exec();

                    CONNECT_END();
                }
                else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
                {
                    CONNECT_END();
                }

                if((i + 1) == j)
                {
                    if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("DFU firmware update complete!\n\n") +
                        Tr::tr("Click the Ok button after your OpenMV Cam has enumerated and finished running its built-in self test (blue led blinking - this takes a while).") +
                        Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                           "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open)."));

                    RECONNECT_WAIT_END();
                }
            }
        }
    }
    else
    {
        for(int i = 0, j = programCommandsCmd.size(); i < j; i++)
        {
            downloadFirmware(Tr::tr("Flashing Firmware"), command, process,
                             Core::ICore::allUsersResourcePath(QStringLiteral("firmware")).pathAppended(programCommandsPath.at(i)).toString(),
                             dfuDeviceVidPid, programCommandsCmd.at(i) +
                             (((i + 1) == j) ? QStringLiteral(" --reset") : QStringLiteral("")) + dfuDeviceSerial, 
                             false, 0, (i == 0) ? extraMessage : QString());

            if(((i + 1) != j) && (process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("Connect"), Tr::tr("DFU firmware update failed!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();

                CONNECT_END();
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                CONNECT_END();
            }

            if((i + 1) == j)
            {
                if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("DFU firmware update complete!\n\n") +
                    Tr::tr("Click the Ok button after your OpenMV Cam has enumerated and finished running its built-in self test (blue led blinking - this takes a while).") +
                    Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                       "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open)."));

                RECONNECT_WAIT_END();
            }
        }
    }
}

} // namespace Internal
} // namespace OpenMV
