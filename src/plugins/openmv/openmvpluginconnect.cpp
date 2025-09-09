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

#include "app/app_version.h"

namespace OpenMV {
namespace Internal {

static bool removeRecursively(const Utils::FilePath &path, QString *error)
{
    return path.removeRecursively(error);
}

static bool removeRecursivelyWrapper(const Utils::FilePath &path, QString *error)
{
    QEventLoop loop;
    QFutureWatcher<bool> watcher;
    QObject::connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(removeRecursively, path, error));
    loop.exec();
    return watcher.result();
}

static bool removeRecursivelyWrapper(const Utils::FilePath &path, const QList<QString> &subFolders, QString *error)
{
    bool ok;

    for (const QString &subFolder : subFolders)
    {
        ok = removeRecursivelyWrapper(path.pathAppended(subFolder), error);

        if(!ok)
        {
            break;
        }
    }

    return ok;
}

static bool extractAll(QByteArray *data, const QString &path)
{
    QBuffer buffer(data);
    QZipReader reader(&buffer);
    return reader.extractAll(path);
}

static bool extractAllWrapper(QByteArray *data, const QString &path)
{
    QEventLoop loop;
    QFutureWatcher<bool> watcher;
    QObject::connect(&watcher, &QFutureWatcher<bool>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(QtConcurrent::run(extractAll, data, path));
    loop.exec();
    return watcher.result();
}

MyQSerialPortInfo createInfo(const QString &selectedPort)
{
    QSerialPortInfo info(selectedPort);

    if (info.isNull())
    {
        return MyQSerialPortInfo(); // WiFi Port
    }
    else
    {
        return MyQSerialPortInfo(info); // Serial Port
    }
}

bool matchVidPid(const QJsonObject &object, const QString &serialNumberFilter, const MyQSerialPortInfo &port, bool enableSerialNumberInverseFilter = false)
{
    if (port.isNull())
    {
        // This is a WiFi Port - Disable PID/VID matching.
        return true;
    }

    QStringList vidpid = object.value(QStringLiteral("boardVidPid")).toString().split(QStringLiteral(":"));
    int mask = object.value(QStringLiteral("boardPidMask")).toString().toInt(nullptr, 16);
    QStringList serialNumbersInverseFilters;

    for (const QJsonValue &value : object.value(QStringLiteral("boardSerialNumberInverseFilters")).toArray())
    {
        serialNumbersInverseFilters.append(value.toString());
    }

    if((vidpid.size() == 2)
    && (serialNumberFilter.isEmpty() || (serialNumberFilter == port.serialNumber().toUpper()))
    && port.hasVendorIdentifier()
    && port.vendorIdentifier() == vidpid.at(0).toInt(nullptr, 16)
    && port.hasProductIdentifier()
    && (port.productIdentifier() & mask) == vidpid.at(1).toInt(nullptr, 16))
    {
        return enableSerialNumberInverseFilter ? (serialNumbersInverseFilters.isEmpty() || (!serialNumbersInverseFilters.contains(port.serialNumber()))) : true;
    }

    return false;
}

bool validPort(const QJsonDocument &settings, const QString &serialNumberFilter, const MyQSerialPortInfo &port)
{
    for (const QJsonValue &value : settings.object().value(QStringLiteral("boards")).toArray())
    {
        if (matchVidPid(value.toObject(), serialNumberFilter, port, true))
        {
            return true;
        }
    }

    return false;
}

bool isBootloaderType(const QJsonDocument &settings, int testVid, int testPid, const QString &bootloaderType)
{
    bool match = false;

    for (const QJsonValue &value : settings.object().value(QStringLiteral("boards")).toArray())
    {
        QStringList list = value.toObject().value(QStringLiteral("boardVidPid")).toString().split(QStringLiteral(":"));

        if (list.size() == 2)
        {
            int vid = list.at(0).toInt(nullptr, 16);
            int pid = list.at(1).toInt(nullptr, 16);

            if ((testVid == vid)
            && ((testPid & value.toObject().value(QStringLiteral("boardPidMask")).toString().toInt(nullptr, 16)) == pid))
            {
                if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == bootloaderType)
                {
                    match = true;
                }
            }
        }

        if (match)
        {
            break;
        }
    }

    return match;
}

void OpenMVPlugin::packageUpdate()
{
    QNetworkAccessManager *manager = new QNetworkAccessManager(this);

    connect(manager, &QNetworkAccessManager::finished, this, [this, manager] (QNetworkReply *reply) {

        QByteArray data = reply->readAll();

        if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
        {
            QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^(\\d+)\\.(\\d+)\\.(\\d+)$")).match(QString::fromUtf8(data).trimmed());

            if(match.hasMatch())
            {
                int new_major = match.captured(1).toInt();
                int new_minor = match.captured(2).toInt();
                int new_patch = match.captured(3).toInt();

                int old_major = 0;
                int old_minor = 0;
                int old_patch = 0;
                QJsonObject resourcesSettings;

                QFile resourcesSettingsFile(Core::ICore::allUsersResourcePath(QStringLiteral("../%1.json").arg(Core::Constants::IDE_CASED_ID)).toString());

                if (resourcesSettingsFile.open(QFile::ReadOnly))
                {
                    resourcesSettings = QJsonDocument::fromJson(resourcesSettingsFile.readAll()).object();
                    resourcesSettingsFile.close();

                    old_major = resourcesSettings.value(QStringLiteral(RESOURCES_MAJOR)).toInt();
                    old_minor = resourcesSettings.value(QStringLiteral(RESOURCES_MINOR)).toInt();
                    old_patch = resourcesSettings.value(QStringLiteral(RESOURCES_PATCH)).toInt();
                }

                if((old_major < new_major)
                || ((old_major == new_major) && (old_minor < new_minor))
                || ((old_major == new_major) && (old_minor == new_minor) && (old_patch < new_patch)))
                {
                    QMessageBox box(QMessageBox::Information, Tr::tr("Update Available"), Tr::tr("New OpenMV IDE resources are available (e.g. examples, firmware, documentation, etc.)."), QMessageBox::Cancel, Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    QPushButton *button = box.addButton(Tr::tr("Install"), QMessageBox::AcceptRole);
                    box.setDefaultButton(button);
                    box.setEscapeButton(QMessageBox::Cancel);
                    box.exec();

                    if(box.clickedButton() == button)
                    {
                        QProgressDialog *dialog = new QProgressDialog(Tr::tr("Downloading..."), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
                        dialog->setWindowModality(Qt::ApplicationModal);
                        dialog->setAttribute(Qt::WA_ShowWithoutActivating);
                        dialog->setAutoClose(false);

                        QNetworkAccessManager *manager2 = new QNetworkAccessManager(this);

                        connect(manager2, &QNetworkAccessManager::finished, this, [this, manager2, new_major, new_minor, new_patch, dialog] (QNetworkReply *reply2) {
                            QByteArray data2 = reply2->error() == QNetworkReply::NoError ? reply2->readAll() : QByteArray();

                            if((reply2->error() == QNetworkReply::NoError) && (!data2.isEmpty()))
                            {
                                dialog->setLabelText(Tr::tr("Installing..."));
                                dialog->setRange(0, 0);
                                dialog->setCancelButton(Q_NULLPTR);

                                QJsonObject resourcesSettings;

                                QFile resourcesSettingsFile(Core::ICore::allUsersResourcePath(QStringLiteral("../%1.json").arg(Core::Constants::IDE_CASED_ID)).toString());

                                if (resourcesSettingsFile.open(QFile::ReadOnly))
                                {
                                    resourcesSettings = QJsonDocument::fromJson(resourcesSettingsFile.readAll()).object();
                                    resourcesSettingsFile.close();
                                }

                                resourcesSettings[QStringLiteral(RESOURCES_MAJOR)] = 0;
                                resourcesSettings[QStringLiteral(RESOURCES_MINOR)] = 0;
                                resourcesSettings[QStringLiteral(RESOURCES_PATCH)] = 0;

                                bool ok = true;

                                if (resourcesSettingsFile.open(QFile::WriteOnly))
                                {
                                    QByteArray data = QJsonDocument(resourcesSettings).toJson();

                                    if (resourcesSettingsFile.write(data) != data.size())
                                    {
                                        QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                                        QApplication::quit();
                                        ok = false;
                                    }

                                    resourcesSettingsFile.close();
                                }
                                else
                                {
                                    QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                                    QApplication::quit();
                                    ok = false;
                                }

                                if (ok)
                                {
                                    QString error;

                                    if(!removeRecursivelyWrapper(Core::ICore::allUsersResourcePath(), m_resourceFoldersToDelete, &error))
                                    {
                                        QMessageBox::critical(Core::ICore::dialogParent(),
                                            QString(),
                                            error + Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));

                                        QApplication::quit();
                                        ok = false;
                                    }
                                    else
                                    {
                                        if(!extractAllWrapper(&data2, Core::ICore::allUsersResourcePath().toString()))
                                        {
                                            QMessageBox::critical(Core::ICore::dialogParent(),
                                                QString(),
                                                Tr::tr("Please close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));

                                            QApplication::quit();
                                            ok = false;
                                        }
                                    }

                                    if(ok)
                                    {
                                        resourcesSettings[QStringLiteral(RESOURCES_MAJOR)] = new_major;
                                        resourcesSettings[QStringLiteral(RESOURCES_MINOR)] = new_minor;
                                        resourcesSettings[QStringLiteral(RESOURCES_PATCH)] = new_patch;

                                        if (resourcesSettingsFile.open(QFile::WriteOnly))
                                        {
                                            QByteArray data = QJsonDocument(resourcesSettings).toJson();

                                            if (resourcesSettingsFile.write(data) == data.size())
                                            {
                                                resourcesSettingsFile.close();

                                                Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();

                                                // Keep backwards compatibility with old versions of OpenMV IDE.
                                                settings->beginGroup(SETTINGS_GROUP);
                                                settings->setValue(RESOURCES_MAJOR, resourcesSettings.value(QStringLiteral(RESOURCES_MAJOR)).toInt());
                                                settings->setValue(RESOURCES_MINOR, resourcesSettings.value(QStringLiteral(RESOURCES_MINOR)).toInt());
                                                settings->setValue(RESOURCES_PATCH, resourcesSettings.value(QStringLiteral(RESOURCES_PATCH)).toInt());
                                                settings->sync();
                                                settings->endGroup();

                                                if (loadDocs(true, false))
                                                {
                                                    QMessageBox::information(Core::ICore::dialogParent(),
                                                        QString(),
                                                        Tr::tr("Installation Sucessful! Please restart OpenMV IDE."));

                                                    Core::ICore::restart();
                                                }
                                                else
                                                {
                                                    QApplication::quit();
                                                }
                                            }
                                            else
                                            {
                                                resourcesSettingsFile.close();

                                                QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                                                QApplication::quit();
                                            }
                                        }
                                        else
                                        {
                                            QMessageBox::critical(Q_NULLPTR, QString(), Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));
                                            QApplication::quit();
                                        }
                                    }
                                }
                            }
                            else if((reply2->error() != QNetworkReply::NoError) && (reply2->error() != QNetworkReply::OperationCanceledError))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Package Update"),
                                    Tr::tr("Error: %L1!").arg(reply2->errorString()));
                            }
                            else if(reply2->error() != QNetworkReply::OperationCanceledError)
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Package Update"),
                                    Tr::tr("Cannot open the resources file \"%L1\"!").arg(reply2->request().url().toString()));
                            }

                            connect(reply2, &QNetworkReply::destroyed, manager2, &QNetworkAccessManager::deleteLater); reply2->deleteLater();

                            delete dialog;
                        });

                        QNetworkRequest request2 = QNetworkRequest(QUrl(QStringLiteral("https://github.com/openmv/openmv-ide/releases/download/v%1.%2.%3/openmv-ide-resources-%1.%2.%3.zip").arg(new_major).arg(new_minor).arg(new_patch)));
                        QNetworkReply *reply2 = manager2->get(request2);

                        if(reply2)
                        {
                            connect(dialog, &QProgressDialog::canceled, reply2, &QNetworkReply::abort);
                            connect(reply2, &QNetworkReply::sslErrors, reply2, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
                            connect(reply2, &QNetworkReply::downloadProgress, this, [dialog] (qint64 bytesReceived, qint64 bytesTotal) {
                                dialog->setMaximum(bytesTotal);
                                dialog->setValue(bytesReceived);
                            });

                            dialog->show();
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Package Update"),
                                Tr::tr("Network request failed \"%L1\"!").arg(request2.url().toString()));
                        }
                    }
                }
            }
        }

        connect(reply, &QNetworkReply::destroyed, manager, &QNetworkAccessManager::deleteLater); reply->deleteLater();
    });

    QNetworkRequest request = QNetworkRequest(QUrl(QStringLiteral("https://raw.githubusercontent.com/openmv/openmv-ide-version/main/openmv-ide-resources-version-v2.txt")));
    QNetworkReply *reply = manager->get(request);

    if(reply)
    {
        connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
    }
}

void OpenMVPlugin::bootloaderClicked()
{
    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog->setWindowTitle(Tr::tr("Load Custom Firmware"));
    QFormLayout *layout = new QFormLayout(dialog);
    layout->setVerticalSpacing(0);

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(SETTINGS_GROUP);

    Utils::PathChooser *pathChooser = new Utils::PathChooser();
    pathChooser->setExpectedKind(Utils::PathChooser::File);
    pathChooser->setPromptDialogTitle(Tr::tr("Firmware Path"));
    pathChooser->setPromptDialogFilter(Tr::tr("Firmware Binary (*.bin *.dfu *.img)"));
    pathChooser->setFilePath(Utils::FilePath::fromVariant(settings->value(LAST_FIRMWARE_PATH, QDir::homePath())));
    pathChooser->setHistoryCompleter(LAST_FIRMWARE_HISTORY, false);
    layout->addRow(Tr::tr("Firmware Path"), pathChooser);
    layout->addItem(new QSpacerItem(0, 6));

    QHBoxLayout *layout2 = new QHBoxLayout;
    layout2->setContentsMargins(0, 0, 0, 0);
    QWidget *widget = new QWidget;
    widget->setLayout(layout2);

    QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal FAT file system"));
    checkBox->setChecked(settings->value(LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
    layout2->addWidget(checkBox);
    checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal FAT file system will be deleted. "
                                "This does not erase files on any removable SD card (if inserted)."));

    QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Reset ROMFS file system"));
    checkBox2->setChecked(settings->value(LAST_DFU_RESET_ROM_FS_STATE, false).toBool());
    layout2->addWidget(checkBox2);
    checkBox2->setEnabled(!pathChooser->filePath().toString().endsWith(QStringLiteral(".img"), Qt::CaseInsensitive));
    checkBox2->setToolTip(Tr::tr("If you enable this option the ROM file system on your OpenMV Cam will be reset back to default."));

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *run = new QPushButton(Tr::tr("Run"));
    run->setEnabled(pathChooser->isValid());
    box->addButton(run, QDialogButtonBox::AcceptRole);
    layout2->addSpacing(80);
    layout2->addWidget(box);
    layout->addRow(widget);

    connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(pathChooser, &Utils::PathChooser::validChanged, run, &QPushButton::setEnabled);
    connect(pathChooser, &Utils::PathChooser::rawPathChanged, this, [this, dialog, pathChooser, checkBox2] () {
        if(pathChooser->filePath().toString().endsWith(QStringLiteral(".img"), Qt::CaseInsensitive))
        {
            checkBox2->setEnabled(false);
        }
        else
        {
            checkBox2->setEnabled(true);
        }

        QTimer::singleShot(0, this, [dialog] { dialog->adjustSize(); });
    });

    if(dialog->exec() == QDialog::Accepted)
    {
        QString forceFirmwarePath = pathChooser->filePath().toString();
        bool flashFSErase = checkBox->isChecked();
        bool resetROMFS = checkBox2->isEnabled() ? checkBox2->isChecked() : false;

        if(QFileInfo(forceFirmwarePath).exists() && QFileInfo(forceFirmwarePath).isFile())
        {
            settings->setValue(LAST_FIRMWARE_PATH, forceFirmwarePath);
            settings->setValue(LAST_DFU_FLASH_FS_ERASE_STATE, flashFSErase);
            settings->setValue(LAST_DFU_RESET_ROM_FS_STATE, resetROMFS);
            settings->endGroup();
            delete dialog;

            connectClicked(true, forceFirmwarePath, flashFSErase,
                           false, false, false, QString(), resetROMFS ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE);
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Bootloader"),
                Tr::tr("\"%L1\" is not a file!").arg(forceFirmwarePath));

            settings->endGroup();
            delete dialog;
        }
    }
    else
    {
        settings->endGroup();
        delete dialog;
    }
}

