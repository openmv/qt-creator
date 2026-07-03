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

void OpenMVPlugin::openmvBossacBootloader(bool forceFlashFSErase,
                                          bool justEraseFlashFs,
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

        flushPortPath();

        m_iodevice->sysReset(!justEraseFlashFs);
        m_iodevice->close();

        loop.exec();

        QElapsedTimer elaspedTimer;
        elaspedTimer.start();

        while(!elaspedTimer.hasExpired(RESET_TO_DFU_SEARCH_TIME))
        {
            QApplication::processEvents();
        }
    }

    QString selectedDfuDeviceVidPid = selectedDfuDevice.isEmpty() ? QString() : selectedDfuDevice;
    QStringList selectedDfuDeviceVidPidList = selectedDfuDeviceVidPid.split(QLatin1Char(':'));
    QString dfuDevicePort;

    bool old = false;
    int oldVid = 0;
    int oldPid = 0;

    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
    {
        QJsonObject object = value.toObject();
        QStringList bootloaderVidPid = object.value(QStringLiteral("bootloaderVidPid")).toString().split(QStringLiteral(":"));

        if ((bootloaderVidPid.size() == 2)
        && (object.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("bossac")))
        {
            QJsonObject bootloaderSettings = object.value(QStringLiteral("bootloaderSettings")).toObject();
            QStringList altvidpid = bootloaderSettings.value(QStringLiteral("altvidpid")).toString().split(QStringLiteral(":"));

            if((altvidpid.size() == 2) && (selectedDfuDeviceVidPidList.size() == 2)
            && ((selectedDfuDeviceVidPidList.first().toInt(nullptr, 16) == altvidpid.at(0).toInt(nullptr, 16)))
            && ((selectedDfuDeviceVidPidList.last().toInt(nullptr, 16) == altvidpid.at(1).toInt(nullptr, 16))))
            {
                old = true;
                oldVid = bootloaderVidPid.at(0).toInt(nullptr, 16);
                oldPid = bootloaderVidPid.at(1).toInt(nullptr, 16);
                break;
            }
        }
    }

    if(old)
    {
        for(QSerialPortInfo raw_port : QSerialPortInfo::availablePorts())
        {
            MyQSerialPortInfo port(raw_port);

            if(port.hasVendorIdentifier() && ((port.vendorIdentifier() == selectedDfuDeviceVidPidList.first().toInt(nullptr, 16)))
            && port.hasProductIdentifier() && ((port.productIdentifier() == selectedDfuDeviceVidPidList.last().toInt(nullptr, 16))))
            {
                dfuDevicePort = port.portName();
                break;
            }
        }

        if(dfuDevicePort.isEmpty())
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("BOSSAC device %1 missing!").arg(selectedDfuDeviceVidPid));

            CONNECT_END();
        }

        Utils::Process process;
        bossacRunBootloader(process, dfuDevicePort);

        selectedDfuDeviceVidPid = QString(QStringLiteral("%1:%2").arg(oldVid, 4, 16, QLatin1Char('0')).arg(oldPid, 4, 16, QLatin1Char('0')));

        QElapsedTimer elaspedTimer;
        elaspedTimer.start();
        dfuDevicePort = QString();

        while(dfuDevicePort.isEmpty() && (!elaspedTimer.hasExpired(RESET_TO_DFU_SEARCH_TIME)))
        {
            QApplication::processEvents();

            for(QSerialPortInfo raw_port : QSerialPortInfo::availablePorts())
            {
                MyQSerialPortInfo port(raw_port);

                if (port.hasVendorIdentifier() && port.hasProductIdentifier())
                {
                    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        QJsonObject object = value.toObject();

                        if (object.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("bossac"))
                        {
                            QStringList vidpid = object.value(QStringLiteral("bootloaderVidPid")).toString().split(QStringLiteral(":"));

                            if((vidpid.size() == 2)
                            && ((port.vendorIdentifier() == vidpid.at(0).toInt(nullptr, 16)))
                            && ((port.productIdentifier() == vidpid.at(1).toInt(nullptr, 16))))
                            {
                                dfuDevicePort = port.portName();
                                break;
                            }
                        }
                    }
                }

                if (!dfuDevicePort.isEmpty())
                {
                    break;
                }
            }
        }

        if(dfuDevicePort.isEmpty())
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("BOSSAC device %1 missing!").arg(selectedDfuDeviceVidPid));

            CONNECT_END();
        }
    }

    QString boardTypeToDfuDeviceVidPid, binProgramCommand, boardDisplayName;

    if(selectedDfuDevice.isEmpty())
    {
        bool foundMatch = false;

        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            QJsonObject obj = val.toObject();

            if((m_boardType == obj.value(QStringLiteral("boardType")).toString())
            && (obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("bossac")))
            {
                boardTypeToDfuDeviceVidPid = obj.value(QStringLiteral("bootloaderVidPid")).toString();
                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();
                binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();
                boardDisplayName = obj.value(QStringLiteral("boardDisplayName")).toString();
                foundMatch = true;
                break;
            }
        }

        if(!foundMatch)
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("No BOSSAC settings for the selected board type!"));

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
            && (obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("bossac")))
            {
                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();
                binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();
                boardDisplayName = obj.value(QStringLiteral("boardDisplayName")).toString();
                foundMatch = true;
                break;
            }
        }

        if(!foundMatch)
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("No BOSSAC settings for the selected device!"));

            CONNECT_END();
        }
    }

    if(forceFlashFSErase && justEraseFlashFs)
    {
        Utils::Process process;
        bossacReset(process, dfuDevicePort);

        if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            QString(QStringLiteral("%1"))
            .arg(Tr::tr("Your %1 doesn't have an internal FAT file system.").arg(boardDisplayName)));

        if(selectedDfuDevice.isEmpty())
        {
            RECONNECT_WAIT_END();
        }
        else
        {
            RECONNECT_END();
        }
    }

    if (romfsAccess != OPENMV_ROMFS_NONE)
    {
        Utils::Process process;
        bossacReset(process, dfuDevicePort);

        if(m_autoUpdate.isEmpty()) QMessageBox::information(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            QString(QStringLiteral("%1"))
            .arg(Tr::tr("Your %1 doesn't have an ROM file system.").arg(boardDisplayName)));

        if(selectedDfuDevice.isEmpty())
        {
            RECONNECT_WAIT_END();
        }
        else
        {
            RECONNECT_END();
        }
    }

    QString dfuDeviceVidPid = selectedDfuDevice.isEmpty() ? boardTypeToDfuDeviceVidPid : selectedDfuDeviceVidPid;
    QStringList dfuDeviceVidPidList = dfuDeviceVidPid.split(QLatin1Char(':'));

    QElapsedTimer elaspedTimer;
    elaspedTimer.start();
    dfuDevicePort = QString();

    while(dfuDevicePort.isEmpty() && (!elaspedTimer.hasExpired(RESET_TO_DFU_SEARCH_TIME)))
    {
        QApplication::processEvents();

        for(QSerialPortInfo raw_port : QSerialPortInfo::availablePorts())
        {
            MyQSerialPortInfo port(raw_port);

            if(port.hasVendorIdentifier() && ((port.vendorIdentifier() == dfuDeviceVidPidList.first().toInt(nullptr, 16)))
            && port.hasProductIdentifier() && ((port.productIdentifier() == dfuDeviceVidPidList.last().toInt(nullptr, 16))))
            {
                dfuDevicePort = port.portName();
                break;
            }
        }
    }

    if(dfuDevicePort.isEmpty())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            Tr::tr("BOSSAC device %1 missing!").arg(dfuDeviceVidPid));

        CONNECT_END();
    }

    QString command;
    Utils::Process process;
    bossacDownloadFirmware(Tr::tr("Flashing Firmware"), command, process, QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)), dfuDevicePort, binProgramCommand);

    if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
    {
        if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            viewerModeFirmwareText(Tr::tr("BOSSAC firmware update complete!\n\n") +
            Tr::tr("Click the Ok button after your OpenMV Cam has enumerated and finished running its built-in self test (blue led blinking - this takes a while).") +
            Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
               "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open).")));

        RECONNECT_WAIT_END();
    }
    else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
    {
        CONNECT_END();
    }
    else
    {
        QMessageBox box(QMessageBox::Critical, Tr::tr("Connect"), Tr::tr("BOSSAC firmware update failed!"), QMessageBox::Ok, Core::ICore::dialogParent(),
            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
        box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
        box.setDefaultButton(QMessageBox::Ok);
        box.setEscapeButton(QMessageBox::Cancel);
        box.exec();
        CONNECT_END();
    }
}

} // namespace Internal
} // namespace OpenMV
