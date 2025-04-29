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

void OpenMVPlugin::openmvInternalBootloader(const QString &forceFirmwarePath,
                                            bool forceFlashFSErase,
                                            bool justEraseFlashFs,
                                            bool installTheLatestDevelopmentFirmware,
                                            const QString &previousMapping,
                                            const QString &selectedPort,
                                            bool forceBootloaderBricked,
                                            bool previousMappingSet,
                                            const QString &originalFirmwareFolder,
                                            const QString &firmwarePath,
                                            int originalEraseFlashSectorStart,
                                            int originalEraseFlashSectorEnd,
                                            int originalEraseFlashSectorAllStart,
                                            int originalEraseFlashSectorAllEnd,
                                            const QJsonObject &originalFallbackBootloaderSettings,
                                            const QString &originalDfuVidPid,
                                            bool dfuNoDialogs,
                                            OpenMVROMFSAccess romfsAccess)
{
    QStringList fallbackVidPid = originalFallbackBootloaderSettings.value(QStringLiteral("vidpid")).toString().split(QStringLiteral(":"));
    int fallbackVid = 0, fallbackPid = 0;

    if (fallbackVidPid.size() == 2)
    {
        fallbackVid = fallbackVidPid.at(0).toInt(nullptr, 16);
        fallbackPid = fallbackVidPid.at(1).toInt(nullptr, 16);
    }

    bool skipStuff = justEraseFlashFs || (romfsAccess != OPENMV_ROMFS_NONE);

    for (bool tryFastMode = true;; )
    {
        QFile file(firmwarePath);

        if(skipStuff || file.open(QIODevice::ReadOnly))
        {
            QByteArray data = skipStuff ? QByteArray() : file.readAll();

            if(skipStuff || ((file.error() == QFile::NoError) && (!data.isEmpty())))
            {
                if(!skipStuff) file.close();

                int qspif_start_block = int();
                int qspif_max_block = int();
                int qspif_block_size_in_bytes = int();
                int packet_chunksize = int();
                int packet_batchsize = int();

                // Start Bootloader ///////////////////////////////////
                {
                    bool done2 = bool(), loopExit = false, done22 = false;
                    bool *done2Ptr = &done2, *loopExitPtr = &loopExit, *done2Ptr2 = &done22;
                    int version2 = int(), *version2Ptr = &version2;
                    bool highspeed2 = bool(), *highspeed2Ptr = &highspeed2;

                    QMetaObject::Connection conn = connect(m_ioport, &OpenMVPluginSerialPort::bootloaderStartResponse,
                        this, [done2Ptr, loopExitPtr, version2Ptr, highspeed2Ptr] (bool done, int version, bool highspeed) {
                        *done2Ptr = done;
                        *loopExitPtr = true;
                        *version2Ptr = version;
                        *highspeed2Ptr = highspeed;
                    });

                    QMetaObject::Connection conn2 = connect(m_ioport, &OpenMVPluginSerialPort::bootloaderStopResponse,
                        this, [done2Ptr2] {
                        *done2Ptr2 = true;
                    });

                    QProgressDialog dialog(((!tryFastMode) || forceBootloaderBricked)
                            ? QString(QStringLiteral("%1%2")).arg(previousMappingSet
                                ? Tr::tr("Reconnect your OpenMV Cam...")
                                : Tr::tr("Disconnect your OpenMV Cam and then reconnect it...")).arg((previousMappingSet || justEraseFlashFs || (!forceFirmwarePath.isEmpty()))
                                    ? QString()
                                    : Tr::tr("\n\nHit cancel to skip to bootloader recovery reprogramming."))
                            : Tr::tr("Connecting... (Hit cancel if this takes more than 5 seconds)."), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                        (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
                    dialog.setWindowModality(Qt::ApplicationModal);
                    dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                    dialog.show();

                    connect(&dialog, &QProgressDialog::canceled,
                            m_ioport, &OpenMVPluginSerialPort::bootloaderStop);

                    QEventLoop loop, loop0, loop1;

                    connect(m_ioport, &OpenMVPluginSerialPort::bootloaderStartResponse,
                            &loop, &QEventLoop::quit);

                    connect(m_ioport, &OpenMVPluginSerialPort::bootloaderStopResponse,
                            &loop0, &QEventLoop::quit);

                    connect(m_ioport, &OpenMVPluginSerialPort::bootloaderResetResponse,
                            &loop1, &QEventLoop::quit);

                    m_ioport->bootloaderStart(selectedPort);

                    QApplication::setOverrideCursor(Qt::WaitCursor);

                    while(!loopExit)
                    {
                        QSerialPortInfo::availablePorts();
                        QApplication::processEvents();
                        // Keep updating the list of available serial
                        // ports for the non-gui serial thread.

                        if (fallbackVidPid.size() == 2)
                        {
                            for(const QString &device : getDevices())
                            {
                                QStringList vidpid = device.split(QStringLiteral(",")).first().split(QStringLiteral(":"));

                                if(vidpid.at(0).toInt(nullptr, 16) == fallbackVid && vidpid.at(1).toInt(nullptr, 16) == fallbackPid)
                                {
                                    dialog.close(); // emits stop

                                    if(!done22)
                                    {
                                        loop0.exec();
                                    }

                                    m_ioport->bootloaderReset();
                                    loop1.exec();
                                    disconnect(conn);
                                    disconnect(conn2);
                                    QApplication::restoreOverrideCursor();

                                    openmvDFUBootloader(forceFlashFSErase,
                                                        justEraseFlashFs,
                                                        installTheLatestDevelopmentFirmware,
                                                        firmwarePath,
                                                        device,
                                                        romfsAccess);
                                    return;
                                }
                            }
                        }
                    }

                    if ((!loopExit) && (romfsAccess != OPENMV_ROMFS_NONE))
                    {
                        dialog.close(); // emits stop

                        if(!done22)
                        {
                            loop0.exec();
                        }

                        m_ioport->bootloaderReset();
                        loop1.exec();
                        disconnect(conn);
                        disconnect(conn2);

                        QEventLoop loop2;

                        connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                                &loop2, &QEventLoop::quit);

                        m_iodevice->bootloaderReset();
                        m_iodevice->close();

                        loop2.exec();
                        QApplication::restoreOverrideCursor();

                        if(QMessageBox::question(Core::ICore::dialogParent(),
                            Tr::tr("Connect"),
                            Tr::tr("OpenMV IDE needs to update your bootloader to support ROMFS.\n\n"
                                   "The internal FAT filesystem will be erased!\n\n"
                                   "Continue?"),
                            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                        == QMessageBox::Ok)
                        {
                            openmvRepairingBootloader(true,
                                                      previousMapping,
                                                      originalDfuVidPid,
                                                      dfuNoDialogs,
                                                      Core::ICore::userResourcePath(QStringLiteral("firmware"))
                                                        .pathAppended(originalFirmwareFolder)
                                                        .pathAppended(QStringLiteral("bootloader.dfu")).toString(),
                                                      true);
                            return;
                        }

                        CONNECT_END();
                    }

                    dialog.close();

                    if(!done22)
                    {
                        loop0.exec();
                    }

                    m_ioport->bootloaderReset();
                    loop1.exec();
                    disconnect(conn);
                    disconnect(conn2);
                    QApplication::restoreOverrideCursor();

                    if(!done2)
                    {
                        if((!previousMappingSet) && (!justEraseFlashFs) && forceFirmwarePath.isEmpty() && QMessageBox::question(Core::ICore::dialogParent(),
                            Tr::tr("Connect"),
                            Tr::tr("OpenMV IDE can still try to repair your OpenMV Cam using your OpenMV Cam's DFU Bootloader.\n\n"
                               "Continue?"),
                            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                        == QMessageBox::Ok)
                        {
                            openmvRepairingBootloader(forceFlashFSErase,
                                                      previousMapping,
                                                      originalDfuVidPid,
                                                      dfuNoDialogs,
                                                      Core::ICore::userResourcePath(QStringLiteral("firmware"))
                                                        .pathAppended(originalFirmwareFolder)
                                                        .pathAppended(QStringLiteral("bootloader.dfu")).toString(),
                                                      true);
                            return;
                        }

                        CONNECT_END();
                    }

                    m_iodevice->bootloaderHS(highspeed2);
                    m_iodevice->bootloaderFastMode(tryFastMode);

                    packet_chunksize = highspeed2 ? (tryFastMode ? HS_CHUNK_SIZE : SAFE_HS_CHUNK_SIZE)
                                                  : (tryFastMode ? FS_CHUNK_SIZE : SAFE_FS_CHUNK_SIZE);
                    packet_batchsize = tryFastMode ? FAST_FLASH_PACKET_BATCH_COUNT : SAFE_FLASH_PACKET_BATCH_COUNT;

                    if((version2 == V2_BOOTLDR) || (version2 == V3_BOOTLDR))
                    {
                        int all_start2 = int(), *all_start2Ptr = &all_start2;
                        int start2 = int(), *start2Ptr = &start2;
                        int last2 = int(), *last2Ptr = &last2;

                        QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::bootloaderQueryDone,
                            this, [all_start2Ptr, start2Ptr, last2Ptr] (int all_start, int start, int last) {
                            *all_start2Ptr = all_start;
                            *start2Ptr = start;
                            *last2Ptr = last;
                        });

                        QEventLoop loop;

                        connect(m_iodevice, &OpenMVPluginIO::bootloaderQueryDone,
                                &loop, &QEventLoop::quit);

                        m_iodevice->bootloaderQuery();

                        loop.exec();

                        disconnect(conn);

                        if((all_start2 || start2 || last2)
                        && ((0 <= all_start2) && (all_start2 <= 1023) && (0 <= start2) && (start2 <= 1023) && (0 <= last2) && (last2 <= 1023)))
                        {
                            originalEraseFlashSectorStart = start2;
                            originalEraseFlashSectorEnd = last2;
                            originalEraseFlashSectorAllStart = all_start2;
                            originalEraseFlashSectorAllEnd = last2;
                        }
                    }

                    if(version2 == V3_BOOTLDR)
                    {
                        int *start_block2Ptr = &qspif_start_block;
                        int *max_block2Ptr = &qspif_max_block;
                        int *block_size_in_bytes2Ptr = &qspif_block_size_in_bytes;

                        QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::bootloaderQSPIFLayoutDone,
                            this, [start_block2Ptr, max_block2Ptr, block_size_in_bytes2Ptr] (int start_block, int max_block, int block_size_in_bytes) {
                            *start_block2Ptr = start_block;
                            *max_block2Ptr = max_block;
                            *block_size_in_bytes2Ptr = block_size_in_bytes;
                        });

                        QEventLoop loop;

                        connect(m_iodevice, &OpenMVPluginIO::bootloaderQSPIFLayoutDone,
                                &loop, &QEventLoop::quit);

                        m_iodevice->bootloaderQSPIFLayout();

                        loop.exec();

                        disconnect(conn);
                    }
                }

                QList<QByteArray> dataChunks;

                for(int i = 0; i < data.size(); i += packet_chunksize)
                {
                    dataChunks.append(data.mid(i, qMin(packet_chunksize, data.size() - i)));
                }

                if(dataChunks.size() && (dataChunks.last().size() % 4))
                {
                    dataChunks.last().append(QByteArray(4 - (dataChunks.last().size() % 4), (char) 255));
                }

                // Erase Flash ////////////////////////////////////////
                {
                    int flash_start = forceFlashFSErase ? originalEraseFlashSectorAllStart : originalEraseFlashSectorStart;
                    int flash_end = forceFlashFSErase ? originalEraseFlashSectorAllEnd : originalEraseFlashSectorEnd;

                    if(justEraseFlashFs)
                    {
                        flash_end = originalEraseFlashSectorStart - 1;
                    }

                    QProgressDialog dialog(Tr::tr("Erasing..."), Tr::tr("Cancel"), flash_start, flash_end, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                        (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
                    dialog.setWindowModality(Qt::ApplicationModal);
                    dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                    dialog.setCancelButton(Q_NULLPTR);
                    dialog.show();

                    if(forceFlashFSErase && (qspif_start_block || qspif_max_block || qspif_block_size_in_bytes))
                    {
                        bool ok2 = bool();
                        bool *ok2Ptr = &ok2;

                        QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::bootloaderQSPIFEraseDone,
                            this, [ok2Ptr] (bool ok) {
                            *ok2Ptr = ok;
                        });

                        QEventLoop loop0, loop1;

                        if(tryFastMode)
                        {
                            connect(m_iodevice, &OpenMVPluginIO::queueEmpty,
                                    &loop0, &QEventLoop::quit);
                        }
                        else
                        {
                            connect(m_iodevice, &OpenMVPluginIO::bootloaderQSPIFEraseDone,
                                    &loop0, &QEventLoop::quit);
                        }

                        m_iodevice->bootloaderQSPIFErase(qspif_start_block);

                        loop0.exec();

                        if((!tryFastMode) && ok2)
                        {
                            QTimer::singleShot(SAFE_FLASH_ERASE_DELAY, &loop1, &QEventLoop::quit);
                            loop1.exec();
                        }

                        disconnect(conn2);

                        if(!ok2)
                        {
                            dialog.close();

                            if(tryFastMode)
                            {
                                tryFastMode = false;
                                continue;
                            }
                            else
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Connect"),
                                    Tr::tr("Timeout Error!"));

                                CLOSE_CONNECT_END();
                            }
                        }
                    }

                    bool ok2 = true;
                    bool *ok2Ptr = &ok2;

                    QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::flashEraseDone,
                        this, [ok2Ptr] (bool ok) {
                        *ok2Ptr = *ok2Ptr && ok;
                    });

                    for(int i = flash_start; i <= flash_end; i++)
                    {
                        QEventLoop loop0, loop1;

                        if(tryFastMode)
                        {
                            connect(m_iodevice, &OpenMVPluginIO::queueEmpty,
                                    &loop0, &QEventLoop::quit);
                        }
                        else
                        {
                            connect(m_iodevice, &OpenMVPluginIO::flashEraseDone,
                                    &loop0, &QEventLoop::quit);
                        }

                        m_iodevice->flashErase(i);

                        loop0.exec();

                        if(!ok2)
                        {
                            break;
                        }

                        if(!tryFastMode)
                        {
                            QTimer::singleShot(SAFE_FLASH_ERASE_DELAY, &loop1, &QEventLoop::quit);
                            loop1.exec();
                        }

                        dialog.setValue(i);
                    }

                    dialog.close();

                    disconnect(conn2);

                    if(!ok2)
                    {
                        if(tryFastMode)
                        {
                            tryFastMode = false;
                            continue;
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Connect"),
                                Tr::tr("Timeout Error!"));

                            CLOSE_CONNECT_END();
                        }
                    }
                }

                // Program Flash //////////////////////////////////////

                if(!justEraseFlashFs)
                {
                    bool ok2 = true;
                    bool *ok2Ptr = &ok2;

                    QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::flashWriteDone,
                        this, [ok2Ptr] (bool ok) {
                        *ok2Ptr = *ok2Ptr && ok;
                    });

                    QProgressDialog dialog(Tr::tr("Programming..."), Tr::tr("Cancel"), 0, dataChunks.size() - 1, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                        (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
                    dialog.setWindowModality(Qt::ApplicationModal);
                    dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                    dialog.setCancelButton(Q_NULLPTR);
                    dialog.show();

                    for(int i = 0; i < dataChunks.size(); i += packet_batchsize)
                    {
                        QEventLoop loop0, loop1;

                        if(tryFastMode)
                        {
                            connect(m_iodevice, &OpenMVPluginIO::queueEmpty,
                                    &loop0, &QEventLoop::quit);
                        }
                        else
                        {
                            connect(m_iodevice, &OpenMVPluginIO::flashWriteDone,
                                    &loop0, &QEventLoop::quit);
                        }

                        for (int j = 0, jj = qMin(packet_batchsize, dataChunks.size() - i); j < jj; j++) {
                            m_iodevice->flashWrite(dataChunks.at(i + j));
                        }

                        loop0.exec();

                        if(!ok2)
                        {
                            break;
                        }

                        if(!tryFastMode)
                        {
                            QTimer::singleShot(SAFE_FLASH_WRITE_DELAY, &loop1, &QEventLoop::quit);
                            loop1.exec();
                        }

                        dialog.setValue(i);
                    }

                    dialog.close();

                    disconnect(conn2);

                    if(!ok2)
                    {
                        if(tryFastMode)
                        {
                            tryFastMode = false;
                            continue;
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Connect"),
                                Tr::tr("Timeout Error!"));

                            CLOSE_CONNECT_END();
                        }
                    }
                }

                // Reset Bootloader ///////////////////////////////////
                {
                    QProgressDialog dialog(Tr::tr("Programming..."), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                        (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
                    dialog.setWindowModality(Qt::ApplicationModal);
                    dialog.setAttribute(Qt::WA_ShowWithoutActivating);
                    dialog.setCancelButton(Q_NULLPTR);
                    dialog.show();

                    QEventLoop loop;

                    connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                            &loop, &QEventLoop::quit);

                    m_iodevice->bootloaderReset();
                    m_iodevice->close();

                    loop.exec();
                    dialog.close();
                    QApplication::processEvents();

                    if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        QString(QStringLiteral("%1%2%3%4")).arg((justEraseFlashFs ? Tr::tr("Onboard Data Flash Erased!\n\n") : Tr::tr("Firmware Upgrade complete!\n\n")))
                        .arg(Tr::tr("Your OpenMV Cam will start running its built-in self-test if no sd card is attached... this may take a while.\n\n"))
                        .arg(Tr::tr("Click OK when your OpenMV Cam's RGB LED starts blinking blue - which indicates the self-test is complete."))
                        .arg(Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                                "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open).")));

                    RECONNECT_WAIT_END();
                }
            }
            else if(file.error() != QFile::NoError)
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Error: %L1!").arg(file.errorString()));

                if(forceBootloaderBricked)
                {
                    CONNECT_END();
                }
                else
                {
                    CLOSE_CONNECT_END();
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("The firmware file is empty!"));

                if(forceBootloaderBricked)
                {
                    CONNECT_END();
                }
                else
                {
                    CLOSE_CONNECT_END();
                }
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("Error: %L1!").arg(file.errorString()));

            if(forceBootloaderBricked)
            {
                CONNECT_END();
            }
            else
            {
                CLOSE_CONNECT_END();
            }
        }
    }
}

