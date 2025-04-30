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

void OpenMVPlugin::openmvArduinoDFUBootloader(bool forceFlashFSErase,
                                              bool justEraseFlashFs,
                                              bool installTheLatestDevelopmentFirmware,
                                              const QString &firmwarePath,
                                              const QString &selectedDfuDevice,
                                              OpenMVROMFSAccess romfsAccess)
{
    // Stopping ///////////////////////////////////////////////////////

    if(selectedDfuDevice.isEmpty())
    {
        QEventLoop loop;

        connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                &loop, &QEventLoop::quit);

        if(!m_portPath.isEmpty())
        {
#if defined(Q_OS_WIN)
            wchar_t driveLetter[m_portPath.size()];
            m_portPath.toWCharArray(driveLetter);

            if(!ejectVolume(driveLetter[0]))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Disconnect"),
                    Tr::tr("Failed to eject \"%L1\"!").arg(m_portPath));
            }
#elif defined(Q_OS_LINUX)
            Utils::Process process;
            std::chrono::seconds timeout(10);
            process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(QStringLiteral("umount")),
                                                  QStringList() << QDir::toNativeSeparators(QDir::cleanPath(m_portPath))));
            process.runBlocking(timeout, Utils::EventLoopMode::On);

            if(process.result() != Utils::ProcessResult::FinishedWithSuccess)
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Disconnect"),
                    Tr::tr("Failed to eject \"%L1\"!").arg(m_portPath));
            }
#elif defined(Q_OS_MAC)
            if(sync_volume_np(m_portPath.toUtf8().constData(), SYNC_VOLUME_FULLSYNC | SYNC_VOLUME_WAIT) < 0)
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Disconnect"),
                    Tr::tr("Failed to eject \"%L1\"!").arg(m_portPath));
            }