void OpenMVPlugin::installTheLatestDevelopmentRelease()
{
    QProgressDialog *dialog = new QProgressDialog(Tr::tr("Downloading..."), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setAttribute(Qt::WA_ShowWithoutActivating);
    dialog->setAutoClose(false);

    QNetworkAccessManager *manager2 = new QNetworkAccessManager(this);

    connect(manager2, &QNetworkAccessManager::finished, this, [this, manager2, dialog] (QNetworkReply *reply2) {
        QByteArray data2 = reply2->error() == QNetworkReply::NoError ? reply2->readAll() : QByteArray();

        if((reply2->error() == QNetworkReply::NoError) && (!data2.isEmpty()))
        {
            int s = data2.indexOf("<div data-pjax=\"true\" data-test-selector=\"body-content\" data-view-component=\"true\" class=\"markdown-body my-3\">");
            int e = data2.indexOf("<div data-view-component=\"true\" class=\"Box-footer\">");

            QByteArray d;

            if((s != -1) and (e != -1))
            {
                d = data2.mid(s, e - s);
                int i = d.lastIndexOf("</div>");
                d = d.mid(0, i - 1);
            }

            QDialog *newDialog = new QDialog(Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            newDialog->setWindowTitle(Tr::tr("Bootloader"));
            QFormLayout *layout = new QFormLayout(newDialog);

            if(!d.isEmpty())
            {
                QTextEdit *edit = new QTextEdit(QString::fromUtf8(d));
                edit->setReadOnly(true);
                layout->addWidget(edit);
            }

            Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
            settings->beginGroup(SETTINGS_GROUP);

            QHBoxLayout *layout2 = new QHBoxLayout;
            layout2->setContentsMargins(0, 0, 0, 0);
            QWidget *widget = new QWidget;
            widget->setLayout(layout2);

            QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal FAT file system"));
            checkBox->setChecked(settings->value(LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
            layout2->addWidget(checkBox);
            checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal FAT file system will be deleted. "
                                        "This does not erase files on any removable SD card (if inserted)."));

            QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Update ROMFS file system"));
            checkBox2->setChecked(settings->value(LAST_DFU_UPDATE_ROM_FS_STATE, false).toBool());
            layout2->addWidget(checkBox2);
            checkBox2->setToolTip(Tr::tr("If you enable this option the ROM file system on your OpenMV Cam will be updated to the latest development release."));

            if((m_major < OPENMV_FORCE_ROMFS_UPGRADE_MAJOR)
            || ((m_major == OPENMV_FORCE_ROMFS_UPGRADE_MAJOR) && (m_minor < OPENMV_FORCE_ROMFS_UPGRADE_MINOR))
            || ((m_major == OPENMV_FORCE_ROMFS_UPGRADE_MAJOR) && (m_minor == OPENMV_FORCE_ROMFS_UPGRADE_MINOR) && (m_patch < OPENMV_FORCE_ROMFS_UPGRADE_PATCH)))
            {
                checkBox->setChecked(true);
                checkBox->setEnabled(false);
                checkBox2->setChecked(true);
                checkBox2->setEnabled(false);

                layout->addRow(new QLabel(Tr::tr("Warning: Upgrading to the new firmware version requires the FAT file system to be erased.")));
            }

            QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
            QPushButton *run = new QPushButton(Tr::tr("Run"));
            box->addButton(run, QDialogButtonBox::AcceptRole);
            layout2->addSpacing(80);
            layout2->addWidget(box);
            layout->addRow(widget);

            connect(box, &QDialogButtonBox::accepted, newDialog, &QDialog::accept);
            connect(box, &QDialogButtonBox::rejected, newDialog, &QDialog::reject);

            if(newDialog->exec() == QDialog::Accepted)
            {
                bool flashFSErase = checkBox->isChecked();
                bool updateROMFS = checkBox2->isChecked();

                if (checkBox->isEnabled()) settings->setValue(LAST_DFU_FLASH_FS_ERASE_STATE, flashFSErase);
                if (checkBox2->isEnabled()) settings->setValue(LAST_DFU_UPDATE_ROM_FS_STATE, updateROMFS);
                settings->endGroup();
                delete newDialog;

                connectClicked(true, QString(), flashFSErase, false, true,
                               false, QString(), updateROMFS ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE);
            }
            else
            {
                settings->endGroup();
                delete newDialog;
            }
        }
        else if((reply2->error() != QNetworkReply::NoError) && (reply2->error() != QNetworkReply::OperationCanceledError))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("Error: %L1!").arg(reply2->errorString()));
        }
        else if(reply2->error() != QNetworkReply::OperationCanceledError)
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("Cannot open the resources file \"%L1\"!").arg(reply2->request().url().toString()));
        }

        connect(reply2, &QNetworkReply::destroyed, manager2, &QNetworkAccessManager::deleteLater); reply2->deleteLater();

        delete dialog;
    });

    QNetworkRequest request2 = QNetworkRequest(QUrl(QStringLiteral("https://github.com/openmv/openmv/releases/tag/development")));
    QNetworkReply *reply2 = manager2->get(request2);

    if(reply2)
    {
        connect(dialog, &QProgressDialog::canceled, reply2, &QNetworkReply::abort);
        connect(reply2, &QNetworkReply::sslErrors, reply2, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
        connect(reply2, &QNetworkReply::downloadProgress, this, [dialog] (qint64 bytesReceived, qint64 bytesTotal) {
            dialog->setMaximum(bytesTotal);
            dialog->setValue(bytesReceived);
        });

        dialog->exec();
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            Tr::tr("Network request failed \"%L1\"!").arg(request2.url().toString()));
    }
}


bool OpenMVPlugin::getTheLatestDevelopmentFirmware(const QString &arch, QString *path, const QString &firmwareFileName, const QString &originalFirmwareFolder)
{
    QProgressDialog *dialog = new QProgressDialog(Tr::tr("Downloading..."), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowType(0)));
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setAttribute(Qt::WA_ShowWithoutActivating);
    dialog->setAutoClose(false);

    QNetworkAccessManager *manager2 = new QNetworkAccessManager(this);

    bool ok = true;
    bool *okPtr = &ok;

    connect(manager2, &QNetworkAccessManager::finished, this, [manager2, dialog, okPtr, path, firmwareFileName, originalFirmwareFolder] (QNetworkReply *reply2) {
        QByteArray data2 = reply2->error() == QNetworkReply::NoError ? reply2->readAll() : QByteArray();

        if((reply2->error() == QNetworkReply::NoError) && (!data2.isEmpty()))
        {
            dialog->setLabelText(Tr::tr("Extracting..."));
            dialog->setRange(0, 0);
            dialog->setCancelButton(Q_NULLPTR);

            if(!extractAllWrapper(&data2, QDir::tempPath()))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    QString(),
                    Tr::tr("Unable to extract firmware!"));

                *okPtr = false;
            }
            else
            {
                *path = QDir::cleanPath(QDir::fromNativeSeparators(QDir::tempPath() + QDir::separator() + firmwareFileName));

                if (firmwareFileName.endsWith(QStringLiteral("lst")))
                {
                    QFile(Core::ICore::allUsersResourcePath(QStringLiteral("firmware"))
                        .pathAppended(originalFirmwareFolder)
                        .pathAppended(firmwareFileName).toString())
                    .copy(QDir::cleanPath(QDir::fromNativeSeparators(QDir::tempPath() + QDir::separator() + firmwareFileName)));
                }
            }
        }
        else if((reply2->error() != QNetworkReply::NoError) && (reply2->error() != QNetworkReply::OperationCanceledError))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("Error: %L1!").arg(reply2->errorString()));

            *okPtr = false;
        }
        else if(reply2->error() != QNetworkReply::OperationCanceledError)
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Connect"),
                Tr::tr("Cannot open the resources file \"%L1\"!").arg(reply2->request().url().toString()));

            *okPtr = false;
        }

        connect(reply2, &QNetworkReply::destroyed, manager2, &QNetworkAccessManager::deleteLater); reply2->deleteLater();

        delete dialog;
    });

    QNetworkRequest request2 = QNetworkRequest(QUrl(QStringLiteral("https://github.com/openmv/openmv/releases/download/development/firmware_%1.zip").arg(arch)));
    QNetworkReply *reply2 = manager2->get(request2);

    if(reply2)
    {
        connect(dialog, &QProgressDialog::canceled, reply2, &QNetworkReply::abort);
        connect(reply2, &QNetworkReply::sslErrors, reply2, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
        connect(reply2, &QNetworkReply::downloadProgress, this, [dialog] (qint64 bytesReceived, qint64 bytesTotal) {
            dialog->setMaximum(bytesTotal);
            dialog->setValue(bytesReceived);
        });

        dialog->exec();

        if(ok)
        {
            return true;
        }
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            Tr::tr("Network request failed \"%L1\"!").arg(request2.url().toString()));
    }

    return false;
}

QList<QPair<QString, QString> > OpenMVPlugin::querySerialPorts(const QStringList &portList)
{
    QList<QPair<QString, QString> > results;

    for (const QString &port : portList)
    {
        QString errorMessage2 = QStringLiteral("Timeout");
        QString *errorMessage2Ptr = &errorMessage2;

        QMetaObject::Connection conn = connect(m_ioport, &OpenMVPluginSerialPort::openResult,
            this, [errorMessage2Ptr] (const QString &errorMessage) {
            *errorMessage2Ptr = errorMessage;
        });

        QEventLoop loop;

        connect(m_ioport, &OpenMVPluginSerialPort::openResult,
                &loop, &QEventLoop::quit);

        m_ioport->open(port);

        QTimer::singleShot(50, &loop, &QEventLoop::quit);

        loop.exec();

        disconnect(conn);

        if(errorMessage2.isEmpty())
        {
            int major2 = int();
            int minor2 = int();
            int patch2 = int();

            int *major2Ptr = &major2;
            int *minor2Ptr = &minor2;
            int *patch2Ptr = &patch2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::firmwareVersion,
                this, [major2Ptr, minor2Ptr, patch2Ptr] (int major, int minor, int patch) {
                *major2Ptr = major;
                *minor2Ptr = minor;
                *patch2Ptr = patch;
            });

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::firmwareVersion,
                    &loop, &QEventLoop::quit);

            m_iodevice->getFirmwareVersion();

            QTimer::singleShot(20, &loop, &QEventLoop::quit);

            loop.exec();

            disconnect(conn);

            if((!major2) && (!minor2) && (!patch2))
            {
                results.append(QPair<QString, QString>(port, Tr::tr("Unknown Board")));
                continue;
            }
            else if((major2 < 0) || (100 < major2) || (minor2 < 0) || (100 < minor2) || (patch2 < 0) || (100 < patch2))
            {
                results.append(QPair<QString, QString>(port, Tr::tr("Unknown Board")));
                continue;
            }

            if(((major2 > OLD_API_MAJOR)
            || ((major2 == OLD_API_MAJOR) && (minor2 > OLD_API_MINOR))
            || ((major2 == OLD_API_MAJOR) && (minor2 == OLD_API_MINOR) && (patch2 >= OLD_API_PATCH))))
            {
                QString arch2 = QString();
                QString *arch2Ptr = &arch2;

                QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::archString,
                    this, [arch2Ptr] (const QString &arch) {
                    *arch2Ptr = arch;
                });

                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::archString,
                        &loop, &QEventLoop::quit);

                m_iodevice->getArchString();

                QTimer::singleShot(20, &loop, &QEventLoop::quit);

                loop.exec();

                disconnect(conn);

                if(!arch2.isEmpty())
                {
                    QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified();
                    MyQSerialPortInfo tempInfo = createInfo(port);

                    bool found = false;

                    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        if((value.toObject().value(QStringLiteral("boardArchString")).toString().toLower() == temp.toLower())
                        && matchVidPid(value.toObject(), QString(), tempInfo))
                        {
                            results.append(QPair<QString, QString>(port, value.toObject().value(QStringLiteral("boardDisplayName")).toString()));
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        results.append(QPair<QString, QString>(port, Tr::tr("Unknown Board")));
                    }
                }
                else
                {
                    results.append(QPair<QString, QString>(port, Tr::tr("Unknown Board")));
                }
            }
            else
            {
                results.append(QPair<QString, QString>(port, Tr::tr("Unknown Board")));
            }
        }
        else
        {
            results.append(QPair<QString, QString>(port, Tr::tr("Unknown Board")));
        }
    }

    return results;
}