void OpenMVPlugin::openmvRepairingBootloader(bool forceFlashFSErase,
                                             QString previousMapping,
                                             const QString &originalDfuVidPid,
                                             bool dfuNoDialogs,
                                             const QString &firmwarePath,
                                             bool repairingBootloader)
{
    if(dfuNoDialogs || forceFlashFSErase || repairingBootloader || (QMessageBox::warning(Core::ICore::dialogParent(),
        Tr::tr("Connect"),
        Tr::tr("DFU update erases your OpenMV Cam's internal flash file system.\n\n"
           "Backup your data before continuing!"),
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
    == QMessageBox::Ok))
    {
        if(dfuNoDialogs || QMessageBox::information(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            Tr::tr("Disconnect your OpenMV Cam from your computer, add a jumper wire between the BOOT and RST pins, and then reconnect your OpenMV Cam to your computer.\n\n"
               "Click the Ok button after your OpenMV Cam's DFU Bootloader has enumerated."),
            QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
        == QMessageBox::Ok)
        {
            QString command;
            Utils::Process process;
            downloadFirmware(Tr::tr("Flashing Bootloader"), command, process, QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)), originalDfuVidPid, QStringLiteral("-a 0 -s :leave"));

            if((process.result() == Utils::ProcessResult::FinishedWithSuccess) || (command.contains(QStringLiteral("dfu-util")) && (process.result() == Utils::ProcessResult::FinishedWithError)))
            {
                if(repairingBootloader)
                {
                    if (QMessageBox::information(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("DFU bootloader reset complete!\n\n") +
                        Tr::tr("Disconnect your OpenMV Cam from your computer and remove the jumper wire between the BOOT and RST pins.\n\n") +
                        Tr::tr("Leave your OpenMV Cam unconnected until instructed to reconnect it."), QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                    == QMessageBox::Ok)
                    {
                        for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                        {
                            QJsonObject object = value.toObject();

                            if ((!object.value(QStringLiteral("hidden")).toBool())
                            && (previousMapping.toLower() == object.value(QStringLiteral("boardArchString")).toString().toLower()))
                            {
                                previousMapping = object.value(QStringLiteral("boardDisplayName")).toString();
                                break;
                            }
                        }

                        // Force repair of ROMFS...
                        OpenMVROMFSAccess romfsAccess = OPENMV_ROMFS_RESET;
                        RECONNECT_AND_FORCEBOOTLOADER_END(forceFlashFSErase, previousMapping, romfsAccess);
                    }
                }
                else
                {
                    if (QMessageBox::information(Core::ICore::dialogParent(),
                                             Tr::tr("Connect"),
                                             Tr::tr("DFU firmware update complete!\n\n") +
                                             (Utils::HostOsInfo::isWindowsHost() ? Tr::tr("Disconnect your OpenMV Cam from your computer, remove the jumper wire between the BOOT and RST pins, and then reconnect your OpenMV Cam to your computer.\n\n") : QString()) +
                                             Tr::tr("Click the Ok button after your OpenMV Cam has enumerated and finished running its built-in self test (blue led blinking - this takes a while)."), QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                    == QMessageBox::Ok)
                    {
                        RECONNECT_END();
                    }
                }
            }
            else if(process.result() == Utils::ProcessResult::TerminatedAbnormally)
            {
                CONNECT_END();
            }
            else
            {
                QMessageBox box(QMessageBox::Critical, Tr::tr("Connect"), repairingBootloader ? Tr::tr("DFU bootloader reset failed!") : Tr::tr("DFU firmware update failed!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                box.setDefaultButton(QMessageBox::Ok);
                box.setEscapeButton(QMessageBox::Cancel);
                box.exec();
            }
        }
    }

    CONNECT_END();
}

} // namespace Internal
} // namespace OpenMV
