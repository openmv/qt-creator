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

#if defined(Q_OS_WIN)
#include <windows.h>
#include <io.h>
#elif defined(Q_OS_LINUX)
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#elif defined(Q_OS_MAC)
#include <unistd.h>
#include <fcntl.h>
#include <sys/mount.h>
#endif

namespace OpenMV {
namespace Internal {

// Parse the A records (hostname -> IPv4) out of an mDNS response datagram.
//
// mDNS reuses the classic DNS message format (RFC 1035), all big-endian -- so a big-endian
// QDataStream reads the words/longs directly, no hand-rolled byte shifts. A message is:
//
//   Header (12 bytes): id(2), flags(2), then four 16-bit section counts:
//       QDCOUNT questions, ANCOUNT answers, NSCOUNT authority, ARCOUNT additional.
//   QDCOUNT question entries:                 NAME, QTYPE(2), QCLASS(2)
//   (ANCOUNT+NSCOUNT+ARCOUNT) resource records: NAME, TYPE(2), CLASS(2), TTL(4), RDLENGTH(2), RDATA(RDLENGTH)
//
// A NAME is a sequence of labels, each a <len byte><len bytes of text>, ending at a zero length
// byte (e.g. "openmv-cam" "local" 0  ->  "openmv-cam.local"). To save space a name can instead be
// a compression pointer: if a length byte's top two bits are set ((len & 0xC0) == 0xC0), the low
// 14 bits of that 2-byte field are an offset from the start of the message to continue the name
// from. That is why reading a name needs random access -- we seek to the pointed-at offset, read
// the rest of the name there, then resume right after the 2-byte pointer.
//
// We only care about A records (TYPE == 1, an IPv4 host address): their RDATA is the 4-byte IP.
// Every other record type is skipped over using its RDLENGTH. The datagram is untrusted (it came
// off the network), so bounds safety rides on QDataStream's status: any read past the end flips
// status to non-Ok, which ends every loop -- a malformed packet just yields fewer records, never
// an out-of-bounds read.
QList<QPair<QString, QHostAddress> > OpenMVPlugin::parseMdnsARecords(const QByteArray &data)
{
    QList<QPair<QString, QHostAddress> > records;

    QDataStream in(data);
    in.setByteOrder(QDataStream::BigEndian);

    // Header: id, flags, then the four section counts.
    quint16 id, flags, questionCount, answerCount, authorityCount, additionalCount;
    in >> id >> flags >> questionCount >> answerCount >> authorityCount >> additionalCount;

    // Read a DNS name at the stream's current position, following compression pointers. A pointer
    // jumps elsewhere in the packet; we resume right after it so the outer cursor stays correct.
    auto readName = [&in]() -> QString {
        QStringList labels;
        qint64 resumePos = -1;

        for(int jumps = 0; (in.status() == QDataStream::Ok) && (jumps < 16); )
        {
            quint8 len;
            in >> len;

            if(len == 0)                            // root label terminates the name
            {
                break;
            }

            if((len & 0xC0) == 0xC0)                // compression pointer: low 14 bits are the offset
            {
                in.device()->seek(in.device()->pos() - 1);
                quint16 pointer;
                in >> pointer;
                if(resumePos < 0) resumePos = in.device()->pos();
                in.device()->seek(pointer & 0x3FFF);
                jumps++;
                continue;
            }

            QByteArray label(len, '\0');            // label: <len byte><len bytes of text>
            if(in.readRawData(label.data(), len) != len) break;
            labels.append(QString::fromUtf8(label));
        }

        if(resumePos >= 0) in.device()->seek(resumePos);
        return labels.join(QLatin1Char('.'));
    };

    for(int i = 0; (i < questionCount) && (in.status() == QDataStream::Ok); i++)
    {
        readName();                                 // don't need the question name, just step past it
        in.skipRawData(4);                          // QTYPE + QCLASS
    }

    const int resourceCount = answerCount + authorityCount + additionalCount;   // an + ns + ar
    const quint16 dnsTypeA = 1;

    for(int i = 0; (i < resourceCount) && (in.status() == QDataStream::Ok); i++)
    {
        const QString name = readName();

        quint16 type, klass, rdlength;
        quint32 ttl;
        in >> type >> klass >> ttl >> rdlength;

        if(in.status() != QDataStream::Ok) break;

        if((type == dnsTypeA) && (rdlength == 4))
        {
            quint32 ipv4;
            in >> ipv4;                             // 4 bytes, network order -> QHostAddress
            records.append(qMakePair(name, QHostAddress(ipv4)));
        }
        else
        {
            in.skipRawData(rdlength);
        }
    }

    return records;
}

void OpenMVPlugin::processEvents()
{
    // No device activity at all while an external tool's modal LoaderDialog
    // is up: the camera must not stream during a compile/flash. Polling
    // resumes by itself once the tool finishes and the dialog is destroyed.
    if(loaderDialogActive())
    {
        return;
    }

    if((!m_working) && m_connected)
    {
        if(m_iodevice->getTimeout())
        {
            disconnectClicked();
        }
        else
        {
            if((!m_useGetState)
                || (m_major < OPENMV_ADD_GET_STATE_MAJOR)
                || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor < OPENMV_ADD_GET_STATE_MINOR))
                || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor == OPENMV_ADD_GET_STATE_MINOR) && (m_patch < OPENMV_ADD_GET_STATE_PATCH)))
            {
                if((!frameBufferDisabled()) && (!m_iodevice->frameSizeDumpQueued()) && m_frameSizeDumpTimer.hasExpired(m_frameSizeDumpSpacing) &&
                    (!(m_iodevice->v2ProtocolEnabled() && m_dynamicFrameReading)))
                {
                    m_frameSizeDumpTimer.restart();
                    m_iodevice->frameSizeDump();
                }

                if((!m_iodevice->getScriptRunningQueued()) &&
                    m_getScriptRunningTimer.hasExpired(m_getScriptRunningSpacing))
                {
                    m_getScriptRunningTimer.restart();
                    m_iodevice->getScriptRunning();

                    if(m_portPath.isEmpty())
                    {
                        setPortPath(true);
                    }
                }

                if((!m_iodevice->getTxBufferQueued()) && m_getTxBufferTimer.hasExpired(m_getTxBufferSpacing))
                {
                    m_getTxBufferTimer.restart();
                    m_iodevice->getTxBuffer();
                }
            }
            else
            {
                if((!m_iodevice->getStateQueued()) && m_getStateTimer.hasExpired(m_getStateSpacing))
                {
                    m_getStateTimer.restart();
                    m_iodevice->getState();

                    if(m_portPath.isEmpty())
                    {
                        setPortPath(true);
                    }
                }
            }