QPair<QStringList, QStringList> filterPorts(const QJsonDocument &settings,
                                            const QString &serialNumberFilter,
                                            bool forceBootloader,
                                            const QList<wifiPort_t> &availableWifiPorts)
{
    QStringList stringList, dfuDevices;

    for(QSerialPortInfo raw_port : QSerialPortInfo::availablePorts())
    {
        MyQSerialPortInfo port(raw_port);

        if(validPort(settings, serialNumberFilter, port))
        {
            stringList.append(port.portName());
        }
    }

    if(Utils::HostOsInfo::isMacHost())
    {
        stringList = stringList.filter(QStringLiteral("cu"), Qt::CaseInsensitive);
    }

    if(!forceBootloader)
    {
        for(wifiPort_t port : availableWifiPorts)
        {
            stringList.append(QString(QStringLiteral("%1:%2")).arg(port.name).arg(port.addressAndPort));
        }
    }

    dfuDevices = picotoolGetDevices() + imxGetAllDevices(settings) + alifGetDevices(settings);

    for(const QString &device : getDevices())
    {
        dfuDevices.append(device);
        QStringList vidpid = device.split(QStringLiteral(",")).first().split(QStringLiteral(":"));

        for(QList<QString>::iterator it = stringList.begin(); it != stringList.end(); )
        {
            QSerialPortInfo raw_info = QSerialPortInfo(*it);
            MyQSerialPortInfo info(raw_info);

            if(info.hasVendorIdentifier()
            && info.vendorIdentifier() == vidpid.at(0).toInt(nullptr, 16)
            && info.hasProductIdentifier()
            && info.productIdentifier() == vidpid.at(1).toInt(nullptr, 16))
            {
                it = stringList.erase(it);
            }
            else
            {
                it++;
            }
        }
    }

    // Move known bootloader serial ports to dfuDevices.
    {
        for(QList<QString>::iterator it = stringList.begin(); it != stringList.end(); )
        {
            QSerialPortInfo raw_info = QSerialPortInfo(*it);
            MyQSerialPortInfo info(raw_info);

            bool vidpidMatch = false, altvidpidMatch = false;

            for (const QJsonValue &value : settings.object().value(QStringLiteral("boards")).toArray())
            {
                QJsonObject object = value.toObject();
                QString bootloaderType = object.value(QStringLiteral("bootloaderType")).toString();
                if ((bootloaderType == QStringLiteral("picotool")) || (bootloaderType == QStringLiteral("bossac")))
                {
                    QStringList vidpid = object.value(QStringLiteral("bootloaderVidPid")).toString().split(QStringLiteral(":"));
                    QJsonObject bootloaderSettings = object.value(QStringLiteral("bootloaderSettings")).toObject();
                    QStringList altvidpid = bootloaderSettings.value(QStringLiteral("altvidpid")).toString().split(QStringLiteral(":"));

                    if((vidpid.size() == 2)
                    && info.hasVendorIdentifier()
                    && info.vendorIdentifier() == vidpid.at(0).toInt(nullptr, 16)
                    && info.hasProductIdentifier()
                    && info.productIdentifier()== vidpid.at(1).toInt(nullptr, 16))
                    {
                        dfuDevices.append(QString(QStringLiteral("%1:%2").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0')).arg(info.productIdentifier(), 4, 16, QLatin1Char('0'))));
                        vidpidMatch = true;
                    }

                    if((altvidpid.size() == 2)
                    && info.hasVendorIdentifier()
                    && info.vendorIdentifier() == altvidpid.at(0).toInt(nullptr, 16)
                    && info.hasProductIdentifier()
                    && info.productIdentifier()== altvidpid.at(1).toInt(nullptr, 16))
                    {
                        dfuDevices.append(QString(QStringLiteral("%1:%2").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0')).arg(info.productIdentifier(), 4, 16, QLatin1Char('0'))));
                        altvidpidMatch = true;
                    }
                }
            }

            if(vidpidMatch || altvidpidMatch || info.isNull())
            {
                it = stringList.erase(it);
            }
            else
            {
                it++;
            }
        }
    }

    return QPair<QStringList, QStringList>(stringList, dfuDevices);
}

void OpenMVPlugin::connectClicked(bool forceBootloader,
                                  QString forceFirmwarePath,
                                  bool forceFlashFSErase,
                                  bool justEraseFlashFs,
                                  bool installTheLatestDevelopmentFirmware,
                                  bool waitForCamera,
                                  QString previousMapping,
                                  OpenMVROMFSAccess romfsAccess)
{
    if(!m_working)
    {
        if(m_connect_disconnect)
        {
            disconnect(m_connect_disconnect);
        }

        if(m_connected)
        {
            m_connect_disconnect = connect(this, &OpenMVPlugin::disconnectDone, this,
                [this, forceBootloader, forceFirmwarePath, forceFlashFSErase, justEraseFlashFs, installTheLatestDevelopmentFirmware, waitForCamera, previousMapping, romfsAccess] {
                QTimer::singleShot(0, this, [this, forceBootloader, forceFirmwarePath, forceFlashFSErase, justEraseFlashFs, installTheLatestDevelopmentFirmware, waitForCamera, previousMapping, romfsAccess] {
                    connectClicked(forceBootloader, forceFirmwarePath, forceFlashFSErase, justEraseFlashFs, installTheLatestDevelopmentFirmware, waitForCamera, previousMapping, romfsAccess);
                });
            });

            QTimer::singleShot(0, this, [this] {disconnectClicked();});

            return;
        }

        m_working = true;

        QStringList stringList;
        QElapsedTimer waitForCameraTimeout;
        waitForCameraTimeout.start();
        QStringList dfuDevices;

        QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

        do
        {
            QPair<QStringList, QStringList> output = filterPorts(m_firmwareSettings, m_serialNumberFilter, forceBootloader, m_availableWifiPorts);
            stringList = output.first;
            dfuDevices = output.second;
            if(waitForCamera && stringList.isEmpty()) QApplication::processEvents();
        }
        while(waitForCamera && stringList.isEmpty() && (!waitForCameraTimeout.hasExpired(10000)));

        QApplication::restoreOverrideCursor();

        Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(SETTINGS_GROUP);

        QString selectedPort;
        bool forceBootloaderBricked = false;
        bool previousMappingSet = !previousMapping.isEmpty();
        QString originalFirmwareFolder = QString();
        QString firmwarePath = forceFirmwarePath;
        int originalEraseFlashSectorStart = FLASH_SECTOR_START;
        int originalEraseFlashSectorEnd = FLASH_SECTOR_END;
        int originalEraseFlashSectorAllStart = FLASH_SECTOR_ALL_START;
        int originalEraseFlashSectorAllEnd = FLASH_SECTOR_ALL_END;
        QJsonObject originalFallbackBootloaderSettings = QJsonObject();
        QString originalDfuVidPid = QString();
        bool dfuNoDialogs = false;
        bool repairingBootloader = false;

        if(stringList.isEmpty())
        {
            if(forceBootloader && (!dfuDevices.isEmpty())
            && (m_autoUpdate != QStringLiteral("release"))
            && (m_autoUpdate != QStringLiteral("developement")))
            {
                forceBootloaderBricked = true;
            }
            else
            {
                bool dfuDeviceResetToRelease = false;
                bool dfuDeviceEraseFlash = false;

                if(!dfuDevices.isEmpty())
                {
                    QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).
                            match(m_firmwareSettings.object().value(QStringLiteral("firmware_version")).toString());

                    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                    dialog->setWindowTitle(Tr::tr("Connect"));
                    QFormLayout *layout = new QFormLayout(dialog);
                    layout->setVerticalSpacing(0);

                    layout->addWidget(new QLabel(Tr::tr("A board in DFU mode was detected. What would you like to do?")));
                    layout->addItem(new QSpacerItem(0, 6));

                    QComboBox *combo = new QComboBox();
                    combo->addItem(Tr::tr("Install the lastest release firmware (v%L1.%L2.%L3)").arg(match.captured(1).toInt()).arg(match.captured(2).toInt()).arg(match.captured(3).toInt()));
                    combo->addItem(Tr::tr("Load a specific firmware"));
                    combo->addItem(Tr::tr("Just erase the internal FAT file system"));
                    combo->addItem(Tr::tr("Edit the ROM file system"));
                    combo->addItem(Tr::tr("Reset the ROM file system"));
                    combo->setCurrentIndex(settings->value(LAST_DFU_ACTION, 0).toInt());
                    layout->addWidget(combo);
                    layout->addItem(new QSpacerItem(0, 6));

                    QHBoxLayout *layout2 = new QHBoxLayout;
                    layout2->setContentsMargins(0, 0, 0, 0);
                    QWidget *widget = new QWidget;
                    widget->setLayout(layout2);

                    QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal FAT file system"));
                    checkBox->setChecked(settings->value(LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
                    layout2->addWidget(checkBox);
                    checkBox->setVisible(combo->currentIndex() == 0);
                    checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal FAT file system will be deleted. "
                                                "This does not erase files on any removable SD card (if inserted)."));

                    QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Reset ROMFS file system"));
                    checkBox2->setChecked(settings->value(LAST_DFU_RESET_ROM_FS_STATE, false).toBool());
                    layout2->addWidget(checkBox2);
                    checkBox2->setVisible(combo->currentIndex() == 0);
                    checkBox2->setToolTip(Tr::tr("If you enable this option the ROM file system on your OpenMV Cam will be reset back to default."));

                    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
                    layout2->addSpacing(80);
                    layout2->addWidget(box);
                    layout->addRow(widget);

                    connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
                    connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
                    connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, dialog, checkBox, checkBox2] (int index) {
                        checkBox->setVisible(index == 0);
                        checkBox2->setVisible(index == 0);
                        QTimer::singleShot(0, this, [dialog] { dialog->adjustSize(); });
                    });

                    // If a board is in DFU mode we install the release firmware.
                    if((m_autoUpdate == QStringLiteral("release"))
                    || (m_autoUpdate == QStringLiteral("developement")))
                    {
                        combo->setCurrentIndex(0);
                        checkBox->setChecked(m_autoErase);
                    }

                    if((m_autoUpdate == QStringLiteral("release"))
                    || (m_autoUpdate == QStringLiteral("developement"))
                    || (dialog->exec() == QDialog::Accepted))
                    {
                        settings->setValue(LAST_DFU_ACTION, combo->currentIndex());
                        settings->setValue(LAST_DFU_FLASH_FS_ERASE_STATE, checkBox->isChecked());
                        settings->setValue(LAST_DFU_RESET_ROM_FS_STATE, checkBox2->isChecked());

                        if(combo->currentIndex() == 0)
                        {
                            dfuDeviceResetToRelease = true;
                            dfuDeviceEraseFlash = checkBox->isChecked();
                            dfuNoDialogs = true;
                            romfsAccess = checkBox2->isChecked() ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE;
                        }
                        else if(combo->currentIndex() == 1)
                        {
                            QTimer::singleShot(0, m_bootloaderAction, &QAction::trigger);
                        }
                        else if(combo->currentIndex() == 2)
                        {
                            QTimer::singleShot(0, m_eraseAction, &QAction::trigger);
                        }
                        else if(combo->currentIndex() == 3)
                        {
                            QTimer::singleShot(0, this, [this] { editRomfsClicked(true); });
                        }
                        else if(combo->currentIndex() == 3)
                        {
                            QTimer::singleShot(0, this, [this] { resetRomfsClicked(); });
                        }
                    }

                    delete dialog;

                    if(!dfuDeviceResetToRelease)
                    {
                        settings->endGroup();
                        CONNECT_END();
                    }
                }

                if((!forceBootloader) && (!dfuDeviceResetToRelease)) QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("No OpenMV Cams found!"));

                QMap<QString, QString> mappings;
                QMap<QString, QPair<int, int> > eraseMappings;
                QMap<QString, QPair<int, int> > eraseAllMappings;
                QMap<QString, QJsonObject> fallbackBootloaderMappings;
                QMap<QString, QString> vidpidMappings;
                QMap<QString, QString> defaultFirmwareNameMappings;

                for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                {
                    if (dfuDeviceResetToRelease || (!value.toObject().value(QStringLiteral("hidden")).toBool()))
                    {
                        QString a = value.toObject().value(QStringLiteral("boardDisplayName")).toString();
                        mappings.insert(a, value.toObject().value(QStringLiteral("boardFirmwareFolder")).toString());
                        vidpidMappings.insert(a, value.toObject().value(QStringLiteral("bootloaderVidPid")).toString());
                        defaultFirmwareNameMappings.insert(a, value.toObject().value(QStringLiteral("defaultFirmwareName")).toString());

                        if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("internal"))
                        {
                            QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                            int eraseSectorStart = bootloaderSettings.value(QStringLiteral("eraseSectorStart")).toInt();
                            int eraseSectorEnd = bootloaderSettings.value(QStringLiteral("eraseSectorEnd")).toInt();
                            int eraseAllSectorStart = bootloaderSettings.value(QStringLiteral("eraseAllSectorStart")).toInt();
                            int eraseAllSectorEnd = bootloaderSettings.value(QStringLiteral("eraseAllSectorEnd")).toInt();
                            eraseMappings.insert(a, QPair<int, int>(eraseSectorStart, eraseSectorEnd));
                            eraseAllMappings.insert(a, QPair<int, int>(eraseAllSectorStart, eraseAllSectorEnd));
                            fallbackBootloaderMappings.insert(a, bootloaderSettings.value(QStringLiteral("fallbackBootloader")).toObject());
                        }
                        else if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu"))
                        {
                            QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                            eraseMappings.insert(a, QPair<int, int>(0, 0));
                            eraseAllMappings.insert(a, QPair<int, int>(0, 0));
                            fallbackBootloaderMappings.insert(a, bootloaderSettings.value(QStringLiteral("fallbackBootloader")).toObject());
                        }
                        else
                        {
                            eraseMappings.insert(a, QPair<int, int>(0, 0));
                            eraseAllMappings.insert(a, QPair<int, int>(0, 0));
                            fallbackBootloaderMappings.insert(a, QJsonObject());
                        }
                    }
                }

                if(!mappings.isEmpty())
                {
                    if(forceBootloader || dfuDeviceResetToRelease || (QMessageBox::question(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Do you have an OpenMV Cam connected and is it bricked?"),
                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
                    == QMessageBox::Yes))
                    {
                        if(dfuDeviceResetToRelease)
                        {
                            for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                            {
                                QJsonObject object = value.toObject();
                                QString bootloaderType = object.value(QStringLiteral("bootloaderType")).toString();

                                if ((!object.value(QStringLiteral("hidden")).toBool())
                                && ((bootloaderType == QStringLiteral("picotool")) || (bootloaderType == QStringLiteral("bossac"))))
                                {
                                    QString boardFirmwareFolder = object.value(QStringLiteral("boardFirmwareFolder")).toString();
                                    QJsonObject bootloaderSettings = object.value(QStringLiteral("bootloaderSettings")).toObject();
                                    QString altvidpidDisplayName = bootloaderSettings.value(QStringLiteral("altvidpidDisplayName")).toString();
                                    QString altvidpid = bootloaderSettings.value(QStringLiteral("altvidpid")).toString();
                                    QString defaultFirmwareName = object.value(QStringLiteral("defaultFirmwareName")).toString();

                                    mappings.insert(altvidpidDisplayName, boardFirmwareFolder);
                                    eraseMappings.insert(altvidpidDisplayName, QPair<int, int>(0, 0));
                                    eraseAllMappings.insert(altvidpidDisplayName, QPair<int, int>(0, 0));
                                    fallbackBootloaderMappings.insert(altvidpidDisplayName, QJsonObject());
                                    vidpidMappings.insert(altvidpidDisplayName, altvidpid);
                                    defaultFirmwareNameMappings.insert(altvidpidDisplayName, defaultFirmwareName);
                                }
                            }

                            for(QMap<QString, QString>::iterator it = mappings.begin(); it != mappings.end(); )
                            {
                                bool found = false;

                                for(const QString &device : dfuDevices)
                                {
                                    if(device.split(QStringLiteral(",")).first().toLower() == vidpidMappings.value(it.key()).toLower())
                                    {
                                        found = true;
                                        break;
                                    }
                                }

                                if(!found)
                                {
                                    eraseMappings.remove(it.key());
                                    eraseAllMappings.remove(it.key());
                                    fallbackBootloaderMappings.remove(it.key());
                                    vidpidMappings.remove(it.key());
                                    defaultFirmwareNameMappings.remove(it.key());
                                    it = mappings.erase(it);
                                }
                                else
                                {
                                    it++;
                                }
                            }
                        }

                        if(mappings.size())
                        {
                            int index = mappings.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE).toString());

                            bool previousMappingAvailable = previousMappingSet && mappings.contains(previousMapping);
                            bool ok = previousMappingAvailable || (mappings.size() == 1);
                            QString temp = previousMappingAvailable ? previousMapping :
                                ((mappings.size() == 1) ? mappings.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),
                                Tr::tr("Connect"), Tr::tr("Please select the board type"),
                                mappings.keys(), (index != -1) ? index : 0, false, &ok,
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint)));

                            if(ok)
                            {
                                if (!previousMappingAvailable) settings->setValue(LAST_BOARD_TYPE_STATE, temp);

                                QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                                dialog->setWindowTitle(Tr::tr("Connect"));
                                QFormLayout *layout = new QFormLayout(dialog);
                                layout->setVerticalSpacing(0);

                                layout->addWidget(new QLabel(Tr::tr("Upgrade options:")));
                                layout->addItem(new QSpacerItem(0, 6));

                                QHBoxLayout *layout2 = new QHBoxLayout;
                                layout2->setContentsMargins(6, 0, 0, 0);
                                QWidget *widget = new QWidget;
                                widget->setLayout(layout2);

                                QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal FAT file system"));
                                checkBox->setChecked(settings->value(LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
                                layout2->addWidget(checkBox);
                                checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal FAT file system will be deleted. "
                                                            "This does not erase files on any removable SD card (if inserted)."));

                                QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Reset ROMFS file system"));
                                checkBox2->setChecked(settings->value(LAST_DFU_RESET_ROM_FS_STATE, false).toBool());
                                layout2->addWidget(checkBox2);
                                checkBox2->setToolTip(Tr::tr("If you enable this option the ROM file system on your OpenMV Cam will be reset back to default."));

                                layout->addRow(widget);
                                layout->addItem(new QSpacerItem(0, 6));

                                QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
                                layout->addWidget(box);

                                connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
                                connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

                                bool ok = (!forceFirmwarePath.isEmpty()) || justEraseFlashFs ||
                                    (forceBootloader && previousMappingSet) || dfuDeviceResetToRelease || (dialog->exec() == QDialog::Accepted);
                                bool forceFlashFSEraseTemp = justEraseFlashFs ||
                                    ((forceBootloader && previousMappingSet) ? forceFlashFSErase : (dfuDeviceResetToRelease ? dfuDeviceEraseFlash : checkBox->isChecked()));
                                OpenMVROMFSAccess romfsAccessTemp = justEraseFlashFs ? OPENMV_ROMFS_NONE :
                                    ((forceBootloader && previousMappingSet) ? romfsAccess : (dfuDeviceResetToRelease ? romfsAccess : (checkBox2->isChecked() ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE)));

                                if (ok)
                                {
                                    settings->setValue(LAST_DFU_FLASH_FS_ERASE_STATE, checkBox->isChecked());
                                    settings->setValue(LAST_DFU_RESET_ROM_FS_STATE, checkBox2->isChecked());
                                    previousMapping = temp;
                                    originalFirmwareFolder = mappings.value(temp);
                                    firmwarePath = Core::ICore::allUsersResourcePath(QStringLiteral("firmware"))
                                        .pathAppended(originalFirmwareFolder)
                                        .pathAppended(defaultFirmwareNameMappings.value(temp)).toString();
                                    if (forceBootloader && (!forceFirmwarePath.isEmpty())) firmwarePath = forceFirmwarePath;
                                    forceBootloader = true;
                                    forceFlashFSErase = forceFlashFSEraseTemp;
                                    forceBootloaderBricked = true;
                                    originalEraseFlashSectorStart = eraseMappings.value(temp).first;
                                    originalEraseFlashSectorEnd = eraseMappings.value(temp).second;
                                    originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;
                                    originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;
                                    originalFallbackBootloaderSettings = fallbackBootloaderMappings.value(temp);
                                    originalDfuVidPid = vidpidMappings.value(temp);
                                    romfsAccess = romfsAccessTemp;
                                }

                                delete dialog;
                            }
                        }
                        else
                        {
                            bool end = false;

                            for(const QString &device : dfuDevices)
                            {
                                QStringList vidpid = device.split(QStringLiteral(",")).first().split(QStringLiteral(":"));

                                if(vidpid.size() == 2)
                                {
                                    if((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID))
                                    {
                                        QMessageBox::critical(Core::ICore::dialogParent(),
                                            Tr::tr("Connect"),
                                            Tr::tr("Please update the bootloader to the latest version and install the SoftDevice to flash the OpenMV firmware. More information can be found on <a href=\"https://docs.arduino.cc\">https://docs.arduino.cc</a>"));

                                        end = true;
                                        break;
                                    }

                                    if((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID))
                                    {
                                        QMessageBox::critical(Core::ICore::dialogParent(),
                                            Tr::tr("Connect"),
                                            Tr::tr("Please short REC to GND and reset your board. More information can be found on <a href=\"https://docs.arduino.cc\">https://docs.arduino.cc</a>"));

                                        end = true;
                                        break;
                                    }
                                }
                            }

                            if (!end)
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                                      Tr::tr("Connect"),
                                                      Tr::tr("No released firmware available for the attached board!"));
                            }
                        }
                    }
                }
            }
        }
        else if(stringList.size() == 1)
        {
            selectedPort = stringList.first();
            settings->setValue(LAST_SERIAL_PORT_STATE, selectedPort);
        }
        else
        {
            int index = stringList.indexOf(settings->value(LAST_SERIAL_PORT_STATE).toString());

            QList<QPair<QString, QString> > prettyNames = querySerialPorts(stringList);

            QStringList stringList2;

            for (const QPair<QString, QString> &pair : prettyNames)
            {
                stringList2.append(QStringLiteral("%1: %2").arg(pair.first).arg(pair.second));
            }

            bool ok;
            QString temp = QInputDialog::getItem(Core::ICore::dialogParent(),
                Tr::tr("Connect"), Tr::tr("Please select a serial port"),
                stringList2, (index != -1) ? index : 0, false, &ok,
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

            if(ok)
            {
                selectedPort = temp.split(QStringLiteral(":")).first();
                settings->setValue(LAST_SERIAL_PORT_STATE, selectedPort);
            }
        }

        settings->endGroup();

        if((!forceBootloaderBricked) && selectedPort.isEmpty())
        {
            CONNECT_END();
        }

        QString selectedDfuDevice;

        if(forceBootloaderBricked && (!dfuDevices.isEmpty()))
        {
            settings->beginGroup(SETTINGS_GROUP);

            if(dfuDevices.size() == 1)
            {
                selectedDfuDevice = dfuDevices.first();
                settings->setValue(LAST_DFU_PORT_STATE, selectedDfuDevice);
            }
            else
            {
                int index = dfuDevices.indexOf(settings->value(LAST_DFU_PORT_STATE).toString());

                bool ok;
                QString temp = QInputDialog::getItem(Core::ICore::dialogParent(),
                    Tr::tr("Connect"), Tr::tr("Please select a DFU Device"),
                    dfuDevices, (index != -1) ? index : 0, false, &ok,
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                if(ok)
                {
                    selectedDfuDevice = temp;
                    settings->setValue(LAST_DFU_PORT_STATE, selectedDfuDevice);
                }
            }

            if(selectedDfuDevice.isEmpty())
            {
                CONNECT_END();
            }

            settings->endGroup();
        }

        bool isOpenMVDfu = false;
        bool isIMX = false;
        bool isAlif = false;
        bool isArduinoDFU = false;
        bool isBossac = false;
        bool isPicotool = false;
        bool isTTR = false;
        bool isPortenta = false;
        bool isNiclav = false;
        bool isGiga = false;
        bool isNRF = false;
        bool isRPIPico = false;

        if(!selectedPort.isEmpty())
        {
            QSerialPortInfo raw_arduinoPort = QSerialPortInfo(selectedPort);
            MyQSerialPortInfo arduinoPort(raw_arduinoPort);

            isTTR = isTouchToReset(m_firmwareSettings, arduinoPort);

            if (arduinoPort.hasVendorIdentifier() && arduinoPort.hasProductIdentifier())
            {
                isArduinoDFU = isBootloaderType(m_firmwareSettings, arduinoPort.vendorIdentifier(), arduinoPort.productIdentifier(), QStringLiteral("arduino_dfu"));
                isBossac = isBootloaderType(m_firmwareSettings, arduinoPort.vendorIdentifier(), arduinoPort.productIdentifier(), QStringLiteral("bossac"));
                isPicotool = isBootloaderType(m_firmwareSettings, arduinoPort.vendorIdentifier(), arduinoPort.productIdentifier(), QStringLiteral("picotool"));

                // These need to be hardcoded to prevent disabling the license check.

                isPortenta = (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && ((arduinoPort.productIdentifier() == PORTENTA_APP_O_PID) ||
                                                                                   (arduinoPort.productIdentifier() == PORTENTA_APP_N_PID));
                isNiclav = (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && ((arduinoPort.productIdentifier() == NICLA_APP_O_PID) ||
                                                                                  (arduinoPort.productIdentifier() == NICLA_APP_N_PID));
                isGiga = (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && (arduinoPort.productIdentifier() == GIGA_APP_PID);
                isNRF = (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && (arduinoPort.productIdentifier() == NRF_APP_PID);
                isRPIPico = ((arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && (arduinoPort.productIdentifier() == RPI_APP_PID)) ||
                          ((arduinoPort.vendorIdentifier() == RPI2040_VID) && (arduinoPort.productIdentifier() == RPI2040_PID));
            }
        }

        if(!selectedDfuDevice.isEmpty())
        {
            QStringList vidpid = selectedDfuDevice.split(QStringLiteral(",")).first().split(QStringLiteral(":"));

            isOpenMVDfu = dfuVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isIMX = imxVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isAlif = alifVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isArduinoDFU = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("arduino_dfu"));
            isBossac = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("bossac"));
            isPicotool = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("picotool"));

            // These need to be hardcoded to prevent disabling the license check.

            isPortenta = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == PORTENTA_LDR_PID);
            isNiclav = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NICLA_LDR_PID);
            isGiga = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == GIGA_LDR_PID);
            isNRF = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == NRF_LDR_PID));
            isRPIPico = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == RPI_LDR_PID))) ||
                        ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));

            if((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Please update the bootloader to the latest version and install the SoftDevice to flash the OpenMV firmware. More information can be found on <a href=\"https://docs.arduino.cc\">https://docs.arduino.cc</a>"));

                CONNECT_END();
            }

            if((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Please short REC to GND and reset your board. More information can be found on <a href=\"https://docs.arduino.cc\">https://docs.arduino.cc</a>"));

                CONNECT_END();
            }
        }

        if((!originalDfuVidPid.isEmpty()) && selectedPort.isEmpty() && dfuDevices.isEmpty())
        {
            QStringList vidpid = originalDfuVidPid.split(QStringLiteral(":"));

            isOpenMVDfu = dfuVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isIMX = imxVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isAlif = alifVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isArduinoDFU = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("arduino_dfu"));
            isBossac = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("bossac"));
            isPicotool = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("picotool"));

            // These need to be hardcoded to prevent disabling the license check.

            isPortenta = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == PORTENTA_LDR_PID);
            isNiclav = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NICLA_LDR_PID);
            isGiga = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == GIGA_LDR_PID);
            isNRF = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == NRF_LDR_PID));
            isRPIPico = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == RPI_LDR_PID))) ||
                        ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));

            if(isOpenMVDfu || isIMX || isAlif || isArduinoDFU || isBossac || isPicotool)
            {
                selectedDfuDevice = originalDfuVidPid + QStringLiteral(",NULL");
            }

            if((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Please update the bootloader to the latest version and install the SoftDevice to flash the OpenMV firmware. More information can be found on <a href=\"https://docs.arduino.cc\">https://docs.arduino.cc</a>"));

                CONNECT_END();
            }

            if((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Please short REC to GND and reset your board. More information can be found on <a href=\"https://docs.arduino.cc\">https://docs.arduino.cc</a>"));

                CONNECT_END();
            }
        }

        if (dfuNoDialogs && (!isOpenMVDfu) && (!isIMX) && (!isAlif) && (!isArduinoDFU) && (!isBossac) && (!isPicotool)) {
            firmwarePath = QFileInfo(firmwarePath).path() + QStringLiteral("/bootloader.dfu");
            repairingBootloader = true;
        }

        // Open Port //////////////////////////////////////////////////////////

        m_iodevice->setHighSpeed(false);

        if(!forceBootloaderBricked)
        {
            QString errorMessage2 = QString();
            QString *errorMessage2Ptr = &errorMessage2;

            QMetaObject::Connection conn = connect(m_ioport, &OpenMVPluginSerialPort::openResult,
                this, [errorMessage2Ptr] (const QString &errorMessage) {
                *errorMessage2Ptr = errorMessage;
            });

            QProgressDialog dialog(Tr::tr("Connecting...\n\n(Hit cancel if this takes more than 5 seconds)."), Tr::tr("Cancel"), 0, 0, Core::ICore::dialogParent(),
               Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::CustomizeWindowHint |
               (Utils::HostOsInfo::isLinuxHost() ? Qt::WindowDoesNotAcceptFocus : Qt::WindowType(0)));
            dialog.setWindowModality(Qt::ApplicationModal);
            dialog.setAttribute(Qt::WA_ShowWithoutActivating);

            forever
            {
                QEventLoop loop;

                connect(m_ioport, &OpenMVPluginSerialPort::openResult,
                        &loop, &QEventLoop::quit);

                m_ioport->open(selectedPort);

                loop.exec();

                if(errorMessage2.isEmpty() || (Utils::HostOsInfo::isLinuxHost() && errorMessage2.contains(QStringLiteral("Permission Denied"), Qt::CaseInsensitive)))
                {
                    break;
                }

                dialog.show();

                QApplication::processEvents();

                if(dialog.wasCanceled())
                {
                    break;
                }
            }

            dialog.close();

            disconnect(conn);

            if(!errorMessage2.isEmpty())
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Error: %L1!").arg(errorMessage2));

                if(Utils::HostOsInfo::isLinuxHost() && errorMessage2.contains(QStringLiteral("Permission Denied"), Qt::CaseInsensitive))
                {
                    QMessageBox::information(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Try doing:\n\n") + QString(QStringLiteral("sudo adduser %L1 dialout\n\n")).arg(Utils::Environment::systemEnvironment().toDictionary().userName()) + Tr::tr("...in a terminal and then restart your computer."));
                }

                CONNECT_END();
            }
        }

        if(isTTR)
        {
            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                    &loop, &QEventLoop::quit);

            m_iodevice->close();

            loop.exec();

            QElapsedTimer elaspedTimer;
            elaspedTimer.start();

            while(!elaspedTimer.hasExpired(RESET_TO_DFU_SEARCH_TIME))
            {
                QApplication::processEvents();
            }

            RECONNECT_END();
        }

        // Get Version ////////////////////////////////////////////////////////

        int major2 = int();
        int minor2 = int();
        int patch2 = int();

        if(!forceBootloaderBricked)
        {
            int *major2Ptr = &major2;
            int *minor2Ptr = &minor2;
            int *patch2Ptr = &patch2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::firmwareVersion,
                this, [major2Ptr, minor2Ptr, patch2Ptr] (int major, int minor, int patch) {
                *major2Ptr = major;
                *minor2Ptr = minor;
                *patch2Ptr = patch;
            });

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::firmwareVersion,
                    &loop, &QEventLoop::quit);

            m_iodevice->getFirmwareVersion();

            loop.exec();

            disconnect(conn);

            if((!major2) && (!minor2) && (!patch2))
            {
                if(m_reconnects < RECONNECTS_MAX)
                {
                    m_reconnects += 1;

                    QThread::msleep(10);
                    CLOSE_RECONNECT_END();
                }

                m_reconnects = 0;

                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Timeout error while getting firmware version!"));

                QMessageBox::warning(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Do not try to connect while the green light on your OpenMV Cam is on!"));

                if(QMessageBox::question(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Try to connect again?"),
                    QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
                == QMessageBox::Yes)
                {
                    CLOSE_RECONNECT_END();
                }

                CLOSE_CONNECT_END();
            }
            else if((major2 < 0) || (100 < major2) || (minor2 < 0) || (100 < minor2) || (patch2 < 0) || (100 < patch2))
            {
                CLOSE_RECONNECT_END();
            }

            m_reconnects = 0;

            if(((major2 > OLD_API_MAJOR)
            || ((major2 == OLD_API_MAJOR) && (minor2 > OLD_API_MINOR))
            || ((major2 == OLD_API_MAJOR) && (minor2 == OLD_API_MINOR) && (patch2 >= OLD_API_PATCH))))
            {
                QString arch2 = QString();
                QString *arch2Ptr = &arch2;

                QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::archString,
                    this, [arch2Ptr] (const QString &arch) {
                    *arch2Ptr = arch;
                });

                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::archString,
                        &loop, &QEventLoop::quit);

                m_iodevice->getArchString();

                loop.exec();

                disconnect(conn);

                if(!arch2.isEmpty())
                {
                    QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified();
                    MyQSerialPortInfo tempInfo = createInfo(selectedPort);

                    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        if((value.toObject().value(QStringLiteral("boardArchString")).toString().toLower() == temp.toLower())
                        && matchVidPid(value.toObject(), QString(), tempInfo))
                        {
                            m_iodevice->setHighSpeed(value.toObject().value(QStringLiteral("highSpeed")).toBool());

                            if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu"))
                            {
                                isOpenMVDfu = true;
                                originalDfuVidPid = value.toObject().value(QStringLiteral("bootloaderVidPid")).toString();
                                break;
                            }

                            if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("imx"))
                            {
                                isIMX = true;
                                break;
                            }

                            break;
                        }
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Timeout error while getting board architecture!"));

                    CLOSE_CONNECT_END();
                }
            }
        }

        // Bootloader /////////////////////////////////////////////////////////

        if(forceBootloader)
        {
            if(!forceBootloaderBricked)
            {
                if((major2 < OLD_API_MAJOR)
                || ((major2 == OLD_API_MAJOR) && (minor2 < OLD_API_MINOR))
                || ((major2 == OLD_API_MAJOR) && (minor2 == OLD_API_MINOR) && (patch2 < OLD_API_PATCH)))
                {
                    if(firmwarePath.isEmpty()) firmwarePath = Core::ICore::allUsersResourcePath(QStringLiteral("firmware"))
                        .pathAppended(QStringLiteral(OLD_API_BOARD))
                        .pathAppended(QStringLiteral("firmware.bin")).toString();

                    if(installTheLatestDevelopmentFirmware)
                    {
                        if(!getTheLatestDevelopmentFirmware(QStringLiteral(OLD_API_BOARD), &firmwarePath, QStringLiteral("firmware"), OLD_API_BOARD))
                        {
                            CLOSE_CONNECT_END();
                        }
                    }
                }
                else
                {
                    QString arch2 = QString();
                    QString *arch2Ptr = &arch2;

                    QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::archString,
                        this, [arch2Ptr] (const QString &arch) {
                        *arch2Ptr = arch;
                    });

                    QEventLoop loop;

                    connect(m_iodevice, &OpenMVPluginIO::archString,
                            &loop, &QEventLoop::quit);

                    m_iodevice->getArchString();

                    loop.exec();

                    disconnect(conn);

                    if(!arch2.isEmpty())
                    {
                        QMap<QString, QString> mappings;
                        QMap<QString, QString> mappingsHumanReadable;
                        QMap<QString, QPair<int, int> > eraseMappings;
                        QMap<QString, QPair<int, int> > eraseAllMappings;
                        QMap<QString, QJsonObject> fallbackBootloaderMappings;
                        QMap<QString, QString> vidpidMappings;
                        QMap<QString, QString> defaultFirmwareNameMapping;

                        MyQSerialPortInfo tempInfo = createInfo(selectedPort);

                        for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                        {
                            if (matchVidPid(value.toObject(), QString(), tempInfo))
                            {
                                QString a = value.toObject().value(QStringLiteral("boardArchString")).toString();
                                mappings.insert(a, value.toObject().value(QStringLiteral("boardFirmwareFolder")).toString());
                                mappingsHumanReadable.insert(value.toObject().value(QStringLiteral("boardDisplayName")).toString(), a);
                                vidpidMappings.insert(a, value.toObject().value(QStringLiteral("bootloaderVidPid")).toString());
                                defaultFirmwareNameMapping.insert(a, value.toObject().value(QStringLiteral("defaultFirmwareName")).toString());

                                if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("internal"))
                                {
                                    QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                                    int eraseSectorStart = bootloaderSettings.value(QStringLiteral("eraseSectorStart")).toInt();
                                    int eraseSectorEnd = bootloaderSettings.value(QStringLiteral("eraseSectorEnd")).toInt();
                                    int eraseAllSectorStart = bootloaderSettings.value(QStringLiteral("eraseAllSectorStart")).toInt();
                                    int eraseAllSectorEnd = bootloaderSettings.value(QStringLiteral("eraseAllSectorEnd")).toInt();
                                    eraseMappings.insert(a, QPair<int, int>(eraseSectorStart, eraseSectorEnd));
                                    eraseAllMappings.insert(a, QPair<int, int>(eraseAllSectorStart, eraseAllSectorEnd));
                                    fallbackBootloaderMappings.insert(a, bootloaderSettings.value(QStringLiteral("fallbackBootloader")).toObject());
                                }
                                else if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu"))
                                {
                                    QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                                    eraseMappings.insert(a, QPair<int, int>(0, 0));
                                    eraseAllMappings.insert(a, QPair<int, int>(0, 0));
                                    fallbackBootloaderMappings.insert(a, bootloaderSettings.value(QStringLiteral("fallbackBootloader")).toObject());
                                }
                                else
                                {
                                    eraseMappings.insert(a, QPair<int, int>(0, 0));
                                    eraseAllMappings.insert(a, QPair<int, int>(0, 0));
                                    fallbackBootloaderMappings.insert(a, QJsonObject());
                                }
                            }
                        }

                        QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified();

                        if(!mappings.contains(temp))
                        {
                            int index = mappingsHumanReadable.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE_2).toString());

                            bool ok = mappingsHumanReadable.size() == 1;
                            temp = (mappingsHumanReadable.size() == 1) ? mappingsHumanReadable.keys().first() : QInputDialog::getItem(Core::ICore::dialogParent(),
                                Tr::tr("Connect"), Tr::tr("Please select the board type"),
                                mappingsHumanReadable.keys(), (index != -1) ? index : 0, false, &ok,
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                            if(ok)
                            {
                                settings->setValue(LAST_BOARD_TYPE_STATE_2, temp);
                            }
                            else
                            {
                                CLOSE_CONNECT_END();
                            }

                            temp = mappingsHumanReadable.value(temp); // Get mappings key.
                        }

                        if(firmwarePath.isEmpty())
                        {
                            previousMapping = temp;
                            originalFirmwareFolder = mappings.value(temp);
                            firmwarePath = Core::ICore::allUsersResourcePath(QStringLiteral("firmware"))
                                .pathAppended(originalFirmwareFolder)
                                .pathAppended(defaultFirmwareNameMapping.value(temp)).toString();
                            if (forceBootloader && (!forceFirmwarePath.isEmpty())) firmwarePath = forceFirmwarePath;
                            originalEraseFlashSectorStart = eraseMappings.value(temp).first;
                            originalEraseFlashSectorEnd = eraseMappings.value(temp).second;
                            originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;
                            originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;
                            originalFallbackBootloaderSettings = fallbackBootloaderMappings.value(temp);


                            if(installTheLatestDevelopmentFirmware)
                            {
                                if(!getTheLatestDevelopmentFirmware(mappings.value(temp), &firmwarePath, defaultFirmwareNameMapping.value(temp), originalFirmwareFolder))
                                {
                                    CLOSE_CONNECT_END();
                                }
                            }
                        }
                        else
                        {
                            previousMapping = temp;
                            originalFirmwareFolder = mappings.value(temp);
                            originalEraseFlashSectorStart = eraseMappings.value(temp).first;
                            originalEraseFlashSectorEnd = eraseMappings.value(temp).second;
                            originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;
                            originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;
                            originalFallbackBootloaderSettings = fallbackBootloaderMappings.value(temp);
                        }

                        QStringList vidpid = vidpidMappings.value(temp).split(QStringLiteral(":"));
                        isOpenMVDfu = dfuVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
                        isIMX = imxVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
                        isAlif = alifVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
                        isArduinoDFU = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("arduino_dfu"));
                        isBossac = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("bossac"));
                        isPicotool = isBootloaderType(m_firmwareSettings, vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16), QStringLiteral("picotool"));

                        // These need to be hardcoded to prevent disabling the license check.

                        isPortenta = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == PORTENTA_LDR_PID);
                        isNiclav = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NICLA_LDR_PID);
                        isGiga = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == GIGA_LDR_PID);
                        isNRF = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == NRF_LDR_PID));
                        isRPIPico = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == RPI_LDR_PID))) ||
                                    ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));

                        QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(.+?)\\[(.+?):(.+?)\\]")).match(arch2);

                        if(match.hasMatch())
                        {
                            m_boardTypeFolder = originalFirmwareFolder;
                            m_fullBoardType = match.captured(1).trimmed();
                            m_boardType = match.captured(2);
                            m_boardId = match.captured(3);
                            m_boardVID = vidpid.at(0).toInt(nullptr, 16);
                            m_boardPID = vidpid.at(1).toInt(nullptr, 16);
                        }
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("Connect"),
                            Tr::tr("Timeout error while getting board architecture!"));

                        CLOSE_CONNECT_END();
                    }
                }

                if((!isOpenMVDfu)
                && (!isIMX)
                && (!isAlif)
                && (!isArduinoDFU)
                && (!isBossac)
                && (!isPicotool)
                && firmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
                {
                    QEventLoop loop;

                    connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                            &loop, &QEventLoop::quit);

                    m_iodevice->close();

                    loop.exec();
                }
            }

            if ((!isOpenMVDfu)
            && (!isIMX)
            && (!isAlif)
            && (!isArduinoDFU)
            && (!isBossac)
            && (!isPicotool)
            && (justEraseFlashFs ||
                firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive) ||
                firmwarePath.endsWith(QStringLiteral(".img"), Qt::CaseInsensitive)))
            {
                QStringList vidpid = QString(selectedDfuDevice).split(QStringLiteral(",")).first().split(QStringLiteral(":"));

                if((vidpid.size() == 2) && (vidpid.at(0).toInt(nullptr, 16) == STM32_DFU_VID) && (vidpid.at(1).toInt(nullptr, 16) == STM32_DFU_PID))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Only loading *.dfu files is supported for the STM32 recovery bootloader!\n\n"
                               "Please select a bootloader.dfu file and try again. "
                               "Note that loading the firmware.dfu or openmv.dfu (bootloader + firmware) "
                               "may not work on STM32H7 boards due to a bug in the chip's ROM bootloader!"));

                    CONNECT_END();
                }

                openmvInternalBootloader(forceFirmwarePath,
                                         forceFlashFSErase,
                                         justEraseFlashFs,
                                         installTheLatestDevelopmentFirmware,
                                         previousMapping,
                                         selectedPort,
                                         forceBootloaderBricked,
                                         previousMappingSet,
                                         originalFirmwareFolder,
                                         firmwarePath,
                                         originalEraseFlashSectorStart,
                                         originalEraseFlashSectorEnd,
                                         originalEraseFlashSectorAllStart,
                                         originalEraseFlashSectorAllEnd,
                                         originalFallbackBootloaderSettings,
                                         originalDfuVidPid,
                                         dfuNoDialogs,
                                         romfsAccess);
                return;
            }

            if(isOpenMVDfu)
            {
                if ((!firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive))
                && (!firmwarePath.endsWith(QStringLiteral(".img"), Qt::CaseInsensitive))
                && (!firmwarePath.endsWith(QStringLiteral(".lst"), Qt::CaseInsensitive))
                && (!firmwarePath.isEmpty()))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Only loading *.bin and *.img files are supported for the internal bootloader!"));

                    CONNECT_END();
                }

                if (originalFallbackBootloaderSettings.value(QStringLiteral("type")) == QStringLiteral("internal"))
                {
                    QJsonObject fallbackSettings = originalFallbackBootloaderSettings.value(QStringLiteral("settings")).toObject();
                    QJsonObject dfuFallbackSettings;
                    dfuFallbackSettings.insert(QStringLiteral("vidpid"), originalDfuVidPid);
                    dfuFallbackSettings.insert(QStringLiteral("type"), QJsonValue::fromVariant(QStringLiteral("openmv_dfu")));

                    openmvInternalBootloader(forceFirmwarePath,
                                             forceFlashFSErase,
                                             justEraseFlashFs,
                                             installTheLatestDevelopmentFirmware,
                                             previousMapping,
                                             selectedPort,
                                             forceBootloaderBricked,
                                             previousMappingSet,
                                             originalFirmwareFolder,
                                             firmwarePath,
                                             fallbackSettings.value(QStringLiteral("eraseAllSectorStart")).toInt(),
                                             fallbackSettings.value(QStringLiteral("eraseAllSectorEnd")).toInt(),
                                             fallbackSettings.value(QStringLiteral("eraseSectorStart")).toInt(),
                                             fallbackSettings.value(QStringLiteral("eraseSectorEnd")).toInt(),
                                             dfuFallbackSettings,
                                             fallbackSettings.value(QStringLiteral("vidpid")).toString(),
                                             dfuNoDialogs,
                                             romfsAccess);
                }
                else
                {
                    openmvDFUBootloader(forceFlashFSErase,
                                        justEraseFlashFs,
                                        installTheLatestDevelopmentFirmware,
                                        firmwarePath,
                                        selectedDfuDevice,
                                        romfsAccess);
                }

                return;
            }

            if(isIMX)
            {
                if ((!firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive))
                && (!firmwarePath.endsWith(QStringLiteral(".img"), Qt::CaseInsensitive))
                && (!firmwarePath.isEmpty()))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Only loading *.bin and *.img files are supported for the IMX bootloader!"));

                    CONNECT_END();
                }

                openmvIMXBootloader(forceFirmwarePath,
                                    forceFlashFSErase,
                                    justEraseFlashFs,
                                    installTheLatestDevelopmentFirmware,
                                    firmwarePath,
                                    settings,
                                    forceBootloaderBricked,
                                    originalFirmwareFolder,
                                    selectedDfuDevice,
                                    romfsAccess);
                return;
            }

            if (isAlif)
            {
                openmvAlifBootloader(forceFirmwarePath,
                                     forceFlashFSErase,
                                     justEraseFlashFs,
                                     settings,
                                     originalFirmwareFolder,
                                     selectedDfuDevice);
                return;
            }

            if (isArduinoDFU)
            {
                if ((!firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive))
                && (!firmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
                && (!firmwarePath.endsWith(QStringLiteral(".img"), Qt::CaseInsensitive))
                && (!firmwarePath.isEmpty()))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Only loading *.bin, *.dfu, and *.img files are supported for the Arduino bootloader!"));

                    CONNECT_END();
                }

                openmvArduinoDFUBootloader(forceFlashFSErase,
                                           justEraseFlashFs,
                                           installTheLatestDevelopmentFirmware,
                                           firmwarePath,
                                           selectedDfuDevice,
                                           romfsAccess);
                return;
            }

            if (isBossac)
            {
                if (!firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)
                && (!firmwarePath.isEmpty()))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Only loading *.bin files is supported for the Bossac bootloader!"));

                    CONNECT_END();
                }

                openmvBossacBootloader(forceFlashFSErase,
                                       justEraseFlashFs,
                                       firmwarePath,
                                       selectedDfuDevice,
                                       romfsAccess);
                return;
            }

            if (isPicotool)
            {
                if (!firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)
                && (!firmwarePath.isEmpty()))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Only loading *.bin files is supported for the Picotool bootloader!"));

                    CONNECT_END();
                }

                openmvPictotoolBootloader(forceFlashFSErase,
                                          justEraseFlashFs,
                                          firmwarePath,
                                          selectedDfuDevice,
                                          romfsAccess);
                return;
            }

            if(firmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
            {
                QStringList vidpid = QString(selectedDfuDevice).split(QStringLiteral(",")).first().split(QStringLiteral(":"));

                if((vidpid.size() == 2) && (vidpid.at(0).toInt(nullptr, 16) == STM32_DFU_VID) && (vidpid.at(1).toInt(nullptr, 16) == STM32_DFU_PID))
                {
                    if (QFileInfo(firmwarePath).baseName() != QStringLiteral("bootloader"))
                    {
                        if (QMessageBox::warning(Core::ICore::dialogParent(),
                                                 Tr::tr("Connect"),
                                                 Tr::tr("Note that loading the firmware.dfu or openmv.dfu (bootloader + firmware) "
                                                        "may not work on STM32H7 boards due to a bug in the chip's ROM bootloader!\n\n"
                                                        "OpenMV recommends only loading the bootloader.dfu to repair the bootloader."),
                                                 QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok) != QMessageBox::Ok)
                        {
                            CONNECT_END();
                        }
                    }
                }

                for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                {
                    QJsonObject object = value.toObject();

                    if (object.value(QStringLiteral("hidden")).toBool()
                    && (previousMapping.toLower() == object.value(QStringLiteral("boardDisplayName")).toString().toLower()))
                    {
                        previousMapping = object.value(QStringLiteral("boardArchString")).toString();
                        break;
                    }
                }

                openmvRepairingBootloader(forceFlashFSErase,
                                          previousMapping,
                                          originalDfuVidPid,
                                          dfuNoDialogs,
                                          firmwarePath,
                                          repairingBootloader,
                                          originalFallbackBootloaderSettings.value(QStringLiteral("useSTCubeProgrammer")).toBool());
                return;
            }

            CONNECT_END();
        }

        // Check ID ///////////////////////////////////////////////////////////

        bool disableLicenseCheck = false;
        if(isPortenta || isNiclav || isGiga || isNRF || isRPIPico) disableLicenseCheck = true;
        m_sensorType = QString();

        if((major2 < OPENMV_DBG_PROTOCOL_CHNAGE_MAJOR)
        || ((major2 == OPENMV_DBG_PROTOCOL_CHNAGE_MAJOR) && (minor2 < OPENMV_DBG_PROTOCOL_CHNAGE_MINOR))
        || ((major2 == OPENMV_DBG_PROTOCOL_CHNAGE_MAJOR) && (minor2 == OPENMV_DBG_PROTOCOL_CHNAGE_MINOR) && (patch2 < OPENMV_DBG_PROTOCOL_CHNAGE_PATCH)))
        {
            m_iodevice->breakUpGetAttributeCommand(false);
            m_iodevice->breakUpSetAttributeCommand(false);
            m_iodevice->breakUpFBEnable(false);
            m_iodevice->breakUpJPEGEnable(false);
        }
        else
        {
            m_iodevice->breakUpGetAttributeCommand(true);
            m_iodevice->breakUpSetAttributeCommand(true);
            m_iodevice->breakUpFBEnable(true);
            m_iodevice->breakUpJPEGEnable(true);

            // Get Sensor Type
            {
                int id2 = int();
                int *id2Ptr = &id2;

                QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::sensorIdDone,
                    this, [id2Ptr] (int id) {
                    *id2Ptr = id;
                });

                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::sensorIdDone,
                        &loop, &QEventLoop::quit);

                m_iodevice->sensorId();

                loop.exec();

                disconnect(conn);

                if(id2 == 0xFF)
                {
                    disableLicenseCheck = true;
                    m_sensorType = Tr::tr("None");
                }
                else if(id2)
                {
                    bool found = false;

                    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("sensors")).toArray())
                    {
                        if (int(value.toObject().value(QStringLiteral("id")).toString().toLongLong(nullptr, 0)) == id2)
                        {
                            m_sensorType = value.toObject().value(QStringLiteral("name")).toString();
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                    {
                        m_sensorType = Tr::tr("Unknown");
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Timeout error while getting sensor type!"));

                    disableLicenseCheck = true;
                    m_sensorType = Tr::tr("Unknown");
                }
            }
        }

        if((major2 < OPENMV_RGB565_BYTE_REVERSAL_FIXED_MAJOR)
        || ((major2 == OPENMV_RGB565_BYTE_REVERSAL_FIXED_MAJOR) && (minor2 < OPENMV_RGB565_BYTE_REVERSAL_FIXED_MINOR))
        || ((major2 == OPENMV_RGB565_BYTE_REVERSAL_FIXED_MAJOR) && (minor2 == OPENMV_RGB565_BYTE_REVERSAL_FIXED_MINOR) && (patch2 < OPENMV_RGB565_BYTE_REVERSAL_FIXED_PATCH)))
        {
            m_iodevice->rgb565ByteReservedEnable(true);
        }
        else
        {
            m_iodevice->rgb565ByteReservedEnable(false);
        }

        if((major2 < OPENMV_NEW_PIXFORMAT_MAJOR)
        || ((major2 == OPENMV_NEW_PIXFORMAT_MAJOR) && (minor2 < OPENMV_NEW_PIXFORMAT_MINOR))
        || ((major2 == OPENMV_NEW_PIXFORMAT_MAJOR) && (minor2 == OPENMV_NEW_PIXFORMAT_MINOR) && (patch2 < OPENMV_NEW_PIXFORMAT_PATCH)))
        {
            m_iodevice->newPixformatEnable(false);
        }
        else
        {
            m_iodevice->newPixformatEnable(true);
        }

        if((major2 < OPENMV_ADD_MAIN_TERMINAL_INPUT_MAJOR)
        || ((major2 == OPENMV_ADD_MAIN_TERMINAL_INPUT_MAJOR) && (minor2 < OPENMV_ADD_MAIN_TERMINAL_INPUT_MINOR))
        || ((major2 == OPENMV_ADD_MAIN_TERMINAL_INPUT_MAJOR) && (minor2 == OPENMV_ADD_MAIN_TERMINAL_INPUT_MINOR) && (patch2 < OPENMV_ADD_MAIN_TERMINAL_INPUT_PATCH)))
        {
            m_iodevice->mainTerminalInputEnable(false);
        }
        else
        {
            m_iodevice->mainTerminalInputEnable(true);
        }

        if((major2 < OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_MAJOR)
        || ((major2 == OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_MAJOR) && (minor2 < OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_MINOR))
        || ((major2 == OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_MAJOR) && (minor2 == OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_MINOR) && (patch2 < OPENMV_ADD_GET_STATE_VARAIBLE_SIZE_PATCH)))
        {
            m_iodevice->setGetStateVariableSize(false);
        }
        else
        {
            m_iodevice->setGetStateVariableSize(true);
        }

        m_boardTypeFolder = QString();
        m_fullBoardType = QString();
        m_boardType = QString();
        m_boardId = QString();
        m_boardVID = 0;
        m_boardPID = 0;

        QString boardTypeLabel = Tr::tr("Unknown");

        if(((major2 > OLD_API_MAJOR)
        || ((major2 == OLD_API_MAJOR) && (minor2 > OLD_API_MINOR))
        || ((major2 == OLD_API_MAJOR) && (minor2 == OLD_API_MINOR) && (patch2 >= OLD_API_PATCH))))
        {
            QString arch2 = QString();
            QString *arch2Ptr = &arch2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::archString,
                this, [arch2Ptr] (const QString &arch) {
                *arch2Ptr = arch;
            });

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::archString,
                    &loop, &QEventLoop::quit);

            m_iodevice->getArchString();

            loop.exec();

            disconnect(conn);

            if(!arch2.isEmpty())
            {
                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(.+?)\\[(.+?):(.+?)\\]")).match(arch2);

                if(match.hasMatch())
                {
                    QSerialPortInfo raw_tempPort = QSerialPortInfo(selectedPort);
                    MyQSerialPortInfo tempPort(raw_tempPort);

                    QString board = match.captured(2);
                    QString id = match.captured(3);

                    m_fullBoardType = match.captured(1).trimmed();
                    m_boardType = board;
                    m_boardId = id;
                    m_boardVID = tempPort.vendorIdentifier();
                    m_boardPID = tempPort.productIdentifier();

                    boardTypeLabel = board;

                    QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified();

                    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        if ((value.toObject().value(QStringLiteral("boardArchString")).toString().toLower() == temp.toLower())
                        && matchVidPid(value.toObject(), QString(), tempPort))
                        {
                            boardTypeLabel = value.toObject().value(QStringLiteral("boardDisplayName")).toString();
                            m_boardTypeFolder = value.toObject().value(QStringLiteral("boardFirmwareFolder")).toString();
                            break;
                        }
                    }

                    if((!m_formKey.isEmpty())
                    // Skip OpenMV Cam M4's...
                    || ((!disableLicenseCheck) && (board != QStringLiteral("M4"))))
                    {
                        QNetworkAccessManager *manager = new QNetworkAccessManager(this);

                        connect(manager, &QNetworkAccessManager::finished, this, [this, manager, board, id] (QNetworkReply *reply) {

                            QByteArray data = reply->readAll();

                            if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
                            {
                                if(QString::fromUtf8(data).contains(QStringLiteral("<p>Yes</p>")))
                                {
                                    if (!m_formKey.isEmpty())
                                    {
                                        m_registerButton->setProperty("statusColor",
                                            Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ?
                                                                        QStringLiteral("lightgreen") :
                                                                        QStringLiteral("green"));
                                        m_registerButton->setText(Tr::tr("Registered"));
                                        m_registerButton->setVisible(true);
                                        m_registerButtonSpacer->setVisible(true);
                                    }
                                    else
                                    {
                                        m_registerButton->setText(QString());
                                        m_registerButton->setVisible(false);
                                        m_registerButtonSpacer->setVisible(false);
                                    }
                                }
                                else if(QString::fromUtf8(data).contains(QStringLiteral("<p>No</p>")))
                                {
                                    m_registerButton->setProperty("statusColor",
                                        Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ?
                                                                    QStringLiteral("lightcoral") :
                                                                    QStringLiteral("coral"));
                                    m_registerButton->setText(Tr::tr("Unregistered"));
                                    m_registerButton->update();
                                    m_registerButton->setVisible(true);
                                    m_registerButtonSpacer->setVisible(true);

                                    QTimer::singleShot(0, this, [this, board, id] { registerOpenMVCam(board, id); });
                                }
                                else if((!m_formKey.isEmpty()) && (!QString::fromUtf8(data).contains(QStringLiteral("<p>Yes</p>"))))
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        Tr::tr("Register OpenMV Cam"),
                                        Tr::tr("Database Error!"));

                                    CLOSE_CONNECT_END();
                                }
                            }
                            else if((!m_formKey.isEmpty()) && (reply->error() != QNetworkReply::NoError))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Register OpenMV Cam"),
                                    Tr::tr("Error: %L1!").arg(reply->error()));

                                CLOSE_CONNECT_END();
                            }
                            else if(!m_formKey.isEmpty())
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Register OpenMV Cam"),
                                    Tr::tr("GET Network error!"));

                                CLOSE_CONNECT_END();
                            }

                            connect(reply, &QNetworkReply::destroyed, manager, &QNetworkAccessManager::deleteLater); reply->deleteLater();
                        });

                        QNetworkRequest request = QNetworkRequest(QUrl(QString(QStringLiteral("https://upload.openmv.io/openmv-swd-ids-check.php?board=%1&id=%2")).arg(board).arg(id)));
                        QNetworkReply *reply = manager->get(request);

                        if(reply)
                        {
                            connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
                        }
                        else if(!m_formKey.isEmpty())
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Register OpenMV Cam"),
                                Tr::tr("GET network error!"));

                            CLOSE_CONNECT_END();
                        }
                    }
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Timeout error while getting board architecture!"));

                CLOSE_CONNECT_END();
            }
        }

        if((major2 > LEARN_MTU_ADDED_MAJOR)
        || ((major2 == LEARN_MTU_ADDED_MAJOR) && (minor2 > LEARN_MTU_ADDED_MINOR))
        || ((major2 == LEARN_MTU_ADDED_MAJOR) && (minor2 == LEARN_MTU_ADDED_MINOR) && (patch2 >= LEARN_MTU_ADDED_PATCH)))
        {
            bool ok2 = bool();
            bool *ok2Ptr = &ok2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::learnedMTU,
                this, [ok2Ptr] (bool ok) {
                *ok2Ptr = ok;
            });

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::learnedMTU,
                    &loop, &QEventLoop::quit);

            m_iodevice->learnMTU();

            loop.exec();

            disconnect(conn);

            if(!ok2)
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("Timeout error while learning MTU!"));

                CLOSE_CONNECT_END();
            }
        }

        // Stopping ///////////////////////////////////////////////////////////

        if(m_stopOnConnectDiconnectionAction->isChecked()) m_iodevice->scriptStop();

        if((!m_useGetState)
        || (major2 < OPENMV_ADD_GET_STATE_MAJOR)
        || ((major2 == OPENMV_ADD_GET_STATE_MAJOR) && (minor2 < OPENMV_ADD_GET_STATE_MINOR))
        || ((major2 == OPENMV_ADD_GET_STATE_MAJOR) && (minor2 == OPENMV_ADD_GET_STATE_MINOR) && (patch2 < OPENMV_ADD_GET_STATE_PATCH)))
        {
            // Drain text buffer
            {
                bool empty = bool();
                bool *emptyPtr = &empty;

                QEventLoop loop2;

                QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                    this, [emptyPtr] (bool ok) {
                    *emptyPtr = ok;
                });

                connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                        &loop2, &QEventLoop::quit);

                while(!empty)
                {
                    m_iodevice->getTxBuffer();
                    loop2.exec();
                }

                disconnect(conn2);
            }

            // Drain image buffer
            {
                bool empty = bool();
                bool *emptyPtr = &empty;

                QEventLoop loop2;

                QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                    this, [emptyPtr] (bool ok) {
                    *emptyPtr = ok;
                });

                connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                        &loop2, &QEventLoop::quit);

                while(!empty)
                {
                    m_iodevice->frameSizeDump();
                    loop2.exec();
                }

                disconnect(conn2);
            }
        } else {
            bool printEmpty = bool(), frameBufferEmpty = bool();
            bool *printEmptyPtr = &printEmpty, *frameBufferEmptyPtr = &frameBufferEmpty;

            QEventLoop loop2;

            QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                this, [printEmptyPtr] (bool ok) {
                *printEmptyPtr = ok;
            });

            QMetaObject::Connection conn3 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                this, [frameBufferEmptyPtr] (bool ok) {
                *frameBufferEmptyPtr = ok;
            });

            connect(m_iodevice, &OpenMVPluginIO::getStateDone,
                    &loop2, &QEventLoop::quit);

            while((!printEmpty) || (!frameBufferEmpty))
            {
                m_iodevice->getState();
                loop2.exec();
            }

            disconnect(conn2);
            disconnect(conn3);
        }

        m_iodevice->jpegEnable(m_jpgCompress->isChecked());
        m_iodevice->fbEnable(!m_disableFrameBuffer->isChecked());

        Core::MessageManager::grayOutOldContent();

        ///////////////////////////////////////////////////////////////////////

        m_iodevice->getTimeout(); // clear

        m_frameSizeDumpTimer.restart();
        m_getScriptRunningTimer.restart();
        m_getTxBufferTimer.restart();

        m_timer.restart();
        m_queue.clear();
        m_connected = true;
        m_running = false;
        m_portName = selectedPort;
        m_portPath = QString();
        m_portDriveSerialNumber = serialPortDriveSerialNumber(selectedPort);
        m_major = major2;
        m_minor = minor2;
        m_patch = patch2;
        m_errorFilterString = QString();

        m_openDriveFolderAction->setEnabled(false);
        m_configureSettingsAction->setEnabled(false);
        m_saveAction->setEnabled(false);
        m_resetAction->setEnabled(true);
        m_developmentReleaseAction->setEnabled(true);
        if(!m_autoReconnectAction->isChecked()) m_connectAction->setEnabled(false);
        m_connectAction->setVisible(false);
        if(!m_autoReconnectAction->isChecked()) m_disconnectAction->setEnabled(true);
        m_disconnectAction->setVisible(true);
        Core::IEditor *editor = Core::EditorManager::currentEditor();
        m_startAction->setEnabled((!m_viewerMode) && (editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false));
        m_startAction->setVisible((!m_viewerMode) && true);
        m_stopAction->setEnabled((!m_viewerMode) && false);
        m_stopAction->setVisible((!m_viewerMode) && false);

        m_boardLabel->setEnabled(true);
        m_boardLabel->setText(Tr::tr("Board: %L1").arg(boardTypeLabel));
        m_sensorLabel->setEnabled(true);
        m_sensorLabel->setText(Tr::tr("Sensor: %L1").arg(m_sensorType));
        m_versionButton->setEnabled(true);
        m_versionButton->setText(Tr::tr("Firmware Version: %L1.%L2.%L3").arg(major2).arg(minor2).arg(patch2));
        m_portLabel->setEnabled(true);
        m_portLabel->setText(Tr::tr("Serial Port: %L1").arg(m_portName));
        m_pathButton->setEnabled(true);
        m_pathButton->setText(Tr::tr("Drive:"));
        m_fpsButton->setEnabled(true);
        m_fpsButton->setText(Tr::tr("FPS: 0"));

        m_frameBuffer->enableSaveTemplate(false);
        m_frameBuffer->enableSaveDescriptor(false);

        if((major2 > OPENMV_ADD_TIME_INPUT_MAJOR)
        || ((major2 == OPENMV_ADD_TIME_INPUT_MAJOR) && (minor2 > OPENMV_ADD_TIME_INPUT_MINOR))
        || ((major2 == OPENMV_ADD_TIME_INPUT_MAJOR) && (minor2 == OPENMV_ADD_TIME_INPUT_MINOR) && (patch2 >= OPENMV_ADD_TIME_INPUT_PATCH)))
        {
            m_iodevice->timeInput();
        }

        // Fix Hello World ////////////////////////////////////////////////////

        TextEditor::BaseTextEditor *textEditor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::currentEditor());

        if(textEditor)
        {
            TextEditor::TextDocument *document = textEditor->textDocument();

            if(document && document->displayName() == QStringLiteral("helloworld_1.py") && (!document->isModified()))
            {
                QString filePath = Core::ICore::allUsersResourcePath(QStringLiteral("examples/00-HelloWorld/helloworld.py")).toString();

                QFile file(filePath);

                if(file.open(QIODevice::ReadOnly))
                {
                    QByteArray data = file.readAll();

                    if((file.error() == QFile::NoError) && (!data.isEmpty()))
                    {
                        data = fixScriptForSensor(data);

                        if (document->contents().simplified().trimmed() != data.simplified().trimmed())
                        {
                            QFile reload(document->filePath().toString());

                            if(reload.open(QIODevice::WriteOnly))
                            {
                                bool ok = reload.write(data) == data.size();
                                reload.close();

                                if (ok)
                                {
                                    QString error;
                                    document->reload(&error);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Check Version //////////////////////////////////////////////////////

        QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).
                match(m_firmwareSettings.object().value(QStringLiteral("firmware_version")).toString());

        if((major2 < match.captured(1).toInt())
        || ((major2 == match.captured(1).toInt()) && (minor2 < match.captured(2).toInt()))
        || ((major2 == match.captured(1).toInt()) && (minor2 == match.captured(2).toInt()) && (patch2 < match.captured(3).toInt())))
        {
            m_versionButton->setText(m_versionButton->text().append(Tr::tr(" - [ out of date - click here to updgrade ]")));

            QTimer::singleShot(1, this, [this] {
                if ((!m_autoConnect) && Utils::CheckableMessageBox::question(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Your OpenMV Cam's firmware is out of date. Would you like to upgrade?"),
                        Utils::CheckableDecider(DONT_SHOW_UPGRADE_FW_AGAIN),
                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel,
                        QMessageBox::Yes, QMessageBox::No, {}, {}, QMessageBox::Yes) == QMessageBox::Yes)
                {
                    OpenMVPlugin::updateCam(true);
                }
            });
        }
        else
        {
            m_versionButton->setText(m_versionButton->text().append(Tr::tr(" - [ latest ]")));
        }

        ///////////////////////////////////////////////////////////////////////

        // Try to kickoff scanning drives one last time before connecting.
        if(m_availableDrives.isEmpty())
        {
            QTimer::singleShot(1000, this, [this] {
                if(m_availableDrives.isEmpty()) QTimer::singleShot(0, m_scanDriveThread, &ScanDriveThread::scanDrivesSlot);
            });

            QTimer::singleShot(2000, this, [this] {
                if(m_availableDrives.isEmpty()) QTimer::singleShot(0, m_scanDriveThread, &ScanDriveThread::scanDrivesSlot);
            });

            QTimer::singleShot(3000, this, [this] {
                if(m_availableDrives.isEmpty()) QTimer::singleShot(0, m_scanDriveThread, &ScanDriveThread::scanDrivesSlot);
            });
        }

        m_working = false;

        OpenMVPlugin::setPortPath(true);

        if(m_autoRun)
        {
            startClicked();
        }
        else
        {
            QTimer::singleShot(0, this, &OpenMVPlugin::workingDone);
        }

        return;
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Connect"),
            Tr::tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::disconnectClicked(bool reset)
{
    if(m_connected)
    {
        if(!m_working)
        {
            m_working = true;

            // Stopping ///////////////////////////////////////////////////////
            {
                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                        &loop, &QEventLoop::quit);

                if(reset)
                {
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

                    m_iodevice->sysReset();
                }
                else
                {
                    if(m_stopOnConnectDiconnectionAction->isChecked()) m_iodevice->scriptStop();

                    if((!m_useGetState)
                    || (m_major < OPENMV_ADD_GET_STATE_MAJOR)
                    || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor < OPENMV_ADD_GET_STATE_MINOR))
                    || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor == OPENMV_ADD_GET_STATE_MINOR) && (m_patch < OPENMV_ADD_GET_STATE_PATCH)))
                    {
                        // Drain text buffer
                        {
                            bool empty = bool();
                            bool *emptyPtr = &empty;

                            QEventLoop loop2;

                            QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                                this, [emptyPtr] (bool ok) {
                                *emptyPtr = ok;
                            });

                            connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                                    &loop2, &QEventLoop::quit);

                            while(!empty)
                            {
                                m_iodevice->getTxBuffer();
                                loop2.exec();
                            }

                            disconnect(conn2);
                        }

                        // Drain image buffer
                        {
                            bool empty = bool();
                            bool *emptyPtr = &empty;

                            QEventLoop loop2;

                            QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                                this, [emptyPtr] (bool ok) {
                                *emptyPtr = ok;
                            });

                            connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                                    &loop2, &QEventLoop::quit);

                            while(!empty)
                            {
                                m_iodevice->frameSizeDump();
                                loop2.exec();
                            }

                            disconnect(conn2);
                        }
                    } else {
                        bool printEmpty = bool(), frameBufferEmpty = bool();
                        bool *printEmptyPtr = &printEmpty, *frameBufferEmptyPtr = &frameBufferEmpty;

                        QEventLoop loop2;

                        QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                            this, [printEmptyPtr] (bool ok) {
                            *printEmptyPtr = ok;
                        });

                        QMetaObject::Connection conn3 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                            this, [frameBufferEmptyPtr] (bool ok) {
                            *frameBufferEmptyPtr = ok;
                        });

                        connect(m_iodevice, &OpenMVPluginIO::getStateDone,
                                &loop2, &QEventLoop::quit);

                        while((!printEmpty) || (!frameBufferEmpty))
                        {
                            m_iodevice->getState();
                            loop2.exec();
                        }

                        disconnect(conn2);
                        disconnect(conn3);
                    }
                }

                m_iodevice->close();

                loop.exec();
            }

            ///////////////////////////////////////////////////////////////////

            m_iodevice->getTimeout(); // clear

            m_frameSizeDumpTimer.restart();
            m_getScriptRunningTimer.restart();
            m_getTxBufferTimer.restart();

            m_timer.restart();
            m_queue.clear();
            m_connected = false;
            m_running = false;
            m_major = int();
            m_minor = int();
            m_patch = int();
            // LEAVE CACHED m_boardTypeFolder = QString();
            // LEAVE CACHED m_fullBoardType = QString();
            // LEAVE CACHED m_boardType = QString();
            // LEAVE CACHED m_boardId = QString();
            // LEAVE CACHED m_boardVID = 0;
            // LEAVE CACHED m_boardPID = 0;
            m_sensorType = QString();
            m_portName = QString();
            m_portPath = QString();
            m_portDriveSerialNumber = QString();
            m_errorFilterString = QString();

            m_openDriveFolderAction->setEnabled(false);
            m_configureSettingsAction->setEnabled(false);
            m_saveAction->setEnabled(false);
            m_resetAction->setEnabled(false);
            m_developmentReleaseAction->setEnabled(false);
            m_connectAction->setVisible(true);
            if(!m_autoReconnectAction->isChecked()) m_connectAction->setEnabled(true);
            m_disconnectAction->setVisible(false);
            if(!m_autoReconnectAction->isChecked()) m_disconnectAction->setEnabled(false);
            m_startAction->setEnabled((!m_viewerMode) && false);
            m_startAction->setVisible((!m_viewerMode) && true);
            m_stopAction->setEnabled((!m_viewerMode) && false);
            m_stopAction->setVisible((!m_viewerMode) && false);

            m_registerButton->setText(QString());
            m_registerButton->setVisible(false);
            m_registerButtonSpacer->setVisible(false);
            m_boardLabel->setDisabled(true);
            m_boardLabel->setText(Tr::tr("Board:"));
            m_sensorLabel->setDisabled(true);
            m_sensorLabel->setText(Tr::tr("Sensor:"));
            m_versionButton->setDisabled(true);
            m_versionButton->setText(Tr::tr("Firmware Version:"));
            m_portLabel->setDisabled(true);
            m_portLabel->setText(Tr::tr("Serial Port:"));
            m_pathButton->setDisabled(true);
            m_pathButton->setText(Tr::tr("Drive:"));
            m_fpsButton->setDisabled(true);
            m_fpsButton->setText(Tr::tr("FPS:"));

            m_frameBuffer->enableSaveTemplate(false);
            m_frameBuffer->enableSaveDescriptor(false);

            Python::Internal::PyLSClient::setPortPath(Utils::FilePath::fromUserInput(m_portPath));

            ///////////////////////////////////////////////////////////////////

            m_working = false;
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                reset ? Tr::tr("Reset") : Tr::tr("Disconnect"),
                Tr::tr("Busy... please wait..."));
        }
    }

    QTimer::singleShot(0, this, &OpenMVPlugin::disconnectDone);
}

