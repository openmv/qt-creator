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

namespace OpenMV {
namespace Internal {

void OpenMVPlugin::processEvents()
{
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
                if((!m_disableFrameBuffer->isChecked()) && (!m_iodevice->frameSizeDumpQueued()) && m_frameSizeDumpTimer.hasExpired(m_frameSizeDumpSpacing) &&
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

            if(m_iodevice->v2ProtocolEnabled() && m_dynamicFrameReading && (!m_dynamicFrameReadingLock))
            {
                m_ioport->getFrameReady();
                m_dynamicFrameReadingLock = true;
            }

            if (m_dynamicFrameReadingPending && (!m_disableFrameBuffer->isChecked()) && (!m_iodevice->frameSizeDumpQueued()))
            {
                m_dynamicFrameReadingPending = false;
                m_frameSizeDumpTimer.restart();
                m_iodevice->frameSizeDump();
            }

            if(m_timer.hasExpired(FPS_TIMER_EXPIRATION_TIME))
            {
                m_fpsButton->setText(Tr::tr("FPS: 0"));
            }
        }
    }
}

void OpenMVPlugin::setPortPath(bool silent)
{
    if(!m_working)
    {
        QStringList drives;

        for(const QPair<QString, QString> &pair : m_availableDrives)
        {
            const QString rootPath = pair.first;
            const QString serialNumber = pair.second;

            if((((m_major < OPENMV_DISK_ADDED_MAJOR)
                  || ((m_major == OPENMV_DISK_ADDED_MAJOR) && (m_minor < OPENMV_DISK_ADDED_MINOR))
                  || ((m_major == OPENMV_DISK_ADDED_MAJOR) && (m_minor == OPENMV_DISK_ADDED_MINOR) && (m_patch < OPENMV_DISK_ADDED_PATCH)))
                 || QFile::exists(rootPath + QStringLiteral(OPENMV_DISK_ADDED_NAME)))
                && (serialNumber.toLower() == m_portDriveSerialNumber.toLower()))
            {
                drives.append(rootPath);
            }
        }

        Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(SERIAL_PORT_SETTINGS_GROUP);

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
                settings->setValue(m_portName.toUtf8(), m_portPath);
            }
        }
        else
        {
            int index = drives.indexOf(settings->value(m_portName.toUtf8()).toString());

            bool ok = silent;
            QString temp = silent ? drives.first() : QInputDialog::getItem(Core::ICore::dialogParent(),
                                                                           Tr::tr("Select Drive"), Tr::tr("Please associate a drive with your OpenMV Cam"),
                                                                           drives, (index != -1) ? index : 0, false, &ok,
                                                                           Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                                                               (Utils::HostOsInfo::isMacHost() ? Qt::WindowType() : Qt::WindowCloseButtonHint));

            if(ok)
            {
                m_portPath = temp;
                settings->setValue(m_portName.toUtf8(), m_portPath);
            }
        }

        settings->endGroup();

        m_pathButton->setText((!m_portPath.isEmpty()) ? Tr::tr("Drive: %L1").arg(m_portPath) : Tr::tr("Drive:"));