            if(m_iodevice->getProfileEnabled())
            {
                if((!m_iodevice->readProfileQueued()) && m_readProfileTimer.hasExpired(m_readProfileSpacing))
                {
                    m_readProfileTimer.restart();
                    m_iodevice->readProfile();
                }
            }

            // Poll memory stats continuously while connected -- not only while
            // the Memory view is visible -- so switching to it shows current
            // data immediately, and the last values stay on screen after a
            // disconnect (this whole block only runs while m_connected). The
            // serial layer never sends SYS_MEMORY to firmware that predates it,
            // so polling costs nothing there.
            if((!m_iodevice->getMemoryStatsQueued()) && m_memoryStatsTimer.hasExpired(MEMORY_STATS_SPACING))
            {
                m_memoryStatsTimer.restart();
                m_iodevice->getMemoryStats();
            }

            // Board Info: system info is static per connection (cached on the
            // camera at connect), so this fetches once and then goes quiet.
            if(m_boardInfoView && (!m_boardInfoView->hasData()))
            {
                if((!m_iodevice->getSystemInfoQueued()) && m_systemInfoTimer.hasExpired(SYSTEM_INFO_SPACING))
                {
                    m_systemInfoTimer.restart();
                    m_iodevice->getSystemInfo();
                }
            }

            // Poll protocol statistics continuously while connected, like the
            // memory stats above: host stats are local and device stats are one
            // small PROTO_STATS command.
            if((!m_iodevice->getProtocolStatsQueued()) && m_protocolStatsTimer.hasExpired(PROTOCOL_STATS_SPACING))
            {
                m_protocolStatsTimer.restart();
                m_iodevice->getProtocolStats();
            }

            // Channels: polled continuously while connected, like the memory
            // and protocol stats above, so the view is always current and a
            // CSV recording can't gap when another pane view is selected.
            // Fast polling only happens while the script actually publishes
            // channels; otherwise poll at the discovery rate, where a read is
            // free (the camera layer sends nothing for an empty channel set).
            {
                int readChannelsSpacing = m_userChannelsPresent ? m_readChannelsSpacing : int(READ_CHANNELS_DISCOVERY_SPACING);

                if((!m_iodevice->readChannelsQueued()) && m_readChannelsTimer.hasExpired(readChannelsSpacing))
                {
                    m_readChannelsTimer.restart();
                    m_iodevice->readChannels();
                }
            }

            if(m_iodevice->v2ProtocolEnabled() && m_dynamicFrameReading && (!m_dynamicFrameReadingLock))
            {
                m_ioport->getFrameReady();
                m_dynamicFrameReadingLock = true;
            }

            if (m_dynamicFrameReadingPending && (!frameBufferDisabled()) && (!m_iodevice->frameSizeDumpQueued()))
            {
                m_dynamicFrameReadingPending = false;
                m_frameSizeDumpTimer.restart();
                m_iodevice->frameSizeDump();
            }

            if(m_timer.hasExpired(FPS_TIMER_EXPIRATION_TIME))
            {
                // No frames for a while (script stopped / camera silent) -> zero both rates.
                m_fpsIde = 0.0;
                m_fpsCamera = 0.0;
                refreshFpsButton();
            }
        }
    }
}

