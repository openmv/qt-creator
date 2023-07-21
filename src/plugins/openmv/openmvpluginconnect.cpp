#include "openmvplugin.h"

#include "tools/myqserialportinfo.h"

#include "openmvtr.h"

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

                QSettings *settings = ExtensionSystem::PluginManager::settings();
                settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

                int old_major = settings->value(QStringLiteral(RESOURCES_MAJOR)).toInt();
                int old_minor = settings->value(QStringLiteral(RESOURCES_MINOR)).toInt();
                int old_patch = settings->value(QStringLiteral(RESOURCES_PATCH)).toInt();

                settings->endGroup();

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

                        connect(manager2, &QNetworkAccessManager::finished, this, [manager2, new_major, new_minor, new_patch, dialog] (QNetworkReply *reply2) {
                            QByteArray data2 = reply2->error() == QNetworkReply::NoError ? reply2->readAll() : QByteArray();

                            if((reply2->error() == QNetworkReply::NoError) && (!data2.isEmpty()))
                            {
                                dialog->setLabelText(Tr::tr("Installing..."));
                                dialog->setRange(0, 0);
                                dialog->setCancelButton(Q_NULLPTR);

                                QSettings *settings2 = ExtensionSystem::PluginManager::settings();
                                settings2->beginGroup(QStringLiteral(SETTINGS_GROUP));

                                settings2->setValue(QStringLiteral(RESOURCES_MAJOR), 0);
                                settings2->setValue(QStringLiteral(RESOURCES_MINOR), 0);
                                settings2->setValue(QStringLiteral(RESOURCES_PATCH), 0);
                                settings2->sync();

                                bool ok = true;

                                QString error;

                                if(!removeRecursivelyWrapper(Core::ICore::userResourcePath(), &error))
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        QString(),
                                        error + Tr::tr("\n\nPlease close any programs that are viewing/editing OpenMV IDE's application data and then restart OpenMV IDE!"));

                                    QApplication::quit();
                                    ok = false;
                                }
                                else
                                {
                                    if(!extractAllWrapper(&data2, Core::ICore::userResourcePath().toString()))
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
                                    settings2->setValue(QStringLiteral(RESOURCES_MAJOR), new_major);
                                    settings2->setValue(QStringLiteral(RESOURCES_MINOR), new_minor);
                                    settings2->setValue(QStringLiteral(RESOURCES_PATCH), new_patch);
                                    settings2->sync();

                                    QMessageBox::information(Core::ICore::dialogParent(),
                                        QString(),
                                        Tr::tr("Installation Sucessful! Please restart OpenMV IDE."));

                                    Core::ICore::restart();
                                }

                                settings2->endGroup();
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

    QNetworkRequest request = QNetworkRequest(QUrl(QStringLiteral("https://raw.githubusercontent.com/openmv/openmv-ide-version/main/openmv-ide-resources-version.txt")));
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
    dialog->setWindowTitle(Tr::tr("Bootloader"));
    QFormLayout *layout = new QFormLayout(dialog);
    layout->setVerticalSpacing(0);

    QSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

    Utils::PathChooser *pathChooser = new Utils::PathChooser();
    pathChooser->setExpectedKind(Utils::PathChooser::File);
    pathChooser->setPromptDialogTitle(Tr::tr("Firmware Path"));
    pathChooser->setPromptDialogFilter(Tr::tr("Firmware Binary (*.bin *.dfu)"));
    pathChooser->setFilePath(Utils::FilePath::fromVariant(settings->value(QStringLiteral(LAST_FIRMWARE_PATH), QDir::homePath())));
    layout->addRow(Tr::tr("Firmware Path"), pathChooser);
    layout->addItem(new QSpacerItem(0, 6));

    QHBoxLayout *layout2 = new QHBoxLayout;
    layout2->setContentsMargins(0, 0, 0, 0);
    QWidget *widget = new QWidget;
    widget->setLayout(layout2);

    QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal file system"));
    checkBox->setChecked(settings->value(QStringLiteral(LAST_FLASH_FS_ERASE_STATE), false).toBool());
    layout2->addWidget(checkBox);
    checkBox->setVisible(!pathChooser->filePath().toString().endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive));
    checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal flash drive will be deleted. "
                            "This does not erase files on any removable SD card (if inserted)."));
    QCheckBox *checkBox2 = new QCheckBox(Tr::tr("Erase internal file system"));
    checkBox2->setChecked(true);
    checkBox2->setEnabled(false);
    checkBox2->setToolTip(Tr::tr("Loading firmware via DFU always erases your OpenMV Cam's internal flash drive. "
                             "This does not erase files on any removable SD card (if inserted)."));
    layout2->addWidget(checkBox2);
    checkBox2->setVisible(pathChooser->filePath().toString().endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive));

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *run = new QPushButton(Tr::tr("Run"));
    run->setEnabled(pathChooser->isValid());
    box->addButton(run, QDialogButtonBox::AcceptRole);
    layout2->addSpacing(160);
    layout2->addWidget(box);
    layout->addRow(widget);

    connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    connect(pathChooser, &Utils::PathChooser::validChanged, run, &QPushButton::setEnabled);
    connect(pathChooser, &Utils::PathChooser::rawPathChanged, this, [this, dialog, pathChooser, checkBox, checkBox2] () {

        if(pathChooser->filePath().toString().endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
        {
            checkBox->setVisible(false);
            checkBox2->setVisible(true);
        }
        else
        {
            checkBox->setVisible(true);
            checkBox2->setVisible(false);
        }

        QTimer::singleShot(0, this, [dialog] { dialog->adjustSize(); });
    });

    if(dialog->exec() == QDialog::Accepted)
    {
        QString forceFirmwarePath = pathChooser->filePath().toString();
        bool flashFSErase = checkBox->isChecked();

        if(QFileInfo(forceFirmwarePath).exists() && QFileInfo(forceFirmwarePath).isFile())
        {
            settings->setValue(QStringLiteral(LAST_FIRMWARE_PATH), forceFirmwarePath);
            settings->setValue(QStringLiteral(LAST_FLASH_FS_ERASE_STATE), flashFSErase);
            settings->endGroup();
            delete dialog;

            connectClicked(true, forceFirmwarePath, (flashFSErase || forceFirmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive)));
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

            QSettings *settings = ExtensionSystem::PluginManager::settings();
            settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

            QHBoxLayout *layout2 = new QHBoxLayout;
            layout2->setContentsMargins(0, 0, 0, 0);
            QWidget *widget = new QWidget;
            widget->setLayout(layout2);

            QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal file system"));
            checkBox->setChecked(settings->value(QStringLiteral(LAST_FLASH_FS_ERASE_STATE), false).toBool());
            layout2->addWidget(checkBox);
            checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal flash drive will be deleted. "
                                    "This does not erase files on any removable SD card (if inserted)."));

            QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
            QPushButton *run = new QPushButton(Tr::tr("Run"));
            box->addButton(run, QDialogButtonBox::AcceptRole);
            layout2->addSpacing(160);
            layout2->addWidget(box);
            layout->addRow(widget);

            connect(box, &QDialogButtonBox::accepted, newDialog, &QDialog::accept);
            connect(box, &QDialogButtonBox::rejected, newDialog, &QDialog::reject);

            if(newDialog->exec() == QDialog::Accepted)
            {
                bool flashFSErase = checkBox->isChecked();

                settings->setValue(QStringLiteral(LAST_FLASH_FS_ERASE_STATE), flashFSErase);
                settings->endGroup();
                delete newDialog;

                connectClicked(true, QString(), flashFSErase, false, true);
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

#define CONNECT_END() \
do { \
    m_working = false; \
    QTimer::singleShot(0, this, &OpenMVPlugin::workingDone); \
    return; \
} while(0)

#define RECONNECT_END() \
do { \
    m_working = false; \
    QTimer::singleShot(0, this, [this] {connectClicked();}); \
    return; \
} while(0)

#define RECONNECT_WAIT_END() \
do { \
    m_working = false; \
    QTimer::singleShot(0, this, [this] {connectClicked(false, QString(), false, false, false, true);}); \
    return; \
} while(0)

#define RECONNECT_AND_FORCEBOOTLOADER_END() \
do { \
    m_working = false; \
    QTimer::singleShot(0, this, [this] {connectClicked(true);}); \
    return; \
} while(0)

#define CLOSE_CONNECT_END() \
do { \
    QEventLoop m_loop; \
    connect(m_iodevice, &OpenMVPluginIO::closeResponse, &m_loop, &QEventLoop::quit); \
    m_iodevice->close(); \
    m_loop.exec(); \
    m_working = false; \
    QTimer::singleShot(0, this, &OpenMVPlugin::workingDone); \
    return; \
} while(0)

#define CLOSE_RECONNECT_END() \
do { \
    QEventLoop m_loop; \
    connect(m_iodevice, &OpenMVPluginIO::closeResponse, &m_loop, &QEventLoop::quit); \
    m_iodevice->close(); \
    m_loop.exec(); \
    m_working = false; \
    QTimer::singleShot(0, this, [this] {connectClicked();}); \
    return; \
} while(0)

#define CLOSE_RECONNECT_WAIT_END() \
do { \
    QEventLoop m_loop; \
    connect(m_iodevice, &OpenMVPluginIO::closeResponse, &m_loop, &QEventLoop::quit); \
    m_iodevice->close(); \
    m_loop.exec(); \
    m_working = false; \
    QTimer::singleShot(0, this, [this] {connectClicked(false, QString(), false, false, false, true);}); \
    return; \
} while(0)

#define CLOSE_RECONNECT_AND_FORCEBOOTLOADER_END() \
do { \
    QEventLoop m_loop; \
    connect(m_iodevice, &OpenMVPluginIO::closeResponse, &m_loop, &QEventLoop::quit); \
    m_iodevice->close(); \
    m_loop.exec(); \
    m_working = false; \
    QTimer::singleShot(0, this, [this] {connectClicked(true);}); \
    return; \
} while(0)

bool OpenMVPlugin::getTheLatestDevelopmentFirmware(const QString &arch, QString *path)
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

    connect(manager2, &QNetworkAccessManager::finished, this, [manager2, dialog, okPtr, path] (QNetworkReply *reply2) {
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
                *path = QDir::cleanPath(QDir::fromNativeSeparators(QDir::tempPath() + QStringLiteral("/firmware.bin")));
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

void OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePath, bool forceFlashFSErase, bool justEraseFlashFs, bool installTheLatestDevelopmentFirmware, bool waitForCamera)
{
    if(!m_working)
    {
        if(m_connect_disconnect)
        {
            disconnect(m_connect_disconnect);
        }

        if(m_connected)
        {
            m_connect_disconnect = connect(this, &OpenMVPlugin::disconnectDone, this, [this, forceBootloader, forceFirmwarePath, forceFlashFSErase, justEraseFlashFs, installTheLatestDevelopmentFirmware, waitForCamera] {
                QTimer::singleShot(0, this, [this, forceBootloader, forceFirmwarePath, forceFlashFSErase, justEraseFlashFs, installTheLatestDevelopmentFirmware, waitForCamera] {connectClicked(forceBootloader, forceFirmwarePath, forceFlashFSErase, justEraseFlashFs, installTheLatestDevelopmentFirmware, waitForCamera);});
            });

            QTimer::singleShot(0, this, [this] {disconnectClicked();});

            return;
        }

        m_working = true;

        QStringList stringList;
        QElapsedTimer waitForCameraTimeout;
        waitForCameraTimeout.start();
        QStringList dfuDevices;

        do
        {
            foreach(QSerialPortInfo raw_port, QSerialPortInfo::availablePorts())
            {
                MyQSerialPortInfo port(raw_port);

                if(port.hasVendorIdentifier() && port.hasProductIdentifier()
                && (m_serialNumberFilter.isEmpty() || (m_serialNumberFilter == port.serialNumber().toUpper()))
                && (((port.vendorIdentifier() == OPENMVCAM_VID) && (port.productIdentifier() == OPENMVCAM_PID) && (port.serialNumber() != QStringLiteral("000000000010")) && (port.serialNumber() != QStringLiteral("000000000011")))
                || ((port.vendorIdentifier() == ARDUINOCAM_VID) && (((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||
                                                                    ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||
                                                                    ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||
                                                                    ((port.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID)))
                || ((port.vendorIdentifier() == RPI2040_VID) && (port.productIdentifier() == RPI2040_PID))))
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
                foreach(wifiPort_t port, m_availableWifiPorts)
                {
                    stringList.append(QString(QStringLiteral("%1:%2")).arg(port.name).arg(port.addressAndPort));
                }
            }

            dfuDevices = picotoolGetDevices() + imxGetAllDevices();

            foreach(const QString &device, getDevices())
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

                    if(info.hasVendorIdentifier()
                    && (info.vendorIdentifier() == ARDUINOCAM_VID)
                    && info.hasProductIdentifier()
                    && ((info.productIdentifier() == NRF_OLD_PID) || (info.productIdentifier() == NRF_LDR_PID) || (info.productIdentifier() == RPI_OLD_PID) || (info.productIdentifier() == RPI_LDR_PID)))
                    {
                        dfuDevices.append(QString(QStringLiteral("%1:%2").arg(info.vendorIdentifier(), 4, 16, QLatin1Char('0')).arg(info.productIdentifier(), 4, 16, QLatin1Char('0'))));
                        it = stringList.erase(it);
                    }
                    else
                    {
                        it++;
                    }
                }
            }

            if(waitForCamera && stringList.isEmpty()) QApplication::processEvents();
        }
        while(waitForCamera && stringList.isEmpty() && (!waitForCameraTimeout.hasExpired(10000)));

        QSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

        QString selectedPort;
        bool forceBootloaderBricked = false;
        QString originalFirmwareFolder = QString();
        QString firmwarePath = forceFirmwarePath;
        int originalEraseFlashSectorStart = FLASH_SECTOR_START;
        int originalEraseFlashSectorEnd = FLASH_SECTOR_END;
        int originalEraseFlashSectorAllStart = FLASH_SECTOR_ALL_START;
        int originalEraseFlashSectorAllEnd = FLASH_SECTOR_ALL_END;
        QString originalDfuVidPid = QString();
        bool dfuNoDialogs = false;
        bool repairingBootloader = false;

        if(stringList.isEmpty())
        {
            if(forceBootloader && (!dfuDevices.isEmpty())
            && (m_autoUpdateOnConnect != QStringLiteral("release"))
            && (m_autoUpdateOnConnect != QStringLiteral("developement")))
            {
                forceBootloaderBricked = true;
            }
            else
            {
                bool dfuDeviceResetToRelease = false;
                bool dfuDeviceEraseFlash = false;

                if(!dfuDevices.isEmpty())
                {
                    QFile file(Core::ICore::userResourcePath(QStringLiteral("firmware/firmware.txt")).toString());

                    if(file.open(QIODevice::ReadOnly))
                    {
                        QByteArray data = file.readAll();

                        if((file.error() == QFile::NoError) && (!data.isEmpty()))
                        {
                            file.close();

                            QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).match(QString::fromUtf8(data));

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
                            combo->addItem(Tr::tr("Just erase the interal file system"));
                            combo->setCurrentIndex(settings->value(QStringLiteral(LAST_DFU_ACTION), 0).toInt());
                            layout->addWidget(combo);
                            layout->addItem(new QSpacerItem(0, 6));

                            QHBoxLayout *layout2 = new QHBoxLayout;
                            layout2->setContentsMargins(0, 0, 0, 0);
                            QWidget *widget = new QWidget;
                            widget->setLayout(layout2);

                            QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal file system"));
                            checkBox->setChecked(settings->value(QStringLiteral(LAST_DFU_FLASH_FS_ERASE_STATE), false).toBool());
                            layout2->addWidget(checkBox);
                            checkBox->setVisible(combo->currentIndex() == 0);
                            checkBox->setToolTip(Tr::tr("If you enable this option all files on your OpenMV Cam's internal flash drive will be deleted. "
                                                    "This does not erase files on any removable SD card (if inserted)."));

                            QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
                            layout2->addSpacing(160);
                            layout2->addWidget(box);
                            layout->addRow(widget);

                            connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
                            connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
                            connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, dialog, checkBox] (int index) {
                                checkBox->setVisible(index == 0);
                                QTimer::singleShot(0, this, [dialog] { dialog->adjustSize(); });
                            });

                            // If a board is in DFU mode we install the release firmware.
                            if((m_autoUpdateOnConnect == QStringLiteral("release"))
                            || (m_autoUpdateOnConnect == QStringLiteral("developement")))
                            {
                                combo->setCurrentIndex(0);
                                checkBox->setChecked(m_autoEraseOnConnect);
                            }

                            if((m_autoUpdateOnConnect == QStringLiteral("release"))
                            || (m_autoUpdateOnConnect == QStringLiteral("developement"))
                            || (dialog->exec() == QDialog::Accepted))
                            {
                                settings->setValue(QStringLiteral(LAST_DFU_ACTION), combo->currentIndex());
                                settings->setValue(QStringLiteral(LAST_DFU_FLASH_FS_ERASE_STATE), checkBox->isChecked());

                                if(combo->currentIndex() == 0)
                                {
                                    dfuDeviceResetToRelease = true;
                                    dfuDeviceEraseFlash = checkBox->isChecked();
                                    dfuNoDialogs = true;
                                }
                                else if(combo->currentIndex() == 1)
                                {
                                    QTimer::singleShot(0, m_bootloaderAction, &QAction::trigger);
                                }
                                else if(combo->currentIndex() == 2)
                                {
                                    QTimer::singleShot(0, m_eraseAction, &QAction::trigger);
                                }
                            }

                            delete dialog;
                        }
                    }

                    if(!dfuDeviceResetToRelease)
                    {
                        settings->endGroup();
                        CONNECT_END();
                    }
                }

                if((!forceBootloader) && (!dfuDeviceResetToRelease)) QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Connect"),
                    Tr::tr("No OpenMV Cams found!"));

                QFile boards(Core::ICore::userResourcePath(QStringLiteral("firmware/boards.txt")).toString());

                if(boards.open(QIODevice::ReadOnly))
                {
                    QMap<QString, QString> mappings;
                    QMap<QString, QPair<int, int> > eraseMappings;
                    QMap<QString, QPair<int, int> > eraseAllMappings;
                    QMap<QString, QString> vidpidMappings;

                    forever
                    {
                        QByteArray data = boards.readLine();

                        if((boards.error() == QFile::NoError) && (!data.isEmpty()))
                        {
                            QRegularExpressionMatch mapping = QRegularExpression(QStringLiteral("(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\S+)")).match(QString::fromUtf8(data));
                            QString temp = mapping.captured(2).replace(QStringLiteral("_"), QStringLiteral(" "));
                            mappings.insert(temp, mapping.captured(3));
                            eraseMappings.insert(temp, QPair<int, int>(mapping.captured(5).toInt(), mapping.captured(6).toInt()));
                            eraseAllMappings.insert(temp, QPair<int, int>(mapping.captured(4).toInt(), mapping.captured(6).toInt()));
                            vidpidMappings.insert(temp, mapping.captured(7));
                        }
                        else
                        {
                            boards.close();
                            break;
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
                                mappings.insert(QStringLiteral("NANO33_M4_OLD"), QStringLiteral("NANO33"));
                                eraseMappings.insert(QStringLiteral("NANO33_M4_OLD"), QPair<int, int>(0, 0));
                                eraseAllMappings.insert(QStringLiteral("NANO33_M4_OLD"), QPair<int, int>(0, 0));
                                vidpidMappings.insert(QStringLiteral("NANO33_M4_OLD"), QStringLiteral("2341:805a"));

                                mappings.insert(QStringLiteral("PICO_M0_OLD"), QStringLiteral("PICO"));
                                eraseMappings.insert(QStringLiteral("PICO_M0_OLD"), QPair<int, int>(0, 0));
                                eraseAllMappings.insert(QStringLiteral("PICO_M0_OLD"), QPair<int, int>(0, 0));
                                vidpidMappings.insert(QStringLiteral("PICO_M0_OLD"), QStringLiteral("2341:805e"));

                                for(QMap<QString, QString>::iterator it = mappings.begin(); it != mappings.end(); )
                                {
                                    bool found = false;

                                    foreach(const QString &device, dfuDevices)
                                    {
                                        if(device.split(QStringLiteral(",")).first() == vidpidMappings.value(it.key()))
                                        {
                                            found = true;
                                            break;
                                        }
                                    }

                                    if(!found)
                                    {
                                        eraseMappings.remove(it.key());
                                        eraseAllMappings.remove(it.key());
                                        vidpidMappings.remove(it.key());
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
                                int index = mappings.keys().indexOf(settings->value(QStringLiteral(LAST_BOARD_TYPE_STATE)).toString());

                                bool ok = mappings.size() == 1;
                                QString temp = (mappings.size() == 1) ? mappings.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),
                                    Tr::tr("Connect"), Tr::tr("Please select the board type"),
                                    mappings.keys(), (index != -1) ? index : 0, false, &ok,
                                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                                if(ok)
                                {
                                    settings->setValue(QStringLiteral(LAST_BOARD_TYPE_STATE), temp);

                                    int answer = dfuDeviceResetToRelease ? (dfuDeviceEraseFlash ? QMessageBox::Yes : QMessageBox::No) : QMessageBox::question(Core::ICore::dialogParent(),
                                        Tr::tr("Connect"),
                                        Tr::tr("Erase the internal file system?"),
                                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);

                                    if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))
                                    {
                                        forceBootloader = true;
                                        forceFlashFSErase = answer == QMessageBox::Yes;
                                        forceBootloaderBricked = true;
                                        originalFirmwareFolder = mappings.value(temp);
                                        firmwarePath = Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(originalFirmwareFolder).pathAppended(QStringLiteral("firmware.bin")).toString();
                                        originalEraseFlashSectorStart = eraseMappings.value(temp).first;
                                        originalEraseFlashSectorEnd = eraseMappings.value(temp).second;
                                        originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;
                                        originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;
                                        originalDfuVidPid = vidpidMappings.value(temp);
                                    }
                                }
                            }
                            else
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
            settings->setValue(QStringLiteral(LAST_SERIAL_PORT_STATE), selectedPort);
        }
        else
        {
            int index = stringList.indexOf(settings->value(QStringLiteral(LAST_SERIAL_PORT_STATE)).toString());

            bool ok;
            QString temp = QInputDialog::getItem(Core::ICore::dialogParent(),
                Tr::tr("Connect"), Tr::tr("Please select a serial port"),
                stringList, (index != -1) ? index : 0, false, &ok,
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

            if(ok)
            {
                selectedPort = temp;
                settings->setValue(QStringLiteral(LAST_SERIAL_PORT_STATE), selectedPort);
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
            settings->beginGroup(QStringLiteral(SETTINGS_GROUP));

            if(dfuDevices.size() == 1)
            {
                selectedDfuDevice = dfuDevices.first();
                settings->setValue(QStringLiteral(LAST_DFU_PORT_STATE), selectedDfuDevice);
            }
            else
            {
                int index = dfuDevices.indexOf(settings->value(QStringLiteral(LAST_DFU_PORT_STATE)).toString());

                bool ok;
                QString temp = QInputDialog::getItem(Core::ICore::dialogParent(),
                    Tr::tr("Connect"), Tr::tr("Please select a DFU Device"),
                    dfuDevices, (index != -1) ? index : 0, false, &ok,
                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                if(ok)
                {
                    selectedDfuDevice = temp;
                    settings->setValue(QStringLiteral(LAST_DFU_PORT_STATE), selectedDfuDevice);
                }
            }

            if(selectedDfuDevice.isEmpty())
            {
                CONNECT_END();
            }

            settings->endGroup();
        }

        bool isIMX = false;
        bool isArduino = false;
        bool isTouchToReset = false;
        bool isPortenta = false;
        bool isNiclav = false;
        bool isNRF = false;
        bool isRPIPico = false;

        if(!selectedPort.isEmpty())
        {
            QSerialPortInfo raw_arduinoPort = QSerialPortInfo(selectedPort);
            MyQSerialPortInfo arduinoPort(raw_arduinoPort);

            isArduino = arduinoPort.hasVendorIdentifier() &&
                        arduinoPort.hasProductIdentifier() &&
                     (((arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && (((arduinoPort.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||
                                                                              ((arduinoPort.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||
                                                                              ((arduinoPort.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||
                                                                              ((arduinoPort.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID))) ||
                      ((arduinoPort.vendorIdentifier() == RPI2040_VID) && (arduinoPort.productIdentifier() == RPI2040_PID)));
            isTouchToReset = arduinoPort.hasVendorIdentifier() &&
                             arduinoPort.hasProductIdentifier() &&
                            (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && ((arduinoPort.productIdentifier() == PORTENTA_TTR_1_PID) ||
                                                                                   (arduinoPort.productIdentifier() == PORTENTA_TTR_2_PID) ||
                                                                                   (arduinoPort.productIdentifier() == NICLA_TTR_1_PID) ||
                                                                                   (arduinoPort.productIdentifier() == NICLA_TTR_2_PID));
            isPortenta = arduinoPort.hasVendorIdentifier() &&
                         arduinoPort.hasProductIdentifier() &&
                        (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && ((arduinoPort.productIdentifier() == PORTENTA_APP_O_PID) ||
                                                                               (arduinoPort.productIdentifier() == PORTENTA_APP_N_PID));
            isNiclav = arduinoPort.hasVendorIdentifier() &&
                       arduinoPort.hasProductIdentifier() &&
                      (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && (arduinoPort.productIdentifier() == NICLA_APP_PID);
            isNRF = arduinoPort.hasVendorIdentifier() &&
                    arduinoPort.hasProductIdentifier() &&
                   (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && (arduinoPort.productIdentifier() == NRF_APP_PID);
            isRPIPico = arduinoPort.hasVendorIdentifier() &&
                        arduinoPort.hasProductIdentifier() &&
                     (((arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && (arduinoPort.productIdentifier() == RPI_APP_PID)) ||
                      ((arduinoPort.vendorIdentifier() == RPI2040_VID) && (arduinoPort.productIdentifier() == RPI2040_PID)));
        }

        if(!selectedDfuDevice.isEmpty())
        {
            QStringList vidpid = selectedDfuDevice.split(QStringLiteral(",")).first().split(QStringLiteral(":"));

            isIMX = imxVidPidList().contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isArduino = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID))) ||
                        ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));
            isPortenta = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == PORTENTA_LDR_PID);
            isNiclav = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NICLA_LDR_PID);
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

            isIMX = imxVidPidList().contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isArduino = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID))) ||
                        ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));
            isPortenta = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == PORTENTA_LDR_PID);
            isNiclav = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NICLA_LDR_PID);
            isNRF = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == NRF_LDR_PID));
            isRPIPico = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == RPI_LDR_PID))) ||
                        ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));

            if(isIMX) selectedDfuDevice = originalDfuVidPid + QStringLiteral(",NULL");
            if(isArduino) selectedDfuDevice = originalDfuVidPid + QStringLiteral(",NULL");

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

        if (dfuNoDialogs && (!isIMX) && (!isArduino)) {
            firmwarePath = QFileInfo(firmwarePath).path() + QStringLiteral("/bootloader.dfu");
            repairingBootloader = true;
        }

        // Open Port //////////////////////////////////////////////////////////

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

        if(isTouchToReset)
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
                    QRegularExpressionMatch match = QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]")).match(arch2);

                    if(match.hasMatch())
                    {
                        isIMX = match.captured(1).contains(QStringLiteral("IMX"));
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
                    if(firmwarePath.isEmpty()) firmwarePath = Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(QStringLiteral(OLD_API_BOARD)).pathAppended(QStringLiteral("firmware.bin")).toString();

                    if(installTheLatestDevelopmentFirmware)
                    {
                        if(!getTheLatestDevelopmentFirmware(QStringLiteral(OLD_API_BOARD), &firmwarePath))
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
                        QFile boards(Core::ICore::userResourcePath(QStringLiteral("firmware/boards.txt")).toString());

                        if(boards.open(QIODevice::ReadOnly))
                        {
                            QMap<QString, QString> mappings;
                            QMap<QString, QPair<int, int> > eraseMappings;
                            QMap<QString, QPair<int, int> > eraseAllMappings;
                            QMap<QString, QString> vidpidMappings;

                            forever
                            {
                                QByteArray data = boards.readLine();

                                if((boards.error() == QFile::NoError) && (!data.isEmpty()))
                                {
                                    QRegularExpressionMatch mapping = QRegularExpression(QStringLiteral("(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\S+)")).match(QString::fromUtf8(data));
                                    QString temp = mapping.captured(1).replace(QStringLiteral("_"), QStringLiteral(" "));
                                    mappings.insert(temp, mapping.captured(3));
                                    eraseMappings.insert(temp, QPair<int, int>(mapping.captured(5).toInt(), mapping.captured(6).toInt()));
                                    eraseAllMappings.insert(temp, QPair<int, int>(mapping.captured(4).toInt(), mapping.captured(6).toInt()));
                                    vidpidMappings.insert(temp, mapping.captured(7));
                                }
                                else
                                {
                                    boards.close();
                                    break;
                                }
                            }

                            QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified().replace(QStringLiteral("_"), QStringLiteral(" "));

                            if(!mappings.contains(temp))
                            {
                                int index = mappings.keys().indexOf(settings->value(QStringLiteral(LAST_BOARD_TYPE_STATE)).toString());

                                bool ok = mappings.size() == 1;
                                temp = (mappings.size() == 1) ? mappings.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),
                                    Tr::tr("Connect"), Tr::tr("Please select the board type"),
                                    mappings.keys(), (index != -1) ? index : 0, false, &ok,
                                    Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                    (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                                if(ok)
                                {
                                    settings->setValue(QStringLiteral(LAST_BOARD_TYPE_STATE), temp);
                                }
                                else
                                {
                                    CLOSE_CONNECT_END();
                                }
                            }

                            if(firmwarePath.isEmpty())
                            {
                                originalFirmwareFolder = mappings.value(temp);
                                firmwarePath = Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(originalFirmwareFolder).pathAppended(QStringLiteral("firmware.bin")).toString();
                                originalEraseFlashSectorStart = eraseMappings.value(temp).first;
                                originalEraseFlashSectorEnd = eraseMappings.value(temp).second;
                                originalEraseFlashSectorAllStart = eraseAllMappings.value(temp).first;
                                originalEraseFlashSectorAllEnd = eraseAllMappings.value(temp).second;

                                if(installTheLatestDevelopmentFirmware)
                                {
                                    if(!getTheLatestDevelopmentFirmware(mappings.value(temp), &firmwarePath))
                                    {
                                        CLOSE_CONNECT_END();
                                    }
                                }
                            }

                            QStringList vidpid = vidpidMappings.value(temp).split(QStringLiteral(":"));
                            isIMX = imxVidPidList().contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
                            isArduino = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||
                                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||
                                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||
                                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID))) ||
                                       ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));
                            isPortenta = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == PORTENTA_LDR_PID);
                            isNiclav = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NICLA_LDR_PID);
                            isNRF = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == NRF_LDR_PID));
                            isRPIPico = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == RPI_LDR_PID))) ||
                                        ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));

                            QRegularExpressionMatch match = QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]")).match(arch2);

                            if(match.hasMatch())
                            {
                                m_boardType = match.captured(1);
                                m_boardId = match.captured(2);
                                m_boardVID = vidpid.at(0).toInt(nullptr, 16);
                                m_boardPID = vidpid.at(1).toInt(nullptr, 16);
                            }
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Connect"),
                                Tr::tr("Error: %L1!").arg(boards.errorString()));

                            CLOSE_CONNECT_END();
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

                if((!isIMX) && (!isArduino) && firmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
                {
                    QEventLoop loop;

                    connect(m_iodevice, &OpenMVPluginIO::closeResponse,
                            &loop, &QEventLoop::quit);

                    m_iodevice->close();

                    loop.exec();
                }
            }

            // BIN Bootloader /////////////////////////////////////////////////

            bool tryFastMode = true;

            while((!isIMX) && (!isArduino) && (justEraseFlashFs || firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)))
            {
                QFile file(firmwarePath);

                if(justEraseFlashFs || file.open(QIODevice::ReadOnly))
                {
                    QByteArray data = justEraseFlashFs ? QByteArray() : file.readAll();

                    if(justEraseFlashFs || ((file.error() == QFile::NoError) && (!data.isEmpty())))
                    {
                        if(!justEraseFlashFs) file.close();

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
                                    ? QString(QStringLiteral("%1%2")).arg(Tr::tr("Disconnect your OpenMV Cam and then reconnect it...")).arg(justEraseFlashFs ? QString() : Tr::tr("\n\nHit cancel to skip to DFU reprogramming."))
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

                            while(!loopExit)
                            {
                                QSerialPortInfo::availablePorts();
                                QApplication::processEvents();
                                // Keep updating the list of available serial
                                // ports for the non-gui serial thread.
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

                            if(!done2)
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    Tr::tr("Connect"),
                                    Tr::tr("Unable to connect to your OpenMV Cam's normal bootloader!"));

                                if((!justEraseFlashFs) && forceFirmwarePath.isEmpty() && QMessageBox::question(Core::ICore::dialogParent(),
                                    Tr::tr("Connect"),
                                    Tr::tr("OpenMV IDE can still try to repair your OpenMV Cam using your OpenMV Cam's DFU Bootloader.\n\n"
                                       "Continue?"),
                                    QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                                == QMessageBox::Ok)
                                {
                                    firmwarePath = QFileInfo(firmwarePath).path() + QStringLiteral("/bootloader.dfu");
                                    repairingBootloader = true;
                                    break;
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

                            if((m_autoUpdateOnConnect.isEmpty()) && (!m_autoEraseOnConnect)) QMessageBox::information(Core::ICore::dialogParent(),
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

            if(isIMX)
            {
                QFile imxSettings(Core::ICore::userResourcePath(QStringLiteral("firmware/imx.txt")).toString());
                QJsonObject outObj;

                if(imxSettings.open(QIODevice::ReadOnly))
                {
                    QJsonParseError error;
                    QJsonDocument doc = QJsonDocument::fromJson(imxSettings.readAll(), &error);

                    if(error.error == QJsonParseError::NoError)
                    {
                        if(originalFirmwareFolder.isEmpty())
                        {
                            QFile boards(Core::ICore::userResourcePath(QStringLiteral("firmware/boards.txt")).toString());

                            if(boards.open(QIODevice::ReadOnly))
                            {
                                QList<QPair<int, int> > vidpidlist = imxVidPidList(false, true);
                                QMap<QString, QString> mappings;

                                forever
                                {
                                    QByteArray data = boards.readLine();

                                    if((boards.error() == QFile::NoError) && (!data.isEmpty()))
                                    {
                                        QRegularExpressionMatch mapping = QRegularExpression(QStringLiteral("(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\S+)")).match(QString::fromUtf8(data));
                                        QString temp = mapping.captured(2).replace(QStringLiteral("_"), QStringLiteral(" "));
                                        QStringList vidpid = mapping.captured(7).split(QStringLiteral(":"));
                                        if(vidpidlist.contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)))) mappings.insert(temp, mapping.captured(3));
                                    }
                                    else
                                    {
                                        boards.close();
                                        break;
                                    }
                                }

                                if(!mappings.isEmpty())
                                {
                                    int index = mappings.keys().indexOf(settings->value(QStringLiteral(LAST_BOARD_TYPE_STATE)).toString());
                                    bool ok = mappings.size() == 1;
                                    QString temp = (mappings.size() == 1) ? mappings.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),
                                        Tr::tr("Connect"), Tr::tr("Please select the board type"),
                                        mappings.keys(), (index != -1) ? index : 0, false, &ok,
                                        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                                    if(ok)
                                    {
                                        settings->setValue(QStringLiteral(LAST_BOARD_TYPE_STATE), temp);
                                        originalFirmwareFolder = mappings.value(temp);
                                    }
                                    else
                                    {
                                        CONNECT_END();
                                    }
                                }
                            }
                        }

                        if(!originalFirmwareFolder.isEmpty())
                        {
                            bool foundMatch = false;

                            foreach(const QJsonValue &val, doc.array())
                            {
                                QJsonObject obj = val.toObject();

                                if(originalFirmwareFolder == obj.value(QStringLiteral("firmware_folder")).toString())
                                {
                                    QString secureBootloaderPath = Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(originalFirmwareFolder).pathAppended(obj.value(QStringLiteral("sdphost_flash_loader_path")).toString()).toString();
                                    QString bootloaderPath = Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(originalFirmwareFolder).pathAppended(obj.value(QStringLiteral("blhost_secure_bootloader_path")).toString()).toString();
                                    outObj = obj;
                                    outObj.insert(QStringLiteral("sdphost_flash_loader_path"), secureBootloaderPath);
                                    outObj.insert(QStringLiteral("blhost_secure_bootloader_path"), bootloaderPath);
                                    outObj.insert(QStringLiteral("blhost_secure_bootloader_length"),
                                            QString::number(QFileInfo(bootloaderPath).size(), 16).prepend(QStringLiteral("0x")));
                                    outObj.insert(QStringLiteral("blhost_firmware_path"), firmwarePath);
                                    outObj.insert(QStringLiteral("blhost_firmware_length"),
                                            QString::number(QFileInfo(firmwarePath).size(), 16).prepend(QStringLiteral("0x")));
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
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("Connect"),
                            Tr::tr("Error: %L1!").arg(error.errorString()));

                        CONNECT_END();
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Connect"),
                        Tr::tr("Error: %L1!").arg(imxSettings.errorString()));

                    CONNECT_END();
                }

                if(selectedDfuDevice.isEmpty())
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
                        Utils::QtcProcess process;
                        process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(QStringLiteral("umount")),
                                           QStringList() << QDir::toNativeSeparators(QDir::cleanPath(m_portPath))));
                        process.runBlocking(Utils::EventLoopMode::On);

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

                    if(!imxGetDeviceSupported())
                    {
                        CONNECT_END();
                    }

                    QApplication::setOverrideCursor(QCursor(Qt::WaitCursor));

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
                            Tr::tr("OpenMV IDE can still try to repair your OpenMV Cam using your OpenMV Cam's SBL Bootloader.\n\n"
                               "Continue?"),
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
                                if(imxDownloadBootloaderAndFirmware(outObj, forceFlashFSErase, justEraseFlashFs))
                                {
                                    if((m_autoUpdateOnConnect.isEmpty()) && (!m_autoEraseOnConnect)) QMessageBox::information(Core::ICore::dialogParent(),
                                        Tr::tr("Connect"),
                                        Tr::tr("Firmware update complete!\n\n") +
                                        Tr::tr("Click the Ok button after your OpenMV Cam has enumerated and finished running its built-in self test (blue led blinking - this takes a while).") +
                                        Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                                           "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open)."));

                                    RECONNECT_WAIT_END();
                                }
                            }
                        }

                        CONNECT_END();
                    }
                    else
                    {
                        if(imxDownloadFirmware(outObj, forceFlashFSErase, justEraseFlashFs))
                        {
                            if((m_autoUpdateOnConnect.isEmpty()) && (!m_autoEraseOnConnect)) QMessageBox::information(Core::ICore::dialogParent(),
                                Tr::tr("Connect"),
                                QString(QStringLiteral("%1%2%3%4")).arg((justEraseFlashFs ? Tr::tr("Onboard Data Flash Erased!\n\n") : Tr::tr("Firmware Upgrade complete!\n\n")))
                                .arg(Tr::tr("Your OpenMV Cam will start running its built-in self-test if no sd card is attached... this may take a while.\n\n"))
                                .arg(Tr::tr("Click OK when your OpenMV Cam's RGB LED starts blinking blue - which indicates the self-test is complete."))
                                .arg(Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                                        "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open).")));

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
                    QPair<int , int> entry(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16));

                    // SPD Mode (SBL)
                    if(imxVidPidList(true, false).contains(entry))
                    {
                        if(imxDownloadBootloaderAndFirmware(outObj, forceFlashFSErase, justEraseFlashFs))
                        {
                            if((m_autoUpdateOnConnect.isEmpty()) && (!m_autoEraseOnConnect)) QMessageBox::information(Core::ICore::dialogParent(),
                                Tr::tr("Connect"),
                                Tr::tr("Firmware update complete!\n\n") +
                                Tr::tr("Click the Ok button after your OpenMV Cam has enumerated and finished running its built-in self test (blue led blinking - this takes a while).") +
                                Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                                   "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open)."));

                            RECONNECT_WAIT_END();
                        }
                        else
                        {
                            CONNECT_END();
                        }
                    }
                    // BL Mode
                    else if(imxVidPidList(false, true).contains(entry))
                    {
                        if(imxDownloadFirmware(outObj, forceFlashFSErase, justEraseFlashFs))
                        {
                            if((m_autoUpdateOnConnect.isEmpty()) && (!m_autoEraseOnConnect)) QMessageBox::information(Core::ICore::dialogParent(),
                                Tr::tr("Connect"),
                                QString(QStringLiteral("%1%2%3%4")).arg((justEraseFlashFs ? Tr::tr("Onboard Data Flash Erased!\n\n") : Tr::tr("Firmware Upgrade complete!\n\n")))
                                .arg(Tr::tr("Your OpenMV Cam will start running its built-in self-test if no sd card is attached... this may take a while.\n\n"))
                                .arg(Tr::tr("Click OK when your OpenMV Cam's RGB LED starts blinking blue - which indicates the self-test is complete."))
                                .arg(Tr::tr("\n\nIf you overwrote main.py on your OpenMV Cam and did not erase the disk then your OpenMV Cam will just run that main.py."
                                        "\n\nIn this case click OK when you see your OpenMV Cam's internal flash drive mount (a window may or may not pop open).")));

                            RECONNECT_WAIT_END();
                        }
                        else
                        {
                            CONNECT_END();
                        }
                    }
                }
            }

            if(isArduino)
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
                        Utils::QtcProcess process;
                        process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(QStringLiteral("umount")),
                                                              QStringList() << QDir::toNativeSeparators(QDir::cleanPath(m_portPath))));
                        process.runBlocking(Utils::EventLoopMode::On);

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

                    m_iodevice->sysReset((!isNRF) || (!justEraseFlashFs));
                    m_iodevice->close();

                    loop.exec();

                    QElapsedTimer elaspedTimer;
                    elaspedTimer.start();

                    while(!elaspedTimer.hasExpired(RESET_TO_DFU_SEARCH_TIME))
                    {
                        QApplication::processEvents();
                    }
                }

                if(isPortenta || isNiclav)
                {
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

                    QFile dfuSettings(Core::ICore::userResourcePath(QStringLiteral("firmware/dfu.txt")).toString());
                    QString boardTypeToDfuDeviceVidPid;
                    QStringList eraseCommands, extraProgramAddrCommands, extraProgramPathCommands;
                    QString binProgramCommand, dfuProgramCommand;

                    if(dfuSettings.open(QIODevice::ReadOnly))
                    {
                        QJsonParseError error;
                        QJsonDocument doc = QJsonDocument::fromJson(dfuSettings.readAll(), &error);

                        if(error.error == QJsonParseError::NoError)
                        {
                            if(selectedDfuDevice.isEmpty())
                            {
                                bool foundMatch = false;

                                foreach(const QJsonValue &val, doc.array())
                                {
                                    QJsonObject obj = val.toObject();

                                    QJsonArray appvidpidArray = obj.value(QStringLiteral("appvidpid")).toArray();
                                    bool isApp = appvidpidArray.isEmpty();

                                    if(!isApp)
                                    {
                                        foreach(const QJsonValue &appvidpid, appvidpidArray)
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
                                        boardTypeToDfuDeviceVidPid = obj.value(QStringLiteral("vidpid")).toString();

                                        QJsonArray eraseCommandsArray = obj.value(QStringLiteral("eraseCommands")).toArray(); foreach(const QJsonValue &command, eraseCommandsArray)
                                        {
                                            eraseCommands.append(command.toString());
                                        }

                                        QJsonArray extraProgramCommandsArray = obj.value(QStringLiteral("extraProgramCommands")).toArray(); foreach(const QJsonValue &command, extraProgramCommandsArray)
                                        {
                                            QJsonObject obj = command.toObject();
                                            extraProgramAddrCommands.append(obj.value(QStringLiteral("addr")).toString());
                                            extraProgramPathCommands.append(obj.value(QStringLiteral("path")).toString());
                                        }

                                        binProgramCommand = obj.value(QStringLiteral("binProgramCommand")).toString();
                                        dfuProgramCommand = obj.value(QStringLiteral("dfuProgramCommand")).toString();
                                        foundMatch = true;
                                        break;
                                    }
                                }

                                if(!foundMatch)
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        Tr::tr("Connect"),
                                        Tr::tr("No DFU settings for the selected board type!") + QString(QStringLiteral("\n\nVID: %1, PID: %2")).arg(m_boardVID, m_boardPID));

                                    CONNECT_END();
                                }
                            }
                            else
                            {
                                bool foundMatch = false;

                                foreach(const QJsonValue &val, doc.array())
                                {
                                    QJsonObject obj = val.toObject();

                                    if(selectedDfuDeviceVidPid == obj.value(QStringLiteral("vidpid")).toString())
                                    {
                                        QJsonArray eraseCommandsArray = obj.value(QStringLiteral("eraseCommands")).toArray(); foreach(const QJsonValue &command, eraseCommandsArray)
                                        {
                                            eraseCommands.append(command.toString());
                                        }

                                        QJsonArray extraProgramCommandsArray = obj.value(QStringLiteral("extraProgramCommands")).toArray(); foreach(const QJsonValue &command, extraProgramCommandsArray)
                                        {
                                            QJsonObject obj = command.toObject();
                                            extraProgramAddrCommands.append(obj.value(QStringLiteral("addr")).toString());
                                            extraProgramPathCommands.append(obj.value(QStringLiteral("path")).toString());
                                        }

                                        binProgramCommand = obj.value(QStringLiteral("binProgramCommand")).toString();
                                        dfuProgramCommand = obj.value(QStringLiteral("dfuProgramCommand")).toString();
                                        foundMatch = true;
                                        break;
                                    }
                                }

                                if(!foundMatch)
                                {
                                    QStringList dfuDeviceVidPidList = selectedDfuDeviceVidPid.split(QLatin1Char(':'));

                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        Tr::tr("Connect"),
                                        Tr::tr("No DFU settings for the selected device!") + QString(QStringLiteral("\n\nVID: %1, PID: %2")).arg(dfuDeviceVidPidList.first().toInt(nullptr, 16), dfuDeviceVidPidList.last().toInt(nullptr, 16)));

                                    CONNECT_END();
                                }
                            }
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Connect"),
                                Tr::tr("Error: %L1!").arg(error.errorString()));

                            CONNECT_END();
                        }
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("Connect"),
                            Tr::tr("Error: %L1!").arg(dfuSettings.errorString()));

                        CONNECT_END();
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
                                Utils::QtcProcess process;

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
                                    if((m_autoUpdateOnConnect.isEmpty()) && (!m_autoEraseOnConnect)) QMessageBox::information(Core::ICore::dialogParent(),
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
                        // Extra Program Flash ////////////////////////////////
                        {
                            QString command;
                            Utils::QtcProcess process;

                            for(int i = 0, j = extraProgramAddrCommands.size(); i < j; i++)
                            {
                                downloadFirmware(Tr::tr("Flashing Firmware"), command, process, Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(extraProgramPathCommands.at(i)).toString(), dfuDeviceVidPid, extraProgramAddrCommands.at(i) + dfuDeviceSerial);

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
                        Utils::QtcProcess process;
                        downloadFirmware(Tr::tr("Flashing Firmware"), command, process, QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)), dfuDeviceVidPid, (firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive) ? binProgramCommand : dfuProgramCommand) + dfuDeviceSerial);

                        if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
                        {
                            if((m_autoUpdateOnConnect.isEmpty()) && (!m_autoEraseOnConnect)) QMessageBox::information(Core::ICore::dialogParent(),
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
                        }
                    }
                }
                else if(isNRF)
                {
                    QString selectedDfuDeviceVidPid = selectedDfuDevice.isEmpty() ? QString() : selectedDfuDevice;
                    QStringList selectedDfuDeviceVidPidList = selectedDfuDeviceVidPid.split(QLatin1Char(':'));
                    QString dfuDevicePort;

                    if((selectedDfuDeviceVidPidList.first().toInt(nullptr, 16) == ARDUINOCAM_VID)
                    && (selectedDfuDeviceVidPidList.last().toInt(nullptr, 16) == NRF_OLD_PID))
                    {
                        foreach(QSerialPortInfo raw_port, QSerialPortInfo::availablePorts())
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

                        Utils::QtcProcess process;
                        bossacRunBootloader(process, dfuDevicePort);

                        selectedDfuDeviceVidPid = QString(QStringLiteral("%1:%2").arg(ARDUINOCAM_VID, 4, 16, QLatin1Char('0')).arg(NRF_LDR_PID, 4, 16, QLatin1Char('0')));

                        QElapsedTimer elaspedTimer;
                        elaspedTimer.start();
                        dfuDevicePort = QString();

                        while(dfuDevicePort.isEmpty() && (!elaspedTimer.hasExpired(RESET_TO_DFU_SEARCH_TIME)))
                        {
                            QApplication::processEvents();

                            foreach(QSerialPortInfo raw_port, QSerialPortInfo::availablePorts())
                            {
                                MyQSerialPortInfo port(raw_port);

                                if(port.hasVendorIdentifier() && ((port.vendorIdentifier() == ARDUINOCAM_VID))
                                && port.hasProductIdentifier() && ((port.productIdentifier() == NRF_LDR_PID)))
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
                                Tr::tr("BOSSAC device %1 missing!").arg(selectedDfuDeviceVidPid));

                            CONNECT_END();
                        }
                    }

                    QFile bossacSettings(Core::ICore::userResourcePath(QStringLiteral("firmware/bossac.txt")).toString());
                    QString boardTypeToDfuDeviceVidPid;
                    QString binProgramCommand;

                    if(bossacSettings.open(QIODevice::ReadOnly))
                    {
                        QJsonParseError error;
                        QJsonDocument doc = QJsonDocument::fromJson(bossacSettings.readAll(), &error);

                        if(error.error == QJsonParseError::NoError)
                        {
                            if(selectedDfuDevice.isEmpty())
                            {
                                bool foundMatch = false;

                                foreach(const QJsonValue &val, doc.array())
                                {
                                    QJsonObject obj = val.toObject();

                                    if(m_boardType == obj.value(QStringLiteral("boardType")).toString())
                                    {
                                        boardTypeToDfuDeviceVidPid = obj.value(QStringLiteral("vidpid")).toString();

                                        binProgramCommand = obj.value(QStringLiteral("binProgramCommand")).toString();
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

                                foreach(const QJsonValue &val, doc.array())
                                {
                                    QJsonObject obj = val.toObject();

                                    if(selectedDfuDeviceVidPid == obj.value(QStringLiteral("vidpid")).toString())
                                    {
                                        binProgramCommand = obj.value(QStringLiteral("binProgramCommand")).toString();
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
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Connect"),
                                Tr::tr("Error: %L1!").arg(error.errorString()));

                            CONNECT_END();
                        }
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("Connect"),
                            Tr::tr("Error: %L1!").arg(bossacSettings.errorString()));

                        CONNECT_END();
                    }

                    if(forceFlashFSErase && justEraseFlashFs)
                    {
                        if((m_autoUpdateOnConnect.isEmpty()) && (!m_autoEraseOnConnect)) QMessageBox::information(Core::ICore::dialogParent(),
                            Tr::tr("Connect"),
                            QString(QStringLiteral("%1"))
                            .arg(Tr::tr("Your Nano 33 BLE doesn't have an onboard data flash disk.")));

                        RECONNECT_WAIT_END();
                    }

                    QString dfuDeviceVidPid = selectedDfuDevice.isEmpty() ? boardTypeToDfuDeviceVidPid : selectedDfuDeviceVidPid;
                    QStringList dfuDeviceVidPidList = dfuDeviceVidPid.split(QLatin1Char(':'));

                    QElapsedTimer elaspedTimer;
                    elaspedTimer.start();
                    dfuDevicePort = QString();

                    while(dfuDevicePort.isEmpty() && (!elaspedTimer.hasExpired(RESET_TO_DFU_SEARCH_TIME)))
                    {
                        QApplication::processEvents();

                        foreach(QSerialPortInfo raw_port, QSerialPortInfo::availablePorts())
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
                    Utils::QtcProcess process;
                    bossacDownloadFirmware(Tr::tr("Flashing Firmware"), command, process, QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)), dfuDevicePort, binProgramCommand);

                    if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
                    {
                        if((m_autoUpdateOnConnect.isEmpty()) && (!m_autoEraseOnConnect)) QMessageBox::information(Core::ICore::dialogParent(),
                            Tr::tr("Connect"),
                            Tr::tr("BOSSAC firmware update complete!\n\n") +
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
                        QMessageBox box(QMessageBox::Critical, Tr::tr("Connect"), Tr::tr("BOSSAC firmware update failed!"), QMessageBox::Ok, Core::ICore::dialogParent(),
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
                        box.setDetailedText(command + QStringLiteral("\n\n") + process.stdOut() + QStringLiteral("\n") + process.stdErr());
                        box.setDefaultButton(QMessageBox::Ok);
                        box.setEscapeButton(QMessageBox::Cancel);
                        box.exec();
                    }
                }
                else if(isRPIPico)
                {
                    // Erase Flash ////////////////////////////////////////

                    QString selectedDfuDeviceVidPid = selectedDfuDevice.isEmpty() ? QString() : selectedDfuDevice.split(QStringLiteral(",")).first();

                    QFile dfuSettings(Core::ICore::userResourcePath(QStringLiteral("firmware/picotool.txt")).toString());
                    QStringList eraseCommands;
                    QString binProgramCommand;

                    if(dfuSettings.open(QIODevice::ReadOnly))
                    {
                        QJsonParseError error;
                        QJsonDocument doc = QJsonDocument::fromJson(dfuSettings.readAll(), &error);

                        if(error.error == QJsonParseError::NoError)
                        {
                            if(selectedDfuDevice.isEmpty())
                            {
                                bool foundMatch = false;

                                foreach(const QJsonValue &val, doc.array())
                                {
                                    QJsonObject obj = val.toObject();

                                    if(m_boardType == obj.value(QStringLiteral("boardType")).toString())
                                    {
                                        QJsonArray eraseCommandsArray = obj.value(QStringLiteral("eraseCommands")).toArray(); foreach(const QJsonValue &command, eraseCommandsArray)
                                        {
                                            eraseCommands.append(command.toString());
                                        }

                                        binProgramCommand = obj.value(QStringLiteral("binProgramCommand")).toString();
                                        foundMatch = true;
                                        break;
                                    }
                                }

                                if(!foundMatch)
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        Tr::tr("Connect"),
                                        Tr::tr("No PicoTool settings for the selected board type!"));

                                    CONNECT_END();
                                }
                            }
                            else
                            {
                                bool foundMatch = false;

                                foreach(const QJsonValue &val, doc.array())
                                {
                                    QJsonObject obj = val.toObject();

                                    if(selectedDfuDeviceVidPid == obj.value(QStringLiteral("vidpid")).toString())
                                    {
                                        QJsonArray eraseCommandsArray = obj.value(QStringLiteral("eraseCommands")).toArray(); foreach(const QJsonValue &command, eraseCommandsArray)
                                        {
                                            eraseCommands.append(command.toString());
                                        }

                                        binProgramCommand = obj.value(QStringLiteral("binProgramCommand")).toString();
                                        foundMatch = true;
                                        break;
                                    }
                                }

                                if(!foundMatch)
                                {
                                    QMessageBox::critical(Core::ICore::dialogParent(),
                                        Tr::tr("Connect"),
                                        Tr::tr("No PicoTool settings for the selected device!"));

                                    CONNECT_END();
                                }
                            }
                        }
                        else
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                Tr::tr("Connect"),
                                Tr::tr("Error: %L1!").arg(error.errorString()));

                            CONNECT_END();
                        }
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("Connect"),
                            Tr::tr("Error: %L1!").arg(dfuSettings.errorString()));

                        CONNECT_END();
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
                                Utils::QtcProcess process;

                                for(int i = 0, j = eraseCommands.size(); i < j; i++)
                                {
                                    picotoolDownloadFirmware(Tr::tr("Erasing Disk"), command, process, QFileInfo(file).canonicalFilePath(), eraseCommands.at(i));

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
                                    picotoolReset(command, process);

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

                                    if((m_autoUpdateOnConnect.isEmpty()) && (!m_autoEraseOnConnect)) QMessageBox::information(Core::ICore::dialogParent(),
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
                        QString command;
                        Utils::QtcProcess process;
                        picotoolDownloadFirmware(Tr::tr("Flashing Firmware"), command, process, QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)), binProgramCommand);

                        if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
                        {
                            if((m_autoUpdateOnConnect.isEmpty()) && (!m_autoEraseOnConnect)) QMessageBox::information(Core::ICore::dialogParent(),
                                Tr::tr("Connect"),
                                Tr::tr("PicoTool firmware update complete!\n\n") +
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
                            QMessageBox box(QMessageBox::Critical, Tr::tr("Connect"), Tr::tr("PicoTool firmware update failed!"), QMessageBox::Ok, Core::ICore::dialogParent(),
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

            // DFU Bootloader /////////////////////////////////////////////////

            if(firmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
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
                        Utils::QtcProcess process;
                        downloadFirmware(Tr::tr("Flashing Bootloader"), command, process, QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)), originalDfuVidPid, QStringLiteral("-a 0 -s :leave"));

                        if((process.result() == Utils::ProcessResult::FinishedWithSuccess) || (command.contains(QStringLiteral("dfu-util")) && (process.result() == Utils::ProcessResult::FinishedWithError)))
                        {
                            if(repairingBootloader)
                            {
                                QMessageBox::information(Core::ICore::dialogParent(),
                                    Tr::tr("Connect"),
                                    Tr::tr("DFU bootloader reset complete!\n\n") +
                                    Tr::tr("Disconnect your OpenMV Cam from your computer and remove the jumper wire between the BOOT and RST pins.\n\n") +
                                    Tr::tr("OpenMV IDE will now try to update your OpenMV Cam again."));

                                RECONNECT_AND_FORCEBOOTLOADER_END();
                            }
                            else
                            {
                                QMessageBox::information(Core::ICore::dialogParent(),
                                                         Tr::tr("Connect"),
                                                         Tr::tr("DFU firmware update complete!\n\n") +
                                                         (Utils::HostOsInfo::isWindowsHost() ? Tr::tr("Disconnect your OpenMV Cam from your computer, remove the jumper wire between the BOOT and RST pins, and then reconnect your OpenMV Cam to your computer.\n\n") : QString()) +
                                                         Tr::tr("Click the Ok button after your OpenMV Cam has enumerated and finished running its built-in self test (blue led blinking - this takes a while)."));

                                RECONNECT_END();
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
        }

        // Check ID ///////////////////////////////////////////////////////////

        bool disableLicenseCheck = false;
        if(isPortenta || isNiclav || isNRF || isRPIPico) disableLicenseCheck = true;
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
                    QFile sensors(Core::ICore::userResourcePath(QStringLiteral("firmware/sensors.txt")).toString());

                    if(sensors.open(QIODevice::ReadOnly))
                    {
                        QMap<int, QString> mappings;

                        forever
                        {
                            QByteArray data = sensors.readLine();

                            if((sensors.error() == QFile::NoError) && (!data.isEmpty()))
                            {
                                QRegularExpressionMatch mapping = QRegularExpression(QStringLiteral("(\\S+)\\s+(\\S+)")).match(QString::fromUtf8(data));
                                mappings.insert(mapping.captured(2).toInt(nullptr, 0), mapping.captured(1));
                            }
                            else
                            {
                                sensors.close();
                                break;
                            }
                        }

                        m_sensorType = mappings.value(id2, Tr::tr("Unknown"));
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("Connect"),
                            Tr::tr("Error: %L1!").arg(sensors.errorString()));

                        CLOSE_CONNECT_END();
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
                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]")).match(arch2);

                if(match.hasMatch())
                {
                    QSerialPortInfo raw_tempPort = QSerialPortInfo(selectedPort);
                    MyQSerialPortInfo tempPort(raw_tempPort);

                    QString board = match.captured(1);
                    QString id = match.captured(2);

                    m_boardType = board;
                    m_boardId = id;
                    m_boardVID = tempPort.vendorIdentifier();
                    m_boardPID = tempPort.productIdentifier();

                    boardTypeLabel = board;

                    QFile boards(Core::ICore::userResourcePath(QStringLiteral("firmware/boards.txt")).toString());

                    if(boards.open(QIODevice::ReadOnly))
                    {
                        QMap<QString, QString> mappings;

                        forever
                        {
                            QByteArray data = boards.readLine();

                            if((boards.error() == QFile::NoError) && (!data.isEmpty()))
                            {
                                QRegularExpressionMatch mapping = QRegularExpression(QStringLiteral("(\\S+)\\s+(\\S+)\\s+(\\S+)\\s+(\\d+)\\s+(\\d+)\\s+(\\d+)\\s+(\\S+)")).match(QString::fromUtf8(data));
                                QString temp = mapping.captured(1).replace(QStringLiteral("_"), QStringLiteral(" "));
                                mappings.insert(temp, mapping.captured(2));
                            }
                            else
                            {
                                boards.close();
                                break;
                            }
                        }

                        if(!mappings.isEmpty())
                        {
                            QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified().replace(QStringLiteral("_"), QStringLiteral(" "));

                            if(mappings.contains(temp))
                            {
                                boardTypeLabel = mappings.value(temp).replace(QStringLiteral("_"), QStringLiteral(" "));
                            }
                        }
                    }

                    if((!disableLicenseCheck)
                    // Skip OpenMV Cam M4's...
                    && (board != QStringLiteral("M4")))
                    {
                        QNetworkAccessManager *manager = new QNetworkAccessManager(this);

                        connect(manager, &QNetworkAccessManager::finished, this, [this, manager, board, id] (QNetworkReply *reply) {

                            QByteArray data = reply->readAll();

                            if((reply->error() == QNetworkReply::NoError) && (!data.isEmpty()))
                            {
                                if(QString::fromUtf8(data).contains(QStringLiteral("<p>No</p>")))
                                {
                                    QTimer::singleShot(0, this, [this, board, id] { registerOpenMVCam(board, id); });
                                }
                            }

                            connect(reply, &QNetworkReply::destroyed, manager, &QNetworkAccessManager::deleteLater); reply->deleteLater();
                        });

                        QNetworkRequest request = QNetworkRequest(QUrl(QString(QStringLiteral("https://upload.openmv.io/openmv-swd-ids-check.php?board=%L1&id=%L2")).arg(board).arg(id)));
                        QNetworkReply *reply = manager->get(request);

                        if(reply)
                        {
                            connect(reply, &QNetworkReply::sslErrors, reply, static_cast<void (QNetworkReply::*)(void)>(&QNetworkReply::ignoreSslErrors));
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
        m_startAction->setEnabled(editor ? (editor->document() ? (!editor->document()->contents().isEmpty()) : false) : false);
        m_startAction->setVisible(true);
        m_stopAction->setEnabled(false);
        m_stopAction->setVisible(false);

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
        m_fpsLabel->setEnabled(true);
        m_fpsLabel->setText(Tr::tr("FPS: 0"));

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

            if(document && document->displayName() == QStringLiteral("helloworld_1.py"))
            {
                QByteArray data = document->contents();

                if((m_sensorType == QStringLiteral("HM01B0")) ||
                   (m_sensorType == QStringLiteral("HM0360")) ||
                   (m_sensorType == QStringLiteral("MT9V0X2")) ||
                   (m_sensorType == QStringLiteral("MT9V0X4")))
                {
                    data = data.replace(QByteArrayLiteral("sensor.set_pixformat(sensor.RGB565)"), QByteArrayLiteral("sensor.set_pixformat(sensor.GRAYSCALE)"));
                    if(m_sensorType == QStringLiteral("HM01B0")) data = data.replace(QByteArrayLiteral("sensor.set_framesize(sensor.VGA)"), QByteArrayLiteral("sensor.set_framesize(sensor.QVGA)"));
                }

                document->setPlainText(QString::fromUtf8(data));
            }
        }

        // Check Version //////////////////////////////////////////////////////

        QFile file(Core::ICore::userResourcePath(QStringLiteral("firmware/firmware.txt")).toString());

        if(file.open(QIODevice::ReadOnly))
        {
            QByteArray data = file.readAll();

            if((file.error() == QFile::NoError) && (!data.isEmpty()))
            {
                file.close();

                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).match(QString::fromUtf8(data));

                if((major2 < match.captured(1).toInt())
                || ((major2 == match.captured(1).toInt()) && (minor2 < match.captured(2).toInt()))
                || ((major2 == match.captured(1).toInt()) && (minor2 == match.captured(2).toInt()) && (patch2 < match.captured(3).toInt())))
                {
                    m_versionButton->setText(m_versionButton->text().append(Tr::tr(" - [ out of date - click here to updgrade ]")));

                    QTimer::singleShot(1, this, [this] {
                        if ((!m_autoConnect) && Utils::CheckableMessageBox::doNotAskAgainQuestion(Core::ICore::dialogParent(),
                                Tr::tr("Connect"),
                                Tr::tr("Your OpenMV Cam's firmware is out of date. Would you like to upgrade?"),
                                ExtensionSystem::PluginManager::settings(),
                                QStringLiteral(DONT_SHOW_UPGRADE_FW_AGAIN),
                                QDialogButtonBox::Yes | QDialogButtonBox::No | QDialogButtonBox::Cancel,
                                QDialogButtonBox::Yes, QDialogButtonBox::No, QDialogButtonBox::Yes) == QDialogButtonBox::Yes)
                        {
                            OpenMVPlugin::updateCam(true);
                        }
                    });
                }
                else
                {
                    m_versionButton->setText(m_versionButton->text().append(Tr::tr(" - [ latest ]")));
                }
            }
        }

        ///////////////////////////////////////////////////////////////////////

        m_working = false;

        OpenMVPlugin::setPortPath(true);

        if(m_autoRunOnConnect)
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
                        Utils::QtcProcess process;
                        process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(QStringLiteral("umount")),
                                                              QStringList() << QDir::toNativeSeparators(QDir::cleanPath(m_portPath))));
                        process.runBlocking(Utils::EventLoopMode::On);

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
            // LEAVE CACHED m_boardType = QString();
            // LEAVE CACHED m_boardId = QString();
            // LEAVE CACHED m_boardVID = 0;
            // LEAVE CACHED m_boardPID = 0;
            m_sensorType = QString();
            m_portName = QString();
            m_portPath = QString();
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
            m_startAction->setEnabled(false);
            m_startAction->setVisible(true);
            m_stopAction->setEnabled(false);
            m_stopAction->setVisible(false);

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
            m_fpsLabel->setDisabled(true);
            m_fpsLabel->setText(Tr::tr("FPS:"));

            m_frameBuffer->enableSaveTemplate(false);
            m_frameBuffer->enableSaveDescriptor(false);

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

            m_iodevice->getScriptRunning();

            loop.exec(); // sync with camera

            disconnect(conn);

            if(running2)
            {
                m_iodevice->scriptStop();

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
            m_fpsLabel->setText(Tr::tr("FPS: 0"));
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

            m_iodevice->getScriptRunning();

            loop.exec(); // sync with camera

            disconnect(conn);

            if(running2)
            {
                m_iodevice->scriptStop();

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
            }
        }

        ///////////////////////////////////////////////////////////////////////

        m_fpsLabel->setText(Tr::tr("FPS: 0"));

        ///////////////////////////////////////////////////////////////////////

        m_working = false;

        ///////////////////////////////////////////////////////////////////////

        if(Core::EditorManager::currentEditor()
            ? Core::EditorManager::currentEditor()->document()
                ? Core::EditorManager::currentEditor()->document()->displayName() == QStringLiteral("helloworld_1.py")
                : false
            : false)
        {
            QTimer::singleShot(2000, this, [] {
                Utils::CheckableMessageBox::doNotShowAgainInformation(Core::ICore::dialogParent(),
                        Tr::tr("More Examples"),
                        Tr::tr("You can find more examples under the File -> Examples menu.\n\n"
                           "In particular, checkout the Color-Tracking examples."),
                        ExtensionSystem::PluginManager::settings(),
                        QStringLiteral(DONT_SHOW_EXAMPLES_AGAIN),
                        QDialogButtonBox::Ok,
                        QDialogButtonBox::Ok);
            });
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

void OpenMVPlugin::updateCam(bool forceYes)
{
    if(!m_working)
    {
        QFile file(Core::ICore::userResourcePath(QStringLiteral("firmware/firmware.txt")).toString());

        if(file.open(QIODevice::ReadOnly))
        {
            QByteArray data = file.readAll();

            if((file.error() == QFile::NoError) && (!data.isEmpty()))
            {
                file.close();

                QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)")).match(QString::fromUtf8(data));

                if((m_major < match.captured(1).toInt())
                || ((m_major == match.captured(1).toInt()) && (m_minor < match.captured(2).toInt()))
                || ((m_major == match.captured(1).toInt()) && (m_minor == match.captured(2).toInt()) && (m_patch < match.captured(3).toInt())))
                {
                    if(forceYes || (QMessageBox::warning(Core::ICore::dialogParent(),
                        Tr::tr("Firmware Update"),
                        Tr::tr("Update your OpenMV Cam's firmware to the latest version?"),
                        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
                    == QMessageBox::Ok))
                    {
                        int answer = QMessageBox::question(Core::ICore::dialogParent(),
                            Tr::tr("Firmware Update"),
                            Tr::tr("Erase the internal file system?"),
                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);

                        if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))
                        {
                            disconnectClicked();

                            if(ExtensionSystem::PluginManager::specForPlugin(this)->state() != ExtensionSystem::PluginSpec::Stopped)
                            {
                                connectClicked(true, QString(), answer == QMessageBox::Yes);
                            }
                        }
                    }
                }
                else
                {
                    QMessageBox::information(Core::ICore::dialogParent(),
                        Tr::tr("Firmware Update"),
                        Tr::tr("Your OpenMV Cam's firmware is up to date."));

                    if(QMessageBox::question(Core::ICore::dialogParent(),
                        Tr::tr("Firmware Update"),
                        Tr::tr("Need to reset your OpenMV Cam's firmware to the release version?"),
                        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
                    == QMessageBox::Yes)
                    {
                        int answer = QMessageBox::question(Core::ICore::dialogParent(),
                            Tr::tr("Firmware Update"),
                            Tr::tr("Erase the internal file system?"),
                            QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No);

                        if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))
                        {
                            disconnectClicked();

                            if(ExtensionSystem::PluginManager::specForPlugin(this)->state() != ExtensionSystem::PluginSpec::Stopped)
                            {
                                connectClicked(true, QString(), answer == QMessageBox::Yes);
                            }
                        }
                    }
                }
            }
            else if(file.error() != QFile::NoError)
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Firmware Update"),
                    Tr::tr("Error: %L1!").arg(file.errorString()));
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Firmware Update"),
                    Tr::tr("Cannot open firmware.txt!"));
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Firmware Update"),
                Tr::tr("Error: %L1!").arg(file.errorString()));
        }
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Firmware Update"),
            Tr::tr("Busy... please wait..."));
    }
}

} // namespace Internal
} // namespace OpenMV