        Core::IEditor *editor = Core::EditorManager::currentEditor();
        m_openDriveFolderAction->setEnabled(!m_portPath.isEmpty());
        m_configureSettingsAction->setEnabled(!m_portPath.isEmpty());
        m_saveAction->setEnabled((!m_portPath.isEmpty()) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));

        m_frameBuffer->enableSaveTemplate(!m_portPath.isEmpty());
        m_frameBuffer->enableSaveDescriptor(!m_portPath.isEmpty());

        Python::Internal::PyLSClient::setPortPath(Utils::FilePath::fromUserInput(m_portPath));
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
                              Tr::tr("Select Drive"),
                              Tr::tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::setSpacing()
{
    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(SETTINGS_GROUP);

    bool useGetState = settings->value(LAST_USE_GET_STATE, true).toBool();
    int frameDumpSpacing = settings->value(LAST_FRAME_DUMP_SPACING, FRAME_SIZE_DUMP_SPACING).toInt();
    int getScriptRunningSpacing = settings->value(LAST_GET_SCRIPT_RUNNING_SPACING, GET_SCRIPT_RUNNING_SPACING).toInt();
    int getTxBufferSpacing = settings->value(LAST_GET_TX_BUFFER_SPACING, GET_TX_BUFFER_SPACING).toInt();
    int getStateSpacing = settings->value(LAST_GET_STATE_SPACING, GET_STATE_SPACING).toInt();
    int readProfileSpacing = settings->value(LAST_READ_PROFILE_SPACING, READ_PROFILE_SPACING).toInt();

    int useGetStateAvailable = !((m_major < OPENMV_ADD_GET_STATE_MAJOR)
    || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) &&
        (m_minor < OPENMV_ADD_GET_STATE_MINOR))
    || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) &&
        (m_minor == OPENMV_ADD_GET_STATE_MINOR) &&
        (m_patch < OPENMV_ADD_GET_STATE_PATCH)));

    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                                  Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                      (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog->setWindowTitle(Tr::tr("Debug Protocol Settings"));
    QVBoxLayout *vlayout = new QVBoxLayout(dialog);

    QWidget *mainWidget = new QWidget;
    QHBoxLayout *hlayout = new QHBoxLayout(mainWidget);
    hlayout->setContentsMargins(0, 0, 0, 0);
    vlayout->addWidget(mainWidget);

    QWidget *leftWidget = new QWidget;
    QVBoxLayout *llayout = new QVBoxLayout(leftWidget);
    llayout->setContentsMargins(0, 0, 0, 0);
    hlayout->addWidget(leftWidget);

    QLabel *infoLabelTitle = new QLabel(Tr::tr("Protocol Version %1 - System Info:").arg(m_iodevice->v2ProtocolEnabled() ? 2 : 1));
    infoLabelTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    llayout->addWidget(infoLabelTitle);
    QLabel *infoLabel = new QLabel;
    infoLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    infoLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    infoLabel->setFrameStyle(QFrame::StyledPanel);
    infoLabel->setMinimumWidth(480);
    connect(m_iodevice, &OpenMVPluginIO::systemInfoString, infoLabel, &QLabel::setText);
    m_iodevice->getSystemInfoString();
    llayout->addWidget(infoLabel);

    QWidget *stats = new QWidget;
    QHBoxLayout *statsLayout = new QHBoxLayout(stats);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    llayout->addWidget(stats);

    QWidget *leftHSWidget = new QWidget;
    QVBoxLayout *lhslayout = new QVBoxLayout(leftHSWidget);
    lhslayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->addWidget(leftHSWidget);

    QLabel *hostStatsTitle = new QLabel(Tr::tr("Host Stats:"));
    hostStatsTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    lhslayout->addWidget(hostStatsTitle);
    QLabel *hostStats = new QLabel;
    hostStats->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    hostStats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    hostStats->setFrameStyle(QFrame::StyledPanel);
    hostStats->setMinimumWidth(240);
    connect(m_iodevice, &OpenMVPluginIO::hostStatsString, hostStats, &QLabel::setText);
    QTimer *hostStatsTimer = new QTimer(dialog);
    connect(hostStatsTimer, &QTimer::timeout, m_iodevice, &OpenMVPluginIO::getHostStatsString);
    hostStatsTimer->start(1000);
    m_iodevice->getHostStatsString();
    lhslayout->addWidget(hostStats);

    QWidget *rightDSWidget = new QWidget;
    QVBoxLayout *rdslayout = new QVBoxLayout(rightDSWidget);
    rdslayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->addWidget(rightDSWidget);

    QLabel *deviceStatsTitle = new QLabel(Tr::tr("Device Stats:"));
    deviceStatsTitle->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    rdslayout->addWidget(deviceStatsTitle);
    QLabel *deviceStats = new QLabel;
    deviceStats->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    deviceStats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    deviceStats->setFrameStyle(QFrame::StyledPanel);
    deviceStats->setMinimumWidth(240);
    connect(m_iodevice, &OpenMVPluginIO::deviceStatsString, deviceStats, &QLabel::setText);
    QTimer *deviceStatsTimer = new QTimer(dialog);
    connect(deviceStatsTimer, &QTimer::timeout, m_iodevice, &OpenMVPluginIO::getDeviceStatsString);
    deviceStatsTimer->start(1000);
    m_iodevice->getDeviceStatsString();
    rdslayout->addWidget(deviceStats);

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

    QGroupBox *getStateGroup = new QGroupBox(Tr::tr("Combined Polling"));
    getStateGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    getStateGroup->setCheckable(true);
    getStateGroup->setChecked(useGetState);
    getStateGroup->setEnabled(useGetStateAvailable);
    rlayout->addWidget(getStateGroup);

    QFormLayout *getStateGroupLayout = new QFormLayout(getStateGroup);

    QSpinBox *getStateSpacingBox = new QSpinBox(getStateGroup);
    getStateSpacingBox->setRange(0, 1000);
    getStateSpacingBox->setValue(getStateSpacing);
    getStateGroupLayout->addRow(Tr::tr("Polling (ms)"), getStateSpacingBox);

    QGroupBox *oldStateGroup = new QGroupBox(useGetStateAvailable ? Tr::tr("Split Polling") : Tr::tr("Polling Settings"));
    oldStateGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    if(useGetStateAvailable)
    {
        oldStateGroup->setDisabled(useGetState);
        connect(getStateGroup, &QGroupBox::toggled, oldStateGroup, &QGroupBox::setDisabled);
    }

    rlayout->addWidget(oldStateGroup);

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

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    vlayout->addWidget(box);

    infoLabel->setFocus();

    connect(m_iodevice, &OpenMVPluginIO::closeResponse, dialog, &QDialog::reject);
    connect(dynamicFrameReadingBox, &QCheckBox::toggled, this, [this, frameDumpSpacingBox] (bool checked) {
        frameDumpSpacingBox->setDisabled(m_iodevice->v2ProtocolEnabled() && checked);
    });

    if(dialog->exec() == QDialog::Accepted)
    {
        settings->setValue(LAST_USE_GET_STATE, m_useGetState = getStateGroup->isChecked());
        settings->setValue(LAST_FRAME_DUMP_SPACING, m_frameSizeDumpSpacing = frameDumpSpacingBox->value());
        settings->setValue(LAST_GET_SCRIPT_RUNNING_SPACING, m_getScriptRunningSpacing = getScriptRunningSpacingBox->value());
        settings->setValue(LAST_GET_TX_BUFFER_SPACING, m_getTxBufferSpacing = getTxBufferSpacingBox->value());
        settings->setValue(LAST_GET_STATE_SPACING, m_getStateSpacing = getStateSpacingBox->value());
        settings->setValue(LAST_READ_PROFILE_SPACING, m_readProfileSpacing = readProfileSpacingBox->value());
        settings->setValue(LAST_DYNAMIC_FRAME_READING, m_dynamicFrameReading = dynamicFrameReadingBox->isChecked());

        m_frameSizeDumpTimer.restart();
        m_getScriptRunningTimer.restart();
        m_getTxBufferTimer.restart();
        m_getStateTimer.restart();
        m_readProfileTimer.restart();
        m_timer.restart();
        m_queue.clear();
    }

    settings->endGroup();
    delete dialog;
}