void OpenMVPlugin::refreshFpsButton()
{
    if(m_fpsCameraValid)
    {
        // On-camera FPS plus the IDE's own display FPS, both in the one label.
        m_fpsButton->setText(Tr::tr("FPS: %L1 Cam - %L2 IDE").arg(m_fpsCamera, 0, 'f', 1).arg(m_fpsIde, 0, 'f', 1));
        m_fpsButton->setToolTip(Tr::tr("On-camera FPS and IDE display FPS"));
    }
    else
    {
        m_fpsButton->setText(Tr::tr("FPS: %L1").arg(m_fpsIde, 5, 'f', 1));
        m_fpsButton->setToolTip(Tr::tr("May be different from camera FPS"));
    }
}

void OpenMVPlugin::setPortPath(bool silent)
{
    if(!m_working)
    {
        QStringList drives;

        for(const QPair<QString, QString> &pair : qAsConst(m_availableDrives))
        {
            const QString rootPath = pair.first;
            const QString serialNumber = pair.second;
            QByteArray serialNumberBytes = serialNumber.toUtf8();
            std::reverse(serialNumberBytes.begin(), serialNumberBytes.end());
            const QString serialNumberRev = QString::fromUtf8(serialNumberBytes);

            if((((m_major < OPENMV_DISK_ADDED_MAJOR)
                  || ((m_major == OPENMV_DISK_ADDED_MAJOR) && (m_minor < OPENMV_DISK_ADDED_MINOR))
                  || ((m_major == OPENMV_DISK_ADDED_MAJOR) && (m_minor == OPENMV_DISK_ADDED_MINOR) && (m_patch < OPENMV_DISK_ADDED_PATCH)))
                  || QFile::exists(rootPath + QStringLiteral(OPENMV_DISK_ADDED_NAME)))
                && ((serialNumber.toLower() == m_portDriveSerialNumber.toLower()) || (serialNumberRev.toLower() == m_portDriveSerialNumber.toLower())))
            {
                drives.append(rootPath);
            }
        }

        // If strict matching didn't work. Allow for weak matching if there's only one drive.
        if(drives.isEmpty() && m_availableDrives.size() == 1)
        {
            drives.append(m_availableDrives.at(0).first);
        }

        Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
        const Utils::Key portKey = Utils::keyFromString(QStringLiteral(SERIAL_PORT_SETTINGS_GROUP "/") + m_portName);

        if(drives.isEmpty())
        {
            if(!silent)
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                                      Tr::tr("Select Drive"),
                                      Tr::tr("No valid drives were found to associate with your OpenMV Cam!"));
            }

            m_portPath = QString();
        }
        else if(drives.size() == 1)
        {
            if(m_portPath == drives.first())
            {
                QTimer::singleShot(0, this, [this] {
                    Core::FileUtils::showInGraphicalShell(Core::ICore::mainWindow(),
                                                          Utils::FilePath::fromString(m_portPath).pathAppended(Utils::HostOsInfo::isWindowsHost()
                                                                                                                   ? QStringLiteral("") : QStringLiteral(".openmv_disk")));
                });
            }
            else
            {
                m_portPath = drives.first();
                settings->setValue(portKey, m_portPath);
            }
        }
        else
        {
            int index = drives.indexOf(settings->value(portKey).toString());

            bool ok = silent;
            QString temp = silent ? drives.first() : QInputDialog::getItem(Core::ICore::dialogParent(),
                                                                           Tr::tr("Select Drive"), Tr::tr("Please associate a drive with your OpenMV Cam"),
                                                                           drives, (index != -1) ? index : 0, false, &ok,
                                                                           Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                                                               (Utils::HostOsInfo::isMacHost() ? Qt::WindowType() : Qt::WindowCloseButtonHint));

            if(ok)
            {
                m_portPath = temp;
                settings->setValue(portKey, m_portPath);
            }
        }

        m_pathButton->setText((!m_portPath.isEmpty()) ? Tr::tr("Drive: %L1").arg(m_portPath) : Tr::tr("Drive:"));

        Core::IEditor *editor = Core::EditorManager::currentEditor();
        m_openDriveFolderAction->setEnabled(!m_portPath.isEmpty());
        m_editWifiDebugAction->setEnabled(!m_portPath.isEmpty());
        m_saveAction->setEnabled((!m_portPath.isEmpty()) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));

        m_frameBuffer->enableSaveTemplate(!m_portPath.isEmpty());
        m_frameBuffer->enableSaveDescriptor(!m_portPath.isEmpty());

        Python::Internal::PyLSClient::setPortPath(Utils::FilePath::fromUserInput(m_portPath));
    }
    else
    {
        deferNormal([this, silent] { setPortPath(silent); });
    }
}