#endif
        }

        m_iodevice->sysReset(true);
        m_iodevice->close();

        loop.exec();

        QElapsedTimer elaspedTimer;
        elaspedTimer.start();

        while(!elaspedTimer.hasExpired(RESET_TO_DFU_SEARCH_TIME))
        {
            QApplication::processEvents();
        }
    }

    if(Utils::HostOsInfo::isLinuxHost()
    && ((QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
    || (QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))))
    {
        if(QMessageBox::warning(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            Tr::tr("DFU Util may not be stable on this platform. If loading fails please use a regular computer."),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Cancel)
        {
            CONNECT_END();
        }
    }

    // Erase Flash ////////////////////////////////////////

    QString selectedDfuDeviceVidPid = selectedDfuDevice.isEmpty() ? QString() : selectedDfuDevice.split(QStringLiteral(",")).first();
    QString selectedDfuDeviceSerialNumber = selectedDfuDevice.isEmpty() ? QString() : selectedDfuDevice.split(QStringLiteral(",")).last();

    QString boardTypeToDfuDeviceVidPid;
    QStringList eraseCommands, extraProgramAddrCommands, extraProgramPathCommands;
    QStringList resetROMFSAddrCommands, resetROMFSPathCommands;
    QString binProgramCommand, dfuProgramCommand, romfsProgramCommand;

    if(selectedDfuDevice.isEmpty())
    {
        bool foundMatch = false;

        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            QJsonObject obj = val.toObject();

            if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("arduino_dfu"))
            {
                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();

                QJsonArray appvidpidArray = bootloaderSettings.value(QStringLiteral("appvidpid")).toArray();
                bool isApp = appvidpidArray.isEmpty();

                if(!isApp)
                {
                    for(const QJsonValue &appvidpid : appvidpidArray)
                    {
                        QStringList vidpid = appvidpid.toString().split(QStringLiteral(":"));

                        if((vidpid.at(0).toInt(nullptr, 16) == m_boardVID) && (vidpid.at(1).toInt(nullptr, 16) == m_boardPID))
                        {
                            isApp = true;
                            break;
                        }
                    }
                }

                if(isApp && (m_boardType == obj.value(QStringLiteral("boardType")).toString()))
                {
                    boardTypeToDfuDeviceVidPid = obj.value(QStringLiteral("bootloaderVidPid")).toString();

                    QJsonArray eraseCommandsArray = bootloaderSettings.value(QStringLiteral("eraseCommands")).toArray();
                    for(const QJsonValue &command : eraseCommandsArray)
                    {
                        eraseCommands.append(command.toString());
                    }

                    QJsonArray extraProgramCommandsArray = bootloaderSettings.value(QStringLiteral("extraProgramCommands")).toArray();
                    for(const QJsonValue &command : extraProgramCommandsArray)
                    {
                        QJsonObject obj2 = command.toObject();
                        extraProgramAddrCommands.append(obj2.value(QStringLiteral("addr")).toString());
                        extraProgramPathCommands.append(obj2.value(QStringLiteral("path")).toString());
                    }

                    QJsonArray resetROMFSCommandsArray = bootloaderSettings.value(QStringLiteral("resetROMFSCommands")).toArray();
                    for(const QJsonValue &command : resetROMFSCommandsArray)
                    {
                        QJsonObject obj2 = command.toObject();
                        resetROMFSAddrCommands.append(obj2.value(QStringLiteral("addr")).toString());
                        resetROMFSPathCommands.append(obj2.value(QStringLiteral("path")).toString());
                    }

                    binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();
                    dfuProgramCommand = bootloaderSettings.value(QStringLiteral("dfuProgramCommand")).toString();
                    romfsProgramCommand = bootloaderSettings.value(QStringLiteral("romfsProgramCommand")).toString();
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
    }
    else
    {
        bool foundMatch = false;

        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            QJsonObject obj = val.toObject();

            if((selectedDfuDeviceVidPid.toLower() == obj.value(QStringLiteral("bootloaderVidPid")).toString().toLower())
            && (obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("arduino_dfu")))
            {
                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();

                QJsonArray eraseCommandsArray = bootloaderSettings.value(QStringLiteral("eraseCommands")).toArray();
                for(const QJsonValue &command : eraseCommandsArray)
                {
                    eraseCommands.append(command.toString());
                }

                QJsonArray extraProgramCommandsArray = bootloaderSettings.value(QStringLiteral("extraProgramCommands")).toArray();
                for(const QJsonValue &command : extraProgramCommandsArray)
                {
                    QJsonObject obj2 = command.toObject();
                    extraProgramAddrCommands.append(obj2.value(QStringLiteral("addr")).toString());
                    extraProgramPathCommands.append(obj2.value(QStringLiteral("path")).toString());
                }

                QJsonArray resetROMFSCommandsArray = bootloaderSettings.value(QStringLiteral("resetROMFSCommands")).toArray();
                for(const QJsonValue &command : resetROMFSCommandsArray)
                {
                    QJsonObject obj2 = command.toObject();
                    resetROMFSAddrCommands.append(obj2.value(QStringLiteral("addr")).toString());
                    resetROMFSPathCommands.append(obj2.value(QStringLiteral("path")).toString());
                }

                binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();
                dfuProgramCommand = bootloaderSettings.value(QStringLiteral("dfuProgramCommand")).toString();
                romfsProgramCommand = bootloaderSettings.value(QStringLiteral("romfsProgramCommand")).toString();
                foundMatch = true;
                break;
            }
        }

        if(!foundMatch)
        {
            QStringList dfuDeviceVidPidList = selectedDfuDeviceVidPid.split(QLatin1Char(':'));

            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("No DFU settings for the selected device!") + QString(QStringLiteral("\n\nVID: %1, PID: %2")).arg(dfuDeviceVidPidList.first().toInt(nullptr, 16)).arg(dfuDeviceVidPidList.last().toInt(nullptr, 16)));

            CONNECT_END();
        }
    }

    QString dfuDeviceVidPid = selectedDfuDevice.isEmpty() ? boardTypeToDfuDeviceVidPid : selectedDfuDeviceVidPid;
    QString dfuDeviceSerial = QString(QStringLiteral(" -S %1")).arg(selectedDfuDevice.isEmpty()
        ? (m_boardId.mid(16, 8) + m_boardId.mid(8, 8) + m_boardId.mid(0, 8)) // The Arduino DFU Bootloader reports its serial number word reversed.
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
                    downloadFirmware(Tr::tr("Erasing Disk"), command, process, QFileInfo(file).canonicalFilePath(), dfuDeviceVidPid, eraseCommands.at(i) + ((justEraseFlashFs && ((i + 1) == j)) ? QStringLiteral(":leave") : QStringLiteral("")) + dfuDeviceSerial);

                    if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
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

    // Program Flash //////////////////////////////////////
    {
        if (romfsAccess == OPENMV_ROMFS_READ)
        {
            QString command;
            Utils::Process process;

            downloadFirmware(Tr::tr("Read ROMFS"), command, process,
                             QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)),
                             dfuDeviceVidPid, romfsProgramCommand + dfuDeviceSerial, true);

            if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                CONNECT_END();
            }

            CONNECT_END();
        }
        else if (romfsAccess == OPENMV_ROMFS_WRITE)
        {
            QString command;
            Utils::Process process;

            downloadFirmware(Tr::tr("Write ROMFS"), command, process,
                             QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)),
                             dfuDeviceVidPid, romfsProgramCommand + dfuDeviceSerial);

            if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                CONNECT_END();
            }

            CONNECT_END();
        }
        else if (romfsAccess == OPENMV_ROMFS_RESET)
        {
            QString command;
            Utils::Process process;

            for(int i = 0, j = resetROMFSAddrCommands.size(); i < j; i++)
            {
                QString path = Core::ICore::allUsersResourcePath(QStringLiteral("firmware")).pathAppended(resetROMFSPathCommands.at(i)).toString();

                if (installTheLatestDevelopmentFirmware)
                {
                    path = QFileInfo(firmwarePath).path() + QDir::separator() + QFileInfo(path).fileName();
                }

                downloadFirmware(Tr::tr("Flashing Firmware"), command, process, path, dfuDeviceVidPid, resetROMFSAddrCommands.at(i) + dfuDeviceSerial);

                if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
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

        // Extra Program Flash ////////////////////////////////
        {
            QString command;
            Utils::Process process;

            for(int i = 0, j = extraProgramAddrCommands.size(); i < j; i++)
            {
                downloadFirmware(Tr::tr("Flashing Firmware"), command, process, Core::ICore::allUsersResourcePath(QStringLiteral("firmware")).
                                 pathAppended(extraProgramPathCommands.at(i)).toString(), dfuDeviceVidPid, extraProgramAddrCommands.at(i) + dfuDeviceSerial);

                if((process.result() != Utils::ProcessResult::FinishedWithSuccess) && (process.result() != Utils::ProcessResult::TerminatedAbnormally))
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

        QString command;
        Utils::Process process;
        downloadFirmware(Tr::tr("Flashing Firmware"), command, process, QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)), dfuDeviceVidPid,
                         (firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive) ? binProgramCommand :
                          (firmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive) ? dfuProgramCommand :
                                                                                                 romfsProgramCommand)) + dfuDeviceSerial);

        if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
        {
            if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("DFU firmware update complete!\n\n") +
                Tr::tr("Click the Ok button after your OpenMV Cam has enumerated and finished running its built-in self test (blue led blinking - this takes a while).") +
                Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                   "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open)."));

            RECONNECT_WAIT_END();
        }
        else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
        {
            CONNECT_END();
        }
        else
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
    }
}

} // namespace Internal
} // namespace OpenMV