void OpenMVPlugin::startClicked()
{
    if(!m_working)
    {
        m_working = true;

        // Stopping ///////////////////////////////////////////////////////////
        {
            bool running2 = bool();
            bool *running2Ptr = &running2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::scriptRunning,
                this, [running2Ptr] (bool running) {
                *running2Ptr = running;
            });

            if(!m_iodevice->queueisEmpty())
            {
                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::queueEmpty,
                        &loop, &QEventLoop::quit);

                loop.exec(); // drain previous commands
            }

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::scriptRunning,
                    &loop, &QEventLoop::quit);

            if((!m_useGetState)
            || (m_major < OPENMV_ADD_GET_STATE_MAJOR)
            || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor < OPENMV_ADD_GET_STATE_MINOR))
            || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor == OPENMV_ADD_GET_STATE_MINOR) && (m_patch < OPENMV_ADD_GET_STATE_PATCH)))
            {
                m_iodevice->getScriptRunning();
            }
            else
            {
                m_iodevice->getState();
            }

            loop.exec(); // sync with camera

            disconnect(conn);

            if(running2)
            {
                m_iodevice->scriptStop();

                if((!m_useGetState)
                || (m_major < OPENMV_ADD_GET_STATE_MAJOR)
                || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor < OPENMV_ADD_GET_STATE_MINOR))
                || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor == OPENMV_ADD_GET_STATE_MINOR) && (m_patch < OPENMV_ADD_GET_STATE_PATCH)))
                {
                    // Drain text buffer
                    {
                        bool empty = bool();
                        bool *emptyPtr = &empty;

                        QEventLoop loop2;

                        QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                            this, [emptyPtr] (bool ok) {
                            *emptyPtr = ok;
                        });

                        connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                                &loop2, &QEventLoop::quit);

                        while(!empty)
                        {
                            m_iodevice->getTxBuffer();
                            loop2.exec();
                        }

                        disconnect(conn2);
                    }

                    // Drain image buffer
                    {
                        bool empty = bool();
                        bool *emptyPtr = &empty;

                        QEventLoop loop2;

                        QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                            this, [emptyPtr] (bool ok) {
                            *emptyPtr = ok;
                        });

                        connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                                &loop2, &QEventLoop::quit);

                        while(!empty)
                        {
                            m_iodevice->frameSizeDump();
                            loop2.exec();
                        }

                        disconnect(conn2);
                    }
                } else {
                    bool printEmpty = bool(), frameBufferEmpty = bool();
                    bool *printEmptyPtr = &printEmpty, *frameBufferEmptyPtr = &frameBufferEmpty;

                    QEventLoop loop2;

                    QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                        this, [printEmptyPtr] (bool ok) {
                        *printEmptyPtr = ok;
                    });

                    QMetaObject::Connection conn3 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                        this, [frameBufferEmptyPtr] (bool ok) {
                        *frameBufferEmptyPtr = ok;
                    });

                    connect(m_iodevice, &OpenMVPluginIO::getStateDone,
                            &loop2, &QEventLoop::quit);

                    while((!printEmpty) || (!frameBufferEmpty))
                    {
                        m_iodevice->getState();
                        loop2.exec();
                    }

                    disconnect(conn2);
                    disconnect(conn3);
                }
            }
        }

        ///////////////////////////////////////////////////////////////////////

        QByteArray contents = Core::EditorManager::currentEditor() ? Core::EditorManager::currentEditor()->document() ? Core::EditorManager::currentEditor()->document()->contents() : QByteArray() : QByteArray();

        if(importHelper(contents))
        {
            m_iodevice->scriptExec(contents);

            m_timer.restart();
            m_queue.clear();
        }
        else
        {
            m_fpsButton->setText(Tr::tr("FPS: 0"));
        }

        ///////////////////////////////////////////////////////////////////////

        m_working = false;

        QTimer::singleShot(0, this, &OpenMVPlugin::workingDone);
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Start"),
            Tr::tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::stopClicked()
{
    if(!m_working)
    {
        m_working = true;

        // Stopping ///////////////////////////////////////////////////////////
        {
            bool running2 = bool();
            bool *running2Ptr = &running2;

            QMetaObject::Connection conn = connect(m_iodevice, &OpenMVPluginIO::scriptRunning,
                this, [running2Ptr] (bool running) {
                *running2Ptr = running;
            });

            if(!m_iodevice->queueisEmpty())
            {
                QEventLoop loop;

                connect(m_iodevice, &OpenMVPluginIO::queueEmpty,
                        &loop, &QEventLoop::quit);

                loop.exec(); // drain previous commands
            }

            QEventLoop loop;

            connect(m_iodevice, &OpenMVPluginIO::scriptRunning,
                    &loop, &QEventLoop::quit);

            if((!m_useGetState)
            || (m_major < OPENMV_ADD_GET_STATE_MAJOR)
            || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor < OPENMV_ADD_GET_STATE_MINOR))
            || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor == OPENMV_ADD_GET_STATE_MINOR) && (m_patch < OPENMV_ADD_GET_STATE_PATCH)))
            {
                m_iodevice->getScriptRunning();
            }
            else
            {
                m_iodevice->getState();
            }

            loop.exec(); // sync with camera

            disconnect(conn);

            if(running2)
            {
                m_iodevice->scriptStop();

                if((!m_useGetState)
                || (m_major < OPENMV_ADD_GET_STATE_MAJOR)
                || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor < OPENMV_ADD_GET_STATE_MINOR))
                || ((m_major == OPENMV_ADD_GET_STATE_MAJOR) && (m_minor == OPENMV_ADD_GET_STATE_MINOR) && (m_patch < OPENMV_ADD_GET_STATE_PATCH)))
                {
                    // Drain text buffer
                    {
                        bool empty = bool();
                        bool *emptyPtr = &empty;

                        QEventLoop loop2;

                        QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                            this, [emptyPtr] (bool ok) {
                            *emptyPtr = ok;
                        });

                        connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                                &loop2, &QEventLoop::quit);

                        while(!empty)
                        {
                            m_iodevice->getTxBuffer();
                            loop2.exec();
                        }

                        disconnect(conn2);
                    }

                    // Drain image buffer
                    {
                        bool empty = bool();
                        bool *emptyPtr = &empty;

                        QEventLoop loop2;

                        QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                            this, [emptyPtr] (bool ok) {
                            *emptyPtr = ok;
                        });

                        connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                                &loop2, &QEventLoop::quit);

                        while(!empty)
                        {
                            m_iodevice->frameSizeDump();
                            loop2.exec();
                        }

                        disconnect(conn2);
                    }
                } else {
                    bool printEmpty = bool(), frameBufferEmpty = bool();
                    bool *printEmptyPtr = &printEmpty, *frameBufferEmptyPtr = &frameBufferEmpty;

                    QEventLoop loop2;

                    QMetaObject::Connection conn2 = connect(m_iodevice, &OpenMVPluginIO::printEmpty,
                        this, [printEmptyPtr] (bool ok) {
                        *printEmptyPtr = ok;
                    });

                    QMetaObject::Connection conn3 = connect(m_iodevice, &OpenMVPluginIO::frameBufferEmpty,
                        this, [frameBufferEmptyPtr] (bool ok) {
                        *frameBufferEmptyPtr = ok;
                    });

                    connect(m_iodevice, &OpenMVPluginIO::getStateDone,
                            &loop2, &QEventLoop::quit);

                    while((!printEmpty) || (!frameBufferEmpty))
                    {
                        m_iodevice->getState();
                        loop2.exec();
                    }

                    disconnect(conn2);
                    disconnect(conn3);
                }
            }
        }

        ///////////////////////////////////////////////////////////////////////

        m_fpsButton->setText(Tr::tr("FPS: 0"));

        ///////////////////////////////////////////////////////////////////////

        m_working = false;

        ///////////////////////////////////////////////////////////////////////

        if(Core::EditorManager::currentEditor()
            ? Core::EditorManager::currentEditor()->document()
                ? Core::EditorManager::currentEditor()->document()->displayName() == QStringLiteral("helloworld_1.py")
                : false
            : false)
        {
            QTimer::singleShot(2000, this, &OpenMVPlugin::showExamplesDialog);
        }

        ///////////////////////////////////////////////////////////////////////

        QTimer::singleShot(0, this, &OpenMVPlugin::workingDone);
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Stop"),
            Tr::tr("Busy... please wait..."));
    }
}

