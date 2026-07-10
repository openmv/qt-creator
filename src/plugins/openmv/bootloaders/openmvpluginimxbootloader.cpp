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

#include <QGuiApplication>

namespace OpenMV {
namespace Internal {

void OpenMVPlugin::openmvIMXBootloader(const QString &forceFirmwarePath,
                                       bool forceFlashFSErase,
                                       bool justEraseFlashFs,
                                       bool installTheLatestDevelopmentFirmware,
                                       const QString &firmwarePath,
                                       Utils::QtcSettings *settings,
                                       bool forceBootloaderBricked,
                                       QString originalFirmwareFolder,
                                       const QString &selectedDfuDevice,
                                       OpenMVROMFSAccess romfsAccess)
{
    QJsonObject outObj;

    if(originalFirmwareFolder.isEmpty())
    {
        QList<QPair<int, int> > vidpidlist = imxVidPidList(m_firmwareSettings, false, true);
        QMap<QString, QString> mappings;

        for (const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            QJsonObject obj = val.toObject();

            if((!obj.value(QStringLiteral("hidden")).toBool())
            && (obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("imx")))
            {
                QString a = val.toObject().value(QStringLiteral("boardDisplayName")).toString();
                QStringList vidpid = val.toObject().value(QStringLiteral("bootloaderVidPid")).toString().split(QStringLiteral(":"));

                if(vidpidlist.contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16))))
                {
                    mappings.insert(a, val.toObject().value(QStringLiteral("boardFirmwareFolder")).toString());
                }
            }
        }