void OpenMVPlugin::setSpacing()
{
    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

    bool useGetState = settings->value(SETTINGS_GROUP "/" LAST_USE_GET_STATE, true).toBool();
    int frameDumpSpacing = settings->value(SETTINGS_GROUP "/" LAST_FRAME_DUMP_SPACING, FRAME_SIZE_DUMP_SPACING).toInt();
    int getScriptRunningSpacing = settings->value(SETTINGS_GROUP "/" LAST_GET_SCRIPT_RUNNING_SPACING,
                                                   GET_SCRIPT_RUNNING_SPACING).toInt();
    int getTxBufferSpacing = settings->value(SETTINGS_GROUP "/" LAST_GET_TX_BUFFER_SPACING,
                                             GET_TX_BUFFER_SPACING).toInt();
    int getStateSpacing = settings->value(SETTINGS_GROUP "/" LAST_GET_STATE_SPACING, GET_STATE_SPACING).toInt();
    int readProfileSpacing = settings->value(SETTINGS_GROUP "/" LAST_READ_PROFILE_SPACING, READ_PROFILE_SPACING).toInt();
    int readChannelsSpacing = settings->value(SETTINGS_GROUP "/" LAST_READ_CHANNELS_SPACING, READ_CHANNELS_SPACING).toInt();

    int useGetStateAvailable =
      !((m_major < OPENMV_ADD_GET_STATE_MAJOR)
    || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) &&
        (m_minor < OPENMV_ADD_GET_STATE_MINOR))
    || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) &&
        (m_minor == OPENMV_ADD_GET_STATE_MINOR) &&
        (m_patch < OPENMV_ADD_GET_STATE_PATCH)));

    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                                  Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                      (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog->setWindowTitle(Tr::tr("Debug Protocol Settings"));
    dialog->setSizeGripEnabled(true);
    QVBoxLayout *vlayout = new QVBoxLayout(dialog);

    QWidget *mainWidget = new QWidget;
    QHBoxLayout *hlayout = new QHBoxLayout(mainWidget);
    hlayout->setContentsMargins(0, 0, 0, 0);
    vlayout->addWidget(mainWidget);

    // The system-info and host/device stats readouts moved into the
    // histogram pane's Board Info and Statistics views; this dialog keeps
    // just the protocol/polling controls.
    QWidget *rightWidget = new QWidget;
    QVBoxLayout *rlayout = new QVBoxLayout(rightWidget);
    rlayout->setContentsMargins(0, 0, 0, 0);
    hlayout->addWidget(rightWidget);

    QLabel *protocolControlsTitle = new QLabel(Tr::tr("Protocol Controls:"));
    protocolControlsTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    rlayout->addWidget(protocolControlsTitle);

    QCheckBox *dynamicFrameReadingBox = new QCheckBox(Tr::tr("Dynamic Frame Reading"));
    dynamicFrameReadingBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    dynamicFrameReadingBox->setChecked(m_dynamicFrameReading);
    dynamicFrameReadingBox->setEnabled(m_iodevice->v2ProtocolEnabled());
    rlayout->addWidget(dynamicFrameReadingBox);

    QFrame *line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    rlayout->addWidget(line);
#ifdef Q_OS_MAC
    rlayout->addSpacing(10);
#endif

    QGroupBox *getStateGroup = new QGroupBox(Tr::tr("Combined Polling"));
    getStateGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    getStateGroup->setCheckable(true);
    getStateGroup->setChecked(useGetState);
    getStateGroup->setEnabled(useGetStateAvailable);
#ifdef Q_OS_MAC
    // QGroupBox on macOS renders with the native Cocoa style, which for a
    // checkable box squishes the title checkbox to a tiny size and gives
    // the frame no interior padding. Setting an explicit stylesheet forces
    // Qt's own painter, which honors margin/padding and draws a normal-sized
    // title -- and lets both group boxes below share the same look.
    const QString kMacGroupBoxStyle = QStringLiteral(
        "QGroupBox { margin-top: 22px; padding: 12px 8px 10px 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 12px; padding: 0 6px; }");
    getStateGroup->setStyleSheet(kMacGroupBoxStyle);
#endif
    rlayout->addWidget(getStateGroup);
#ifdef Q_OS_MAC
    rlayout->addSpacing(12);
#endif

    QFormLayout *getStateGroupLayout = new QFormLayout(getStateGroup);

    QSpinBox *getStateSpacingBox = new QSpinBox(getStateGroup);
    getStateSpacingBox->setRange(0, 1000);
    getStateSpacingBox->setValue(getStateSpacing);
    getStateGroupLayout->addRow(Tr::tr("Polling (ms)"), getStateSpacingBox);

    QGroupBox *oldStateGroup = new QGroupBox(useGetStateAvailable ? Tr::tr("Split Polling") : Tr::tr("Polling Settings"));
    oldStateGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
#ifdef Q_OS_MAC
    oldStateGroup->setStyleSheet(kMacGroupBoxStyle);
#endif

    if(useGetStateAvailable)
    {
        oldStateGroup->setDisabled(useGetState);
        connect(getStateGroup, &QGroupBox::toggled, oldStateGroup, &QGroupBox::setDisabled);
    }

    rlayout->addWidget(oldStateGroup);
#ifdef Q_OS_MAC
    rlayout->addSpacing(12);
#endif

    QFormLayout *oldStateGroupLayout = new QFormLayout(oldStateGroup);

    QSpinBox *frameDumpSpacingBox = new QSpinBox;
    frameDumpSpacingBox->setRange(0, 1000);
    frameDumpSpacingBox->setValue(frameDumpSpacing);
    frameDumpSpacingBox->setDisabled(m_iodevice->v2ProtocolEnabled() && m_dynamicFrameReading);
    oldStateGroupLayout->addRow(Tr::tr("Frame Buffer Polling (ms)"), frameDumpSpacingBox);

    QSpinBox *getScriptRunningSpacingBox = new QSpinBox;
    getScriptRunningSpacingBox->setRange(0, 1000);
    getScriptRunningSpacingBox->setValue(getScriptRunningSpacing);
    getScriptRunningSpacingBox->setDisabled(m_iodevice->v2ProtocolEnabled());
    oldStateGroupLayout->addRow(Tr::tr("Script State Polling (ms)"), getScriptRunningSpacingBox);

    QSpinBox *getTxBufferSpacingBox = new QSpinBox;
    getTxBufferSpacingBox->setRange(0, 1000);
    getTxBufferSpacingBox->setValue(getTxBufferSpacing);
    oldStateGroupLayout->addRow(Tr::tr("Text Buffer Polling (ms)"), getTxBufferSpacingBox);

    QWidget *readProfileWidget = new QWidget;
    QFormLayout *readProfileWidgetLayout = new QFormLayout(readProfileWidget);
    readProfileWidgetLayout->setContentsMargins(0, 0, 0, 0);
    QSpinBox *readProfileSpacingBox = new QSpinBox;
    readProfileSpacingBox->setRange(0, 1000);
    readProfileSpacingBox->setValue(readProfileSpacing);
    readProfileSpacingBox->setEnabled(m_iodevice->getProfileEnabled());
    QLabel *readProfileLabel = new QLabel(Tr::tr("Code Profiler Polling (ms)"));
    readProfileLabel->setEnabled(m_iodevice->getProfileEnabled());
    readProfileWidgetLayout->addRow(readProfileLabel, readProfileSpacingBox);
    rlayout->addWidget(readProfileWidget);

    // Script-published channel reads (Channels view) - V2 protocol only.
    QWidget *readChannelsWidget = new QWidget;
    QFormLayout *readChannelsWidgetLayout = new QFormLayout(readChannelsWidget);
    readChannelsWidgetLayout->setContentsMargins(0, 0, 0, 0);
    QSpinBox *readChannelsSpacingBox = new QSpinBox;
    readChannelsSpacingBox->setRange(0, 1000);
    readChannelsSpacingBox->setValue(readChannelsSpacing);
    readChannelsSpacingBox->setEnabled(m_iodevice->v2ProtocolEnabled());
    QLabel *readChannelsLabel = new QLabel(Tr::tr("Channel Polling (ms)"));
    readChannelsLabel->setEnabled(m_iodevice->v2ProtocolEnabled());
    readChannelsWidgetLayout->addRow(readChannelsLabel, readChannelsSpacingBox);
    rlayout->addWidget(readChannelsWidget);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    vlayout->addWidget(box);

    connect(m_iodevice, &OpenMVPluginIO::closeResponse, dialog, &QDialog::reject);
    connect(dynamicFrameReadingBox, &QCheckBox::toggled, this, [this, frameDumpSpacingBox] (bool checked) {
        frameDumpSpacingBox->setDisabled(m_iodevice->v2ProtocolEnabled() && checked);
    });

    if(dialog->exec() == QDialog::Accepted)
    {
        settings->setValue(SETTINGS_GROUP "/" LAST_USE_GET_STATE, m_useGetState = getStateGroup->isChecked());
        settings->setValue(SETTINGS_GROUP "/" LAST_FRAME_DUMP_SPACING,
                           m_frameSizeDumpSpacing = frameDumpSpacingBox->value());
        settings->setValue(SETTINGS_GROUP "/" LAST_GET_SCRIPT_RUNNING_SPACING,
                           m_getScriptRunningSpacing = getScriptRunningSpacingBox->value());
        settings->setValue(SETTINGS_GROUP "/" LAST_GET_TX_BUFFER_SPACING,
                           m_getTxBufferSpacing = getTxBufferSpacingBox->value());
        settings->setValue(SETTINGS_GROUP "/" LAST_GET_STATE_SPACING, m_getStateSpacing = getStateSpacingBox->value());
        settings->setValue(SETTINGS_GROUP "/" LAST_READ_PROFILE_SPACING,
                           m_readProfileSpacing = readProfileSpacingBox->value());
        settings->setValue(SETTINGS_GROUP "/" LAST_READ_CHANNELS_SPACING,
                           m_readChannelsSpacing = readChannelsSpacingBox->value());
        settings->setValue(SETTINGS_GROUP "/" LAST_DYNAMIC_FRAME_READING,
                           m_dynamicFrameReading = dynamicFrameReadingBox->isChecked());

        m_frameSizeDumpTimer.restart();
        m_getScriptRunningTimer.restart();
        m_getTxBufferTimer.restart();
        m_getStateTimer.restart();
        m_readProfileTimer.restart();
        m_readChannelsTimer.restart();
        m_timer.restart();
        m_queue.clear();
        m_cameraQueue.clear();
    }

    delete dialog;
}