void OpenMVPlugin::saveTemplate(const QRect &rect)
{
    if(!m_working)
    {
        QString drivePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath));

        Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(SETTINGS_GROUP);

        QString path;

        forever
        {
            path =
                QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Save Template"),
                                             settings->value(LAST_SAVE_TEMPLATE_PATH, drivePath).toString(),
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
                    settings->setValue(LAST_SAVE_TEMPLATE_PATH, path);
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                                          Tr::tr("Save Template"),
                                          Tr::tr("\"%L1\" is longer than a max length of %L2 characters!").arg(QString::fromUtf8(sendPath)).arg(DESCRIPTOR_SAVE_PATH_MAX_LEN));
                }
            }
        }

        settings->endGroup();
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
                              Tr::tr("Save Template"),
                              Tr::tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::saveDescriptor(const QRect &rect)
{
    if(!m_working)
    {
        QString drivePath = QDir::cleanPath(QDir::fromNativeSeparators(m_portPath));

        Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(SETTINGS_GROUP);

        QString path;

        forever
        {
            path =
                QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Save Descriptor"),
                                             settings->value(LAST_SAVE_DESCRIPTOR_PATH, drivePath).toString(),
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
                    settings->setValue(LAST_SAVE_DESCRIPTOR_PATH, path);
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                                          Tr::tr("Save Descriptor"),
                                          Tr::tr("\"%L1\" is longer than a max length of %L2 characters!").arg(QString::fromUtf8(sendPath)).arg(DESCRIPTOR_SAVE_PATH_MAX_LEN));
                }
            }
        }

        settings->endGroup();
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
                              Tr::tr("Save Descriptor"),
                              Tr::tr("Busy... please wait..."));
    }
}

QByteArray OpenMVPlugin::fixScriptForSensor(QByteArray data, bool notExamples, bool increaseResolution)
{
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

        if(m_sensorType.startsWith(QStringLiteral("HM01B0")))
        {
            data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"),
                                QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"));
        }

        if((m_sensorType.startsWith(QStringLiteral("BOSON-320"))) ||
            (m_sensorType.startsWith(QStringLiteral("BOSON-320+"))) ||
            (m_sensorType.startsWith(QStringLiteral("PAG7920"))) ||
            (m_sensorType.startsWith(QStringLiteral("PAJ6100"))) ||
            (m_sensorType.startsWith(QStringLiteral("FROGEYE2020"))))
        {
            data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"),
                                QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"));
        }

        if((m_sensorType.startsWith(QStringLiteral("BOSON-640"))) ||
            (m_sensorType.startsWith(QStringLiteral("BOSON-640+"))))
        {
            data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"),
                                QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"));
        }

        if((m_sensorType.startsWith(QStringLiteral("GENX320-S"))) ||
            (m_sensorType.startsWith(QStringLiteral("GENX320"))))
        {
            data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"),
                                QByteArrayLiteral("sensor.set_framesize(sensor.B320X320)"));
            data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"),
                                QByteArrayLiteral("sensor.set_framesize(sensor.B320X320)"));
        }
    }

    if ((!notExamples) &&
        increaseResolution &&
        ((m_sensorType.startsWith(QStringLiteral("PAG7936"))) ||
         (m_sensorType.startsWith(QStringLiteral("PS5520")))))
    {
        data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"),
                            QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"));
    }

    return data;
}

} // namespace Internal
} // namespace OpenMV