void OpenMVPlugin::showExamplesDialog()
{
    if (!QApplication::activeModalWidget()) {
        Utils::CheckableMessageBox::information(Core::ICore::dialogParent(),
                Tr::tr("More Examples"),
                Tr::tr("You can find more examples under the File -> Examples menu.\n\n"
                   "In particular, checkout the Color-Tracking examples."),
                Utils::CheckableDecider(DONT_SHOW_EXAMPLES_AGAIN),
                QMessageBox::Ok,
                QMessageBox::Ok);
    } else {
        QTimer::singleShot(2000, this, &OpenMVPlugin::showExamplesDialog); // try again
    }
}

void OpenMVPlugin::updateCam(bool forceYes)
{
    if(!m_working)
    {
        QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).
            match(m_firmwareSettings.object().value(QStringLiteral("firmware_version")).toString());

        if((m_major < match.captured(1).toInt())
        || ((m_major == match.captured(1).toInt()) && (m_minor < match.captured(2).toInt()))
        || ((m_major == match.captured(1).toInt()) && (m_minor == match.captured(2).toInt()) && (m_patch < match.captured(3).toInt())))
        {
            Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
            settings->beginGroup(SETTINGS_GROUP);

            QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            dialog->setWindowTitle(Tr::tr("Firmware Update"));
            QFormLayout *layout = new QFormLayout(dialog);

            layout->addWidget(new QLabel(forceYes ? Tr::tr("Upgrade options:") : Tr::tr("Update your OpenMV Cam's firmware to the latest version?")));

            QHBoxLayout *layout2 = new QHBoxLayout;
            layout2->setContentsMargins(6, 0, 0, 0);
            QWidget *widget = new QWidget;
            widget->setLayout(layout2);

            QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal FAT file system"));
            checkBox->setChecked(settings->value(LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
            layout2->addWidget(checkBox);
            checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal FAT file system will be deleted. "
                                        "This does not erase files on any removable SD card (if inserted)."));

            QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Reset ROMFS file system"));
            checkBox2->setChecked(settings->value(LAST_DFU_RESET_ROM_FS_STATE, false).toBool());
            layout2->addWidget(checkBox2);
            checkBox2->setToolTip(Tr::tr("If you enable this option the ROM file system on your OpenMV Cam will be reset back to default."));

            layout->addRow(widget);

            if((m_major < OPENMV_FORCE_ROMFS_UPGRADE_MAJOR)
            || ((m_major == OPENMV_FORCE_ROMFS_UPGRADE_MAJOR) && (m_minor < OPENMV_FORCE_ROMFS_UPGRADE_MINOR))
            || ((m_major == OPENMV_FORCE_ROMFS_UPGRADE_MAJOR) && (m_minor == OPENMV_FORCE_ROMFS_UPGRADE_MINOR) && (m_patch < OPENMV_FORCE_ROMFS_UPGRADE_PATCH)))
            {
                checkBox->setChecked(true);
                checkBox->setEnabled(false);
                checkBox2->setChecked(true);
                checkBox2->setEnabled(false);

                layout->addWidget(new QLabel(Tr::tr("Warning: Upgrading to the new firmware version requires the FAT file system to be erased.")));
            }

            QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            layout2->addSpacing(80);
            layout2->addWidget(box);
            layout->addRow(widget);

            connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
            connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

            bool ok = dialog->exec() == QDialog::Accepted;

            if(ok)
            {
                if (checkBox->isEnabled()) settings->setValue(LAST_DFU_FLASH_FS_ERASE_STATE, checkBox->isChecked());
                if (checkBox2->isEnabled()) settings->setValue(LAST_DFU_RESET_ROM_FS_STATE, checkBox2->isChecked());
            }

            settings->endGroup();

            if(ok)
            {
                disconnectClicked();

                if(ExtensionSystem::PluginManager::specForPlugin(this)->state() != ExtensionSystem::PluginSpec::Stopped)
                {
                    connectClicked(true, QString(), checkBox->isChecked(), false, false, false, QString(),
                                   checkBox2->isChecked() ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE);
                }
            }

            delete dialog;
        }
        else
        {
            QMessageBox::information(Core::ICore::dialogParent(),
                Tr::tr("Firmware Update"),
                Tr::tr("Your OpenMV Cam's firmware is up to date."));

            Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
            settings->beginGroup(SETTINGS_GROUP);

            QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
            dialog->setWindowTitle(Tr::tr("Firmware Update"));
            QFormLayout *layout = new QFormLayout(dialog);

            layout->addWidget(new QLabel(Tr::tr("Need to reset your OpenMV Cam's firmware to the release version?")));

            QHBoxLayout *layout2 = new QHBoxLayout;
            layout2->setContentsMargins(6, 0, 0, 0);
            QWidget *widget = new QWidget;
            widget->setLayout(layout2);

            QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal FAT file system"));
            checkBox->setChecked(settings->value(LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
            layout2->addWidget(checkBox);
            checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal FAT file system will be deleted. "
                                        "This does not erase files on any removable SD card (if inserted)."));

            QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Reset ROMFS file system"));
            checkBox2->setChecked(settings->value(LAST_DFU_RESET_ROM_FS_STATE, false).toBool());
            layout2->addWidget(checkBox2);
            checkBox2->setToolTip(Tr::tr("If you enable this option the ROM file system on your OpenMV Cam will be reset back to default."));

            layout->addRow(widget);

            QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            layout2->addSpacing(80);
            layout2->addWidget(box);
            layout->addRow(widget);

            connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
            connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

            bool ok = dialog->exec() == QDialog::Accepted;

            if(ok)
            {
                settings->setValue(LAST_DFU_FLASH_FS_ERASE_STATE, checkBox->isChecked());
                settings->setValue(LAST_DFU_RESET_ROM_FS_STATE, checkBox2->isChecked());
            }

            settings->endGroup();

            if(ok)
            {
                disconnectClicked();

                if(ExtensionSystem::PluginManager::specForPlugin(this)->state() != ExtensionSystem::PluginSpec::Stopped)
                {
                    connectClicked(true, QString(), checkBox->isChecked(), false, false, false, QString(),
                                   checkBox2->isChecked() ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE);
                }
            }

            delete dialog;
        }
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Firmware Update"),
            Tr::tr("Busy... please wait..."));
    }
}