static bool flushFileHandle(QFile &f)
{
    if (!f.isOpen())
        return false;

    if (!f.flush())
        return false;

#if defined(Q_OS_WIN)
    const int fd = f.handle();
    if (fd < 0)
        return false;

    HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE)
        return false;

    return !!FlushFileBuffers(h);

#elif defined(Q_OS_MAC)
    // Strongest flush on macOS
    return (::fcntl(f.handle(), F_FULLFSYNC) == 0);

#else // Linux + other POSIX
    // fdatasync is enough for file contents
    return (::fdatasync(f.handle()) == 0);
#endif
}

static bool writeFileDirectAndFlush(const QString &filePath, const QByteArray &data, QString *errOut)
{
    QFile f(filePath);

    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errOut) *errOut = f.errorString();
        return false;
    }

    const qint64 written = f.write(data);
    if (written != data.size()) {
        if (errOut) *errOut = f.errorString().isEmpty()
            ? QStringLiteral("Short write (%1/%2)").arg(written).arg(data.size())
            : f.errorString();
        f.close();
        return false;
    }

    if (!flushFileHandle(f)) {
        if (errOut) {
#if defined(Q_OS_WIN)
            *errOut = QStringLiteral("FlushFileBuffers failed (winerr=%1)").arg(GetLastError());
#else
            *errOut = QStringLiteral("File flush failed");
#endif
        }

        f.close();
        return false;
    }

    f.close();
    return true;
}