        if(!mappings.isEmpty())
        {
            int index = mappings.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE_IMX).toString());
            bool ok = mappings.size() == 1;
            QString temp = (mappings.size() == 1) ? mappings.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),
                Tr::tr("Connect"), Tr::tr("Please select the board type"),
                mappings.keys(), (index != -1) ? index : 0, false, &ok,
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

            if(ok)
            {
                settings->setValue(LAST_BOARD_TYPE_STATE_IMX, temp);
                originalFirmwareFolder = mappings.value(temp);
            }
            else
            {
                CONNECT_END();
            }
        }
    }

    if(!originalFirmwareFolder.isEmpty())
    {
        bool foundMatch = false;

        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            QJsonObject obj = val.toObject();

            if((originalFirmwareFolder == obj.value(QStringLiteral("boardFirmwareFolder")).toString())
            && (obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("imx")))
            {
                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();
                QString secureBootloaderPath = OpenMVThirdParty::firmwareRootForBoard(obj).
                        pathAppended(originalFirmwareFolder).
                        pathAppended(bootloaderSettings.value(QStringLiteral("sdphost_flash_loader_path")).toString()).toString();
                QString bootloaderPath = OpenMVThirdParty::firmwareRootForBoard(obj).
                        pathAppended(originalFirmwareFolder).
                        pathAppended(bootloaderSettings.value(QStringLiteral("blhost_secure_bootloader_path")).toString()).toString();
                QString romfsPath = OpenMVThirdParty::firmwareRootForBoard(obj).
                        pathAppended(originalFirmwareFolder).
                        pathAppended(bootloaderSettings.value(QStringLiteral("blhost_romfs_path")).toString()).toString();

                if ((romfsAccess == OPENMV_ROMFS_READ) || (romfsAccess == OPENMV_ROMFS_WRITE))
                {
                    romfsPath = firmwarePath;
                }
                else if (installTheLatestDevelopmentFirmware)
                {
                    romfsPath = QFileInfo(firmwarePath).path() + QDir::separator() + QFileInfo(romfsPath).fileName();
                }

                outObj = bootloaderSettings;
                outObj.insert(QStringLiteral("sdphost_flash_loader_path"), secureBootloaderPath);
                outObj.insert(QStringLiteral("blhost_secure_bootloader_path"), bootloaderPath);
                outObj.insert(QStringLiteral("blhost_secure_bootloader_length"),
                        QString::number(QFileInfo(bootloaderPath).size(), 16).prepend(QStringLiteral("0x")));
                outObj.insert(QStringLiteral("blhost_firmware_path"), firmwarePath);
                outObj.insert(QStringLiteral("blhost_firmware_length"),
                        QString::number(QFileInfo(firmwarePath).size(), 16).prepend(QStringLiteral("0x")));
                outObj.insert(QStringLiteral("blhost_romfs_path"), romfsPath);
                outObj.insert(QStringLiteral("blhost_romfs_length"),
                        QString::number(QFileInfo(romfsPath).size(), 16).prepend(QStringLiteral("0x")));

                if (firmwarePath.endsWith(".img"))
                {
                    outObj.insert(QStringLiteral("blhost_firmware_address"), bootloaderSettings.value(QStringLiteral("blhost_romfs_address")));
                }

                foundMatch = true;
                break;
            }
        }

        if(!foundMatch)
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("No IMX settings for the selected board type %L1!").arg(originalFirmwareFolder));

            CONNECT_END();
        }
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            Tr::tr("No IMX settings found!"));

        CONNECT_END();
    }

    if(selectedDfuDevice.isEmpty())
    {
        flushPortPath();

        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

        // Create the pycache for blhost before running during the time limited process.
        imxGetDevice(outObj);

        QProgressDialog dialog(forceBootloaderBricked ? QString(QStringLiteral("%1%2")).arg(Tr::tr("Disconnect your OpenMV Cam and then reconnect it...")).arg(justEraseFlashFs ? QString() : Tr::tr("\n\nHit cancel to skip to SBL reprogramming.")) : Tr::tr("Connecting... (Hit cancel if this takes more than 5 seconds)."), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
            (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
        dialog.setWindowModality(Qt::ApplicationModal);
        dialog.setAttribute(Qt::WA_ShowWithoutActivating);
        dialog.show();

        bool canceled = false;
        bool *canceledPtr = &canceled;

        connect(&dialog, &QProgressDialog::canceled, this, [canceledPtr] {
            *canceledPtr = true;
        });

        QEventLoop loop;

        connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                &loop, &QEventLoop::quit);

        m_iodevice->sysReset(false);
        m_iodevice->close();

        loop.exec();

        while((!imxGetDevice(outObj)) && (!canceled))
        {
            QApplication::processEvents();
        }

        QApplication::restoreOverrideCursor();

        bool userCanceled = canceled;
        dialog.close();

        if(userCanceled)
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("Unable to connect to your OpenMV Cam's normal bootloader!"));

            if((!justEraseFlashFs) && forceFirmwarePath.isEmpty() && QMessageBox::question(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("%1 can still try to repair your OpenMV Cam using your OpenMV Cam's SBL Bootloader.\n\n"
                   "Continue?").arg(QGuiApplication::applicationDisplayName()),
                QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
            == QMessageBox::Ok)
            {
                if(QMessageBox::information(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Disconnect your OpenMV Cam from your computer, add a jumper wire between the SBL and 3.3V pins, and then reconnect your OpenMV Cam to your computer.\n\n"
                       "Click the Ok button after your OpenMV Cam's SBL Bootloader has enumerated."),
                    QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                == QMessageBox::Ok)
                {
                    if(imxDownloadBootloaderAndFirmware(outObj, forceFlashFSErase, justEraseFlashFs, romfsAccess))
                    {
                        if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
                            Tr::tr("Connect"),
                            viewerModeFirmwareText(Tr::tr("Firmware update complete!\n\n") +
                            Tr::tr("Disconnect your OpenMV Cam from your computer, remove the jumper wire between the SBL and 3.3V pins, and then reconnect your OpenMV Cam to your computer.\n\n") +
                            Tr::tr("Click the Ok button after your OpenMV Cam has enumerated and finished running its built-in self test (blue led blinking - this takes a while).") +
                            Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                               "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open)."),
                            Tr::tr("Disconnect the device from your computer, remove the jumper wire between the SBL and 3.3V pins, and then reconnect it.")));

                        RECONNECT_WAIT_END();
                    }
                }
            }

            CONNECT_END();
        }
        else
        {
            if ((romfsAccess == OPENMV_ROMFS_READ) || (romfsAccess == OPENMV_ROMFS_WRITE))
            {
                imxDownloadFirmware(outObj, forceFlashFSErase, justEraseFlashFs, romfsAccess);
                CONNECT_END();
            }
            else if(imxDownloadFirmware(outObj, forceFlashFSErase, justEraseFlashFs, romfsAccess))
            {
                if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    viewerModeFirmwareText(QString(QStringLiteral("%1%2%3%4")).arg((justEraseFlashFs ? Tr::tr("Onboard Data Flash Erased!\n\n") : Tr::tr("Firmware Upgrade complete!\n\n")))
                    .arg(Tr::tr("Your OpenMV Cam will start running its built-in self-test if no sd card is attached... this may take a while.\n\n"))
                    .arg(Tr::tr("Click OK when your OpenMV Cam's RGB LED starts blinking blue - which indicates the self-test is complete."))
                    .arg(Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                            "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open)."))));

                RECONNECT_WAIT_END();
            }
            else
            {
                CONNECT_END();
            }
        }
    }
    else
    {
        QStringList vidpid = selectedDfuDevice.split(QStringLiteral(",")).first().split(QStringLiteral(":"));
        QPair<int, int> entry(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16));

        // SPD Mode (SBL)
        if(imxVidPidList(m_firmwareSettings, true, false).contains(entry))
        {
            if(imxDownloadBootloaderAndFirmware(outObj, forceFlashFSErase, justEraseFlashFs, romfsAccess))
            {
                if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    viewerModeFirmwareText(Tr::tr("Firmware update complete!\n\n") +
                    Tr::tr("If you are forcing SBL mode, disconnect your OpenMV Cam from your computer and remove the SBL wire jumper. "
                           "Then reconnect your OpenMV Cam to your computer.\n\n") +
                    Tr::tr("Click the Ok button after your OpenMV Cam has enumerated and finished running its built-in self test (blue led blinking - this takes a while).") +
                    Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                       "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open)."),
                    Tr::tr("If you are forcing SBL mode, disconnect the device from your computer and remove the SBL wire jumper, then reconnect it.")));

                RECONNECT_WAIT_END();
            }
            else
            {
                CONNECT_END();
            }
        }
        // BL Mode
        else if(imxVidPidList(m_firmwareSettings, false, true).contains(entry))
        {
            if ((romfsAccess == OPENMV_ROMFS_READ) || (romfsAccess == OPENMV_ROMFS_WRITE))
            {
                imxDownloadFirmware(outObj, forceFlashFSErase, justEraseFlashFs, romfsAccess);
                CONNECT_END();
            }
            else if(imxDownloadFirmware(outObj, forceFlashFSErase, justEraseFlashFs, romfsAccess))
            {
                if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    viewerModeFirmwareText(QString(QStringLiteral("%1%2%3%4")).arg((justEraseFlashFs ? Tr::tr("Onboard Data Flash Erased!\n\n") : Tr::tr("Firmware Upgrade complete!\n\n")))
                    .arg(Tr::tr("Your OpenMV Cam will start running its built-in self-test if no sd card is attached... this may take a while.\n\n"))
                    .arg(Tr::tr("Click OK when your OpenMV Cam's RGB LED starts blinking blue - which indicates the self-test is complete."))
                    .arg(Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                            "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open)."))));

                RECONNECT_WAIT_END();
            }
            else
            {
                CONNECT_END();
            }
        }
    }
}

} // namespace Internal
} // namespace OpenMV