QJsonObject OpenMVPlugin::getBoardSettings(const QString &title, Utils::QtcSettings *settings, bool autoConnectToBoard)
{
    if (m_boardPresent && (!m_working) && autoConnectToBoard)
    {
        QEventLoop loop;
        connect(this, &OpenMVPlugin::workingDone, &loop, &QEventLoop::quit);
        QTimer::singleShot(0, this, [this] { connectClicked(); });
        loop.exec();
    }

    if (m_connected)
    {
        QSerialPortInfo raw_tempPort = QSerialPortInfo(m_portName);
        MyQSerialPortInfo tempPort(raw_tempPort);

        QString temp = QString(m_fullBoardType).simplified();

        for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            // Don't ignore "hidden" here.
            if ((value.toObject().value(QStringLiteral("boardArchString")).toString().toLower() == temp.toLower())
            && matchVidPid(value.toObject(), QString(), tempPort))
            {
                return value.toObject();
            }
        }

        QMessageBox::critical(Core::ICore::dialogParent(),
            title,
            Tr::tr("No board settings for the connected board found!"));

        return QJsonObject();
    }
    else
    {
        QMap<QString, QJsonObject> mappingsHumanReadable;

        for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
        {
            if (!value.toObject().value(QStringLiteral("hidden")).toBool())
            {
                mappingsHumanReadable.insert(value.toObject().value(QStringLiteral("boardDisplayName")).toString(), value.toObject());
            }
        }

        int index = mappingsHumanReadable.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE_GET).toString());

        bool ok = mappingsHumanReadable.size() == 1;
        QString temp = (mappingsHumanReadable.size() == 1) ? mappingsHumanReadable.keys().first() : QInputDialog::getItem(Core::ICore::dialogParent(),
            title, Tr::tr("Please select the board type"),
            mappingsHumanReadable.keys(), (index != -1) ? index : 0, false, &ok,
            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

        if(ok)
        {
            settings->setValue(LAST_BOARD_TYPE_STATE_GET, temp);
            return mappingsHumanReadable.value(temp); // Get mappings key.
        }

        return QJsonObject();
    }
}

} // namespace Internal
} // namespace OpenMV