bool OpenMVPlugin::writeFileToDriveAndFlush(const QString &filePath, const QByteArray &data, QString *errOut)
{
    // Write + flush the file handle down to the device (same as Save Script -> main.py).
    if(!writeFileDirectAndFlush(filePath, data, errOut))
    {
        return false;
    }

    // If the file lives on the OpenMV Cam's mounted drive, flush the whole volume too so
    // the cam's filesystem actually sees the change (the OS otherwise caches the write).
#if defined(Q_OS_WIN)
    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
#else
    const Qt::CaseSensitivity cs = Qt::CaseSensitive;
#endif
    if((!m_portPath.isEmpty())
    && QDir::cleanPath(filePath).startsWith(QDir::cleanPath(m_portPath), cs))
    {
        flushPortPath();
    }

    return true;
}

void OpenMVPlugin::saveScript()
{
    if(!m_working)
    {
        int answer = QMessageBox::question(Core::ICore::dialogParent(),
                                           Tr::tr("Save Script"),
                                           Tr::tr("Strip comments and convert spaces to tabs?"),
                                           QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);

        if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))
        {
            QByteArray contents = Core::EditorManager::currentEditor() ? Core::EditorManager::currentEditor()->document() ? Core::EditorManager::currentEditor()->document()->contents() : QByteArray() : QByteArray();

            if(importHelper(contents))
            {
                if(answer == QMessageBox::Yes)
                {
                    contents = loadFilter(contents);
                }

                QString err;

                if(!writeFileDirectAndFlush(m_portPath + QDir::separator() + QStringLiteral("main.py"), contents, &err))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                                          Tr::tr("Save Script"),
                                          Tr::tr("Error: %L1!").arg(err));
                }
                else
                {
                    flushPortPath();
                }
            }
        }
    }
    else
    {
        deferNormal([this] { saveScript(); });
    }
}

void OpenMVPlugin::saveTemplate(const QRect &rect)
{
    if(!m_working)
    {
        QString drivePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath));

        Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

        QString path;

        forever
        {
            path =
                QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Save Template"),
                                             settings->value(SETTINGS_GROUP "/" LAST_SAVE_TEMPLATE_PATH,
                                                             drivePath).toString(),
                                             Tr::tr("Image Files (*.bmp *.jpg *.jpeg *.pgm *.ppm)"));

            if((!path.isEmpty()) && QFileInfo(path).completeSuffix().isEmpty())
            {
                QMessageBox::warning(Core::ICore::dialogParent(),
                                     Tr::tr("Save Template"),
                                     Tr::tr("Please add a file extension!"));

                continue;
            }

            break;
        }

        if(!path.isEmpty())
        {
            path = QDir::cleanPath(QDir::fromNativeSeparators(path));

            if((!path.startsWith(drivePath))
                || (!QDir(QFileInfo(path).path()).exists()))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                                      Tr::tr("Save Template"),
                                      Tr::tr("Please select a valid path on the OpenMV Cam!"));
            }
            else
            {
                QByteArray sendPath = QString(path).remove(0, drivePath.size()).prepend(QLatin1Char('/')).toUtf8();

                if(sendPath.size() <= DESCRIPTOR_SAVE_PATH_MAX_LEN)
                {
                    m_iodevice->templateSave(rect.x(), rect.y(), rect.width(), rect.height(), sendPath);
                    settings->setValue(SETTINGS_GROUP "/" LAST_SAVE_TEMPLATE_PATH, path);
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                                          Tr::tr("Save Template"),
                                          Tr::tr("\"%L1\" is longer than a max length of %L2 characters!").arg(QString::fromUtf8(sendPath)).arg(DESCRIPTOR_SAVE_PATH_MAX_LEN));
                }
            }
        }
    }
    else
    {
        deferNormal([this, rect] { saveTemplate(rect); });
    }
}

void OpenMVPlugin::saveDescriptor(const QRect &rect)
{
    if(!m_working)
    {
        QString drivePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath));

        Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

        QString path;

        forever
        {
            path =
                QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Save Descriptor"),
                                             settings->value(SETTINGS_GROUP "/" LAST_SAVE_DESCRIPTOR_PATH,
                                                             drivePath).toString(),
                                             Tr::tr("Keypoints Files (*.lbp *.orb)"));

            if((!path.isEmpty()) && QFileInfo(path).completeSuffix().isEmpty())
            {
                QMessageBox::warning(Core::ICore::dialogParent(),
                                     Tr::tr("Save Descriptor"),
                                     Tr::tr("Please add a file extension!"));

                continue;
            }

            break;
        }

        if(!path.isEmpty())
        {
            path = QDir::cleanPath(QDir::fromNativeSeparators(path));

            if((!path.startsWith(drivePath))
                || (!QDir(QFileInfo(path).path()).exists()))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                                      Tr::tr("Save Descriptor"),
                                      Tr::tr("Please select a valid path on the OpenMV Cam!"));
            }
            else
            {
                QByteArray sendPath = QString(path).remove(0, drivePath.size()).prepend(QLatin1Char('/')).toUtf8();

                if(sendPath.size() <= DESCRIPTOR_SAVE_PATH_MAX_LEN)
                {
                    m_iodevice->descriptorSave(rect.x(), rect.y(), rect.width(), rect.height(), sendPath);
                    settings->setValue(SETTINGS_GROUP "/" LAST_SAVE_DESCRIPTOR_PATH, path);
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                                          Tr::tr("Save Descriptor"),
                                          Tr::tr("\"%L1\" is longer than a max length of %L2 characters!").arg(QString::fromUtf8(sendPath)).arg(DESCRIPTOR_SAVE_PATH_MAX_LEN));
                }
            }
        }
    }
    else
    {
        deferNormal([this, rect] { saveDescriptor(rect); });
    }
}

QByteArray OpenMVPlugin::fixScriptForSensor(QByteArray data, bool notExamples, bool increaseResolution)
{
    if(!notExamples)
    {
        // Older example scripts may lack a blank line between the import block
        // and the first line of code - insert one if it is missing.
        QList<QByteArray> lines = data.split('\n');

        for(int i = 0; i < lines.size() - 1; i++)
        {
            const QByteArray line = lines.at(i).trimmed();
            const QByteArray next = lines.at(i + 1).trimmed();

            bool lineIsImport = (lines.at(i).startsWith("import ") || lines.at(i).startsWith("from ")) &&
                                (!line.endsWith('(')) && (!line.endsWith('\\'));
            bool nextIsImport = next.startsWith("import ") || next.startsWith("from ");

            if(lineIsImport && (!nextIsImport) && (!next.isEmpty()))
            {
                lines.insert(i + 1, lines.at(i).endsWith('\r') ? QByteArrayLiteral("\r") : QByteArray());
                i += 1;
            }
        }

        data = lines.join('\n');
    }

    if((!notExamples) &&
        ((m_sensorType.startsWith(QStringLiteral("HM01B0"))) ||
         (m_sensorType.startsWith(QStringLiteral("HM0360"))) ||
         (m_sensorType.startsWith(QStringLiteral("MT9V0X2"))) ||
         (m_sensorType.startsWith(QStringLiteral("MT9V0X4"))) ||
         (m_sensorType.startsWith(QStringLiteral("BOSON"))) ||
         (m_sensorType.startsWith(QStringLiteral("BOSON-320"))) ||
         (m_sensorType.startsWith(QStringLiteral("BOSON-640"))) ||
         (m_sensorType.startsWith(QStringLiteral("BOSON-320+"))) ||
         (m_sensorType.startsWith(QStringLiteral("BOSON-640+"))) ||
         (m_sensorType.startsWith(QStringLiteral("PAG7920"))) ||
         (m_sensorType.startsWith(QStringLiteral("PAJ6100"))) ||
         (m_sensorType.startsWith(QStringLiteral("FROGEYE2020"))) ||
         (m_sensorType.startsWith(QStringLiteral("GENX320-S"))) ||
         (m_sensorType.startsWith(QStringLiteral("GENX320")))))
    {
        data = data.replace(QByteArrayLiteral("sensor.set_pixformat(sensor.RGB565)"),
                            QByteArrayLiteral("sensor.set_pixformat(sensor.GRAYSCALE)"));
        data = data.replace(QByteArrayLiteral(".pixformat(csi.RGB565)"),
                            QByteArrayLiteral(".pixformat(csi.GRAYSCALE)"));

        if(m_sensorType.startsWith(QStringLiteral("HM01B0")))
        {
            data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"),
                                QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"));
            data = data.replace(QByteArrayLiteral(".framesize(csi.VGA)"),
                                QByteArrayLiteral(".framesize(csi.QVGA)"));
        }

        if((m_sensorType.startsWith(QStringLiteral("BOSON-320"))) ||
            (m_sensorType.startsWith(QStringLiteral("BOSON-320+"))) ||
            (m_sensorType.startsWith(QStringLiteral("PAG7920"))) ||
            (m_sensorType.startsWith(QStringLiteral("PAJ6100"))) ||
            (m_sensorType.startsWith(QStringLiteral("FROGEYE2020"))))
        {
            data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"),
                                QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"));
            data = data.replace(QByteArrayLiteral(".framesize(csi.VGA)"),
                                QByteArrayLiteral(".framesize(csi.QVGA)"));
        }

        if((m_sensorType.startsWith(QStringLiteral("BOSON-640"))) ||
            (m_sensorType.startsWith(QStringLiteral("BOSON-640+"))))
        {
            data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"),
                                QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"));
            data = data.replace(QByteArrayLiteral(".framesize(csi.QVGA)"),
                                QByteArrayLiteral(".framesize(csi.VGA)"));
        }

        if((m_sensorType.startsWith(QStringLiteral("GENX320-S"))) ||
            (m_sensorType.startsWith(QStringLiteral("GENX320"))))
        {
            data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"),
                                QByteArrayLiteral("sensor.set_framesize(sensor.B320X320)"));
            data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"),
                                QByteArrayLiteral("sensor.set_framesize(sensor.B320X320)"));
            data = data.replace(QByteArrayLiteral(".framesize(csi.QVGA)"),
                                QByteArrayLiteral(".framesize((320, 320))"));
            data = data.replace(QByteArrayLiteral(".framesize(csi.VGA)"),
                                QByteArrayLiteral(".framesize((320, 320))"));
        }
    }

    if ((!notExamples) &&
        increaseResolution &&
        ((m_sensorType.startsWith(QStringLiteral("PAG7936"))) ||
         (m_sensorType.startsWith(QStringLiteral("PS5520")))))
    {
        data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"),
                            QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"));
        data = data.replace(QByteArrayLiteral(".framesize(csi.QVGA)"),
                            QByteArrayLiteral(".framesize(csi.VGA)"));
    }

    if ((!notExamples) &&
        ((m_boardType.startsWith(QStringLiteral("M4"))) ||
         (m_boardType.startsWith(QStringLiteral("M7"))) ||
         (m_boardType.startsWith(QStringLiteral("H7"))) ||
         (m_boardType.startsWith(QStringLiteral("NICLA"))) ||
         (m_boardType.startsWith(QStringLiteral("NICLAV")))))
    {
        data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"),
                            QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"));
        data = data.replace(QByteArrayLiteral(".framesize(csi.VGA)"),
                            QByteArrayLiteral(".framesize(csi.QVGA)"));
    }

    if ((!notExamples) &&
        (m_boardType.startsWith(QStringLiteral("AE3")) || m_boardType.startsWith(QStringLiteral("N6"))) &&
        (m_sensorType.startsWith(QStringLiteral("PAG7936")) || m_sensorType.startsWith(QStringLiteral("PS5520"))))
    {
        data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.QQVGA)"),
                            QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"));
        data = data.replace(QByteArrayLiteral(".framesize(csi.QQVGA)"),
                            QByteArrayLiteral(".framesize(csi.QVGA)"));
    }

    return data;
}

void OpenMVPlugin::flushPortPath()
{
    if(!m_portPath.isEmpty())
    {
#if defined(Q_OS_WIN)
        if (!flushVolume(static_cast<wchar_t>(m_portPath.at(0).unicode())))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  Tr::tr("Disconnect"),
                                  Tr::tr("Failed to flush \"%L1\"!").arg(m_portPath));
        }
#elif defined(Q_OS_LINUX)
        const QByteArray mp = QFile::encodeName(QDir::cleanPath(m_portPath));
        int fd = open(mp.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        bool ok = false;

        if (fd < 0) {
            // Fallback: try opening as a plain file (some FS/mount setups)
            fd = open(mp.constData(), O_RDONLY | O_CLOEXEC);
        }

        if (fd >= 0) {
            ok = (syncfs(fd) == 0);
            close(fd);
        }

        if (!ok) {
            // last-resort coarse global flush
            sync();
            ok = true;
        }

        if (!ok) {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  Tr::tr("Disconnect"),
                                  Tr::tr("Failed to flush \"%L1\"!").arg(m_portPath));
        }
#elif defined(Q_OS_MAC)
        if(sync_volume_np(m_portPath.toUtf8().constData(), SYNC_VOLUME_FULLSYNC | SYNC_VOLUME_WAIT) < 0)
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                                  Tr::tr("Disconnect"),
                                  Tr::tr("Failed to flush \"%L1\"!").arg(m_portPath));
        }
#endif
    }
}

} // namespace Internal
} // namespace OpenMV
