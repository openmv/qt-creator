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

                Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
                settings->beginGroup(SETTINGS_GROUP);

                int old_major = settings->value(RESOURCES_MAJOR).toInt();
                int old_minor = settings->value(RESOURCES_MINOR).toInt();
                int old_patch = settings->value(RESOURCES_PATCH).toInt();

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

                                Utils::QtcSettings *settings2 = ExtensionSystem::PluginManager::settings();
                                settings2->beginGroup(SETTINGS_GROUP);

                                settings2->setValue(RESOURCES_MAJOR, 0);
                                settings2->setValue(RESOURCES_MINOR, 0);
                                settings2->setValue(RESOURCES_PATCH, 0);
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
                                    settings2->setValue(RESOURCES_MAJOR, new_major);
                                    settings2->setValue(RESOURCES_MINOR, new_minor);
                                    settings2->setValue(RESOURCES_PATCH, new_patch);
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

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(SETTINGS_GROUP);

    Utils::PathChooser *pathChooser = new Utils::PathChooser();
    pathChooser->setExpectedKind(Utils::PathChooser::File);
    pathChooser->setPromptDialogTitle(Tr::tr("Firmware Path"));
    pathChooser->setPromptDialogFilter(Tr::tr("Firmware Binary (*.bin *.dfu)"));
    pathChooser->setFilePath(Utils::FilePath::fromVariant(settings->value(LAST_FIRMWARE_PATH, QDir::homePath())));
    pathChooser->setHistoryCompleter(LAST_FIRMWARE_HISTORY, false);
    layout->addRow(Tr::tr("Firmware Path"), pathChooser);
    layout->addItem(new QSpacerItem(0, 6));

    QHBoxLayout *layout2 = new QHBoxLayout;
    layout2->setContentsMargins(0, 0, 0, 0);
    QWidget *widget = new QWidget;
    widget->setLayout(layout2);

    QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal file system"));
    checkBox->setChecked(settings->value(LAST_FLASH_FS_ERASE_STATE, false).toBool());
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
            settings->setValue(LAST_FIRMWARE_PATH, forceFirmwarePath);
            settings->setValue(LAST_FLASH_FS_ERASE_STATE, flashFSErase);
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

            Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
            settings->beginGroup(SETTINGS_GROUP);

            QHBoxLayout *layout2 = new QHBoxLayout;
            layout2->setContentsMargins(0, 0, 0, 0);
            QWidget *widget = new QWidget;
            widget->setLayout(layout2);

            QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal file system"));
            checkBox->setChecked(settings->value(LAST_FLASH_FS_ERASE_STATE, false).toBool());
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

                settings->setValue(LAST_FLASH_FS_ERASE_STATE, flashFSErase);
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
    if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QTimer::singleShot(0, this, [this, previousMapping] {connectClicked(true, QString(), false, false, false, false, previousMapping);}); \
    else if(m_autoUpdate == QStringLiteral("release")) QTimer::singleShot(0, this, [this, previousMapping] {connectClicked(true, QString(), m_autoErase, false, false, false, previousMapping);}); \
    else if(m_autoUpdate == QStringLiteral("developement")) QTimer::singleShot(0, this, [this, previousMapping] {connectClicked(true, QString(), m_autoErase, false, true, false, previousMapping);}); \
    else if(QFileInfo(m_autoUpdate).isFile()) QTimer::singleShot(0, this, [this, previousMapping] {connectClicked(true, m_autoUpdate, (m_autoErase || m_autoUpdate.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive)), false, false, false, previousMapping);}); \
    else if(m_autoErase) QTimer::singleShot(0, this, [this, previousMapping] {connectClicked(true, QString(), true, true, false, false, previousMapping);}); \
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
    if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QTimer::singleShot(0, this, [this, previousMapping] {connectClicked(true, QString(), false, false, false, false, previousMapping);}); \
    else if(m_autoUpdate == QStringLiteral("release")) QTimer::singleShot(0, this, [this, previousMapping] {connectClicked(true, QString(), m_autoErase, false, false, false, previousMapping);}); \
    else if(m_autoUpdate == QStringLiteral("developement")) QTimer::singleShot(0, this, [this, previousMapping] {connectClicked(true, QString(), m_autoErase, false, true, false, previousMapping);}); \
    else if(QFileInfo(m_autoUpdate).isFile()) QTimer::singleShot(0, this, [this, previousMapping] {connectClicked(true, m_autoUpdate, (m_autoErase || m_autoUpdate.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive)), false, false, false, previousMapping);}); \
    else if(m_autoErase) QTimer::singleShot(0, this, [this, previousMapping] {connectClicked(true, QString(), true, true, false, false, previousMapping);}); \
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

bool validPort(const QJsonDocument &settings, const QString &serialNumberFilter, const MyQSerialPortInfo &port)
{
    for (const QJsonValue &value : settings.object().value(QStringLiteral("boards")).toArray())
    {
        QJsonObject object = value.toObject();
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
            return serialNumbersInverseFilters.isEmpty() || (!serialNumbersInverseFilters.contains(port.serialNumber()));
        }
    }

    return false;
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

    dfuDevices = picotoolGetDevices() + imxGetAllDevices(settings);

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

            if(vidpidMatch || altvidpidMatch)
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

void OpenMVPlugin::connectClicked(bool forceBootloader, QString forceFirmwarePath, bool forceFlashFSErase, bool justEraseFlashFs, bool installTheLatestDevelopmentFirmware, bool waitForCamera, QString previousMapping)
{
    if(!m_working)
    {
        if(m_connect_disconnect)
        {
            disconnect(m_connect_disconnect);
        }

        if(m_connected)
        {
            m_connect_disconnect = connect(this, &OpenMVPlugin::disconnectDone, this, [this, forceBootloader, forceFirmwarePath, forceFlashFSErase, justEraseFlashFs, installTheLatestDevelopmentFirmware, waitForCamera, previousMapping] {
                QTimer::singleShot(0, this, [this, forceBootloader, forceFirmwarePath, forceFlashFSErase, justEraseFlashFs, installTheLatestDevelopmentFirmware, waitForCamera, previousMapping] {connectClicked(forceBootloader, forceFirmwarePath, forceFlashFSErase, justEraseFlashFs, installTheLatestDevelopmentFirmware, waitForCamera, previousMapping);});
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
                    combo->addItem(Tr::tr("Just erase the interal file system"));
                    combo->setCurrentIndex(settings->value(LAST_DFU_ACTION, 0).toInt());
                    layout->addWidget(combo);
                    layout->addItem(new QSpacerItem(0, 6));

                    QHBoxLayout *layout2 = new QHBoxLayout;
                    layout2->setContentsMargins(0, 0, 0, 0);
                    QWidget *widget = new QWidget;
                    widget->setLayout(layout2);

                    QCheckBox *checkBox = new QCheckBox(Tr::tr("Erase internal file system"));
                    checkBox->setChecked(settings->value(LAST_DFU_FLASH_FS_ERASE_STATE, false).toBool());
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
                QMap<QString, QString> vidpidMappings;

                for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                {
                    QString a = value.toObject().value(QStringLiteral("boardDisplayName")).toString();
                    mappings.insert(a, value.toObject().value(QStringLiteral("boardFirmwareFolder")).toString());
                    vidpidMappings.insert(a, value.toObject().value(QStringLiteral("bootloaderVidPid")).toString());

                    if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("internal"))
                    {
                        QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                        int eraseSectorStart = bootloaderSettings.value(QStringLiteral("eraseSectorStart")).toString().toInt();
                        int eraseSectorEnd = bootloaderSettings.value(QStringLiteral("eraseSectorEnd")).toString().toInt();
                        int eraseAllSectorStart = bootloaderSettings.value(QStringLiteral("eraseAllSectorStart")).toString().toInt();
                        int eraseAllSectorEnd = bootloaderSettings.value(QStringLiteral("eraseAllSectorEnd")).toString().toInt();
                        eraseMappings.insert(a, QPair<int, int>(eraseSectorStart, eraseSectorEnd));
                        eraseAllMappings.insert(a, QPair<int, int>(eraseAllSectorStart, eraseAllSectorEnd));
                    }
                    else
                    {
                        eraseMappings.insert(a, QPair<int, int>(0, 0));
                        eraseAllMappings.insert(a, QPair<int, int>(0, 0));
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

                                for(const QString &device : dfuDevices)
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
                                settings->setValue(LAST_BOARD_TYPE_STATE, temp);

                                int answer = ((forceBootloader && previousMappingSet) ? (forceFlashFSErase ? QMessageBox::Yes : QMessageBox::No) :
                                    (dfuDeviceResetToRelease ? (dfuDeviceEraseFlash ? QMessageBox::Yes : QMessageBox::No) :
                                        QMessageBox::question(Core::ICore::dialogParent(),
                                                              Tr::tr("Connect"),
                                                              Tr::tr("Erase the internal file system?"),
                                                              QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No)));

                                if((answer == QMessageBox::Yes) || (answer == QMessageBox::No))
                                {
                                    previousMapping = temp;
                                    originalFirmwareFolder = mappings.value(temp);
                                    firmwarePath = Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(originalFirmwareFolder).pathAppended(QStringLiteral("firmware.bin")).toString();
                                    if (forceBootloader && (!forceFirmwarePath.isEmpty())) firmwarePath = forceFirmwarePath;
                                    forceBootloader = true;
                                    forceFlashFSErase = answer == QMessageBox::Yes;
                                    forceBootloaderBricked = true;
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
        else if(stringList.size() == 1)
        {
            selectedPort = stringList.first();
            settings->setValue(LAST_SERIAL_PORT_STATE, selectedPort);
        }
        else
        {
            int index = stringList.indexOf(settings->value(LAST_SERIAL_PORT_STATE).toString());

            bool ok;
            QString temp = QInputDialog::getItem(Core::ICore::dialogParent(),
                Tr::tr("Connect"), Tr::tr("Please select a serial port"),
                stringList, (index != -1) ? index : 0, false, &ok,
                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

            if(ok)
            {
                selectedPort = temp;
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

        bool isIMX = false;
        bool isOpenMVDfu = false;
        bool isArduino = false;
        bool isTouchToReset = false;
        bool isPortenta = false;
        bool isNiclav = false;
        bool isGiga = false;
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
                                                                              ((arduinoPort.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID) ||
                                                                              ((arduinoPort.productIdentifier() & ARDUINOCAM_PID_MASK) == ARDUINOCAM_GH7_PID))) ||
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
            isGiga = arduinoPort.hasVendorIdentifier() &&
                    arduinoPort.hasProductIdentifier() &&
                   (arduinoPort.vendorIdentifier() == ARDUINOCAM_VID) && (arduinoPort.productIdentifier() == GIGA_APP_PID);
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

            isOpenMVDfu = dfuVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isIMX = imxVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
            isArduino = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_GH7_PID))) ||
                        ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));
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
            isArduino = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID) ||
                                                                                 ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_GH7_PID))) ||
                        ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));
            isPortenta = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == PORTENTA_LDR_PID);
            isNiclav = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == NICLA_LDR_PID);
            isGiga = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (vidpid.at(1).toInt(nullptr, 16) == GIGA_LDR_PID);
            isNRF = (vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == NRF_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == NRF_LDR_PID));
            isRPIPico = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && ((vidpid.at(1).toInt(nullptr, 16) == RPI_OLD_PID) || (vidpid.at(1).toInt(nullptr, 16) == RPI_LDR_PID))) ||
                        ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));

            if(isOpenMVDfu || isIMX || isArduino)
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

        if (dfuNoDialogs && (!isOpenMVDfu) && (!isIMX) && (!isArduino)) {
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
                    QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified();

                    for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        if(temp == value.toObject().value(QStringLiteral("boardArchString")).toString())
                        {
                            if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu"))
                            {
                                isOpenMVDfu = true;
                                break;
                            }

                            if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("imx"))
                            {
                                isIMX = true;
                                break;
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
                        QMap<QString, QString> mappings;
                        QMap<QString, QString> mappingsHumanReadable;
                        QMap<QString, QPair<int, int> > eraseMappings;
                        QMap<QString, QPair<int, int> > eraseAllMappings;
                        QMap<QString, QString> vidpidMappings;

                        for (const QJsonValue &value : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                        {
                            QString a = value.toObject().value(QStringLiteral("boardArchString")).toString();
                            mappings.insert(a, value.toObject().value(QStringLiteral("boardFirmwareFolder")).toString());
                            mappingsHumanReadable.insert(value.toObject().value(QStringLiteral("boardDisplayName")).toString(), a);
                            vidpidMappings.insert(a, value.toObject().value(QStringLiteral("bootloaderVidPid")).toString());

                            if (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("internal"))
                            {
                                QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();
                                int eraseSectorStart = bootloaderSettings.value(QStringLiteral("eraseSectorStart")).toString().toInt();
                                int eraseSectorEnd = bootloaderSettings.value(QStringLiteral("eraseSectorEnd")).toString().toInt();
                                int eraseAllSectorStart = bootloaderSettings.value(QStringLiteral("eraseAllSectorStart")).toString().toInt();
                                int eraseAllSectorEnd = bootloaderSettings.value(QStringLiteral("eraseAllSectorEnd")).toString().toInt();
                                eraseMappings.insert(a, QPair<int, int>(eraseSectorStart, eraseSectorEnd));
                                eraseAllMappings.insert(a, QPair<int, int>(eraseAllSectorStart, eraseAllSectorEnd));
                            }
                            else
                            {
                                eraseMappings.insert(a, QPair<int, int>(0, 0));
                                eraseAllMappings.insert(a, QPair<int, int>(0, 0));
                            }
                        }

                        QString temp = QString(arch2).remove(QRegularExpression(QStringLiteral("\\[(.+?):(.+?)\\]"))).simplified();

                        if(!mappings.contains(temp))
                        {
                            int index = mappings.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE).toString());

                            bool ok = mappings.size() == 1;
                            temp = (mappings.size() == 1) ? mappingsHumanReadable.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),
                                Tr::tr("Connect"), Tr::tr("Please select the board type"),
                                mappingsHumanReadable.keys(), (index != -1) ? index : 0, false, &ok,
                                Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                            temp = mappingsHumanReadable.value(temp); // Get mappings key.

                            if(ok)
                            {
                                settings->setValue(LAST_BOARD_TYPE_STATE, temp);
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
                            if (forceBootloader && (!forceFirmwarePath.isEmpty())) firmwarePath = forceFirmwarePath;
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
                        isOpenMVDfu = dfuVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
                        isIMX = imxVidPidList(m_firmwareSettings).contains(QPair<int , int>(vidpid.at(0).toInt(nullptr, 16), vidpid.at(1).toInt(nullptr, 16)));
                        isArduino = ((vidpid.at(0).toInt(nullptr, 16) == ARDUINOCAM_VID) && (((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_PH7_PID) ||
                                                                                             ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NRF_PID) ||
                                                                                             ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_RPI_PID) ||
                                                                                             ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_NCL_PID) ||
                                                                                             ((vidpid.at(1).toInt(nullptr, 16) & ARDUINOCAM_PID_MASK) == ARDUINOCAM_GH7_PID))) ||
                                   ((vidpid.at(0).toInt(nullptr, 16) == RPI2040_VID) && (vidpid.at(1).toInt(nullptr, 16) == RPI2040_PID));
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

                if((!isOpenMVDfu) && (!isIMX) && (!isArduino) && firmwarePath.endsWith(QStringLiteral(".dfu"), Qt::CaseInsensitive))
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

            while((!isOpenMVDfu) && (!isIMX) && (!isArduino) && (justEraseFlashFs || firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive)))
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
                                    ? QString(QStringLiteral("%1%2")).arg(previousMappingSet ? Tr::tr("Reconnect your OpenMV Cam...") : Tr::tr("Disconnect your OpenMV Cam and then reconnect it...")).arg((previousMappingSet || justEraseFlashFs) ? QString() : Tr::tr("\n\nHit cancel to skip to DFU reprogramming."))
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

                                if((!previousMappingSet) && (!justEraseFlashFs) && forceFirmwarePath.isEmpty() && QMessageBox::question(Core::ICore::dialogParent(),
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

            if(isOpenMVDfu)
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

                    m_iodevice->sysReset(false);
                    m_iodevice->close();

                    loop.exec();

                    QElapsedTimer elaspedTimer;
                    elaspedTimer.start();

                    while(!elaspedTimer.hasExpired(1000))
                    {
                        QApplication::processEvents();
                    }
                }

                // DFU //////////////////////////////////////////////////////////

                QString selectedDfuDeviceVidPid = selectedDfuDevice.isEmpty() ? QString() : selectedDfuDevice.split(QStringLiteral(",")).first();
                QString selectedDfuDeviceSerialNumber = selectedDfuDevice.isEmpty() ? QString() : selectedDfuDevice.split(QStringLiteral(",")).last();

                QString boardTypeToDfuDeviceVidPid;
                QStringList eraseCommands, programCommandsCmd, programCommandsPath;

                if(selectedDfuDevice.isEmpty())
                {
                    bool foundMatch = false;

                    for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        QJsonObject obj = val.toObject();

                        if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("openmv_dfu"))
                        {
                            QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();

                            if(m_boardType == obj.value(QStringLiteral("boardType")).toString())
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
                                    QJsonObject obj2 = command.toObject();
                                    programCommandsCmd.append(obj2.value(QStringLiteral("cmd")).toString());
                                    programCommandsPath.append(obj2.value(QStringLiteral("path")).toString());
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
                }
                else
                {
                    bool foundMatch = false;

                    for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        QJsonObject obj = val.toObject();

                        if((selectedDfuDeviceVidPid == obj.value(QStringLiteral("bootloaderVidPid")).toString())
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
                                QJsonObject obj2 = command.toObject();
                                programCommandsCmd.append(obj2.value(QStringLiteral("cmd")).toString());
                                programCommandsPath.append(obj2.value(QStringLiteral("path")).toString());
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
                            Tr::tr("No DFU settings for the selected device!") + QString(QStringLiteral("\n\nVID: %1, PID: %2")).arg(dfuDeviceVidPidList.first().toInt(nullptr, 16)).arg(dfuDeviceVidPidList.last().toInt(nullptr, 16)));

                        CONNECT_END();
                    }
                }

                QString dfuDeviceVidPid = selectedDfuDevice.isEmpty() ? boardTypeToDfuDeviceVidPid : selectedDfuDeviceVidPid;
                QString dfuDeviceSerial = QString(QStringLiteral(" -S %1")).arg(selectedDfuDevice.isEmpty()
                    ? QStringLiteral("0123456789ABCDEF")
                    : selectedDfuDeviceSerialNumber);

                if(dfuDeviceSerial == QStringLiteral(" -S NULL"))
                {
                    dfuDeviceSerial = QString();
                }

                // Erase Flash //////////////////////////////////////

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

                // Program Flash //////////////////////////////////////

                QString command;
                Utils::Process process;

                for(int i = 0, j = programCommandsCmd.size(); i < j; i++)
                {
                    downloadFirmware(Tr::tr("Flashing Firmware"), command, process,
                                     Core::ICore::userResourcePath(QStringLiteral("firmware")).pathAppended(programCommandsPath.at(i)).toString(),
                                     dfuDeviceVidPid, programCommandsCmd.at(i) +
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

            if(isIMX)
            {
                QJsonObject outObj;

                if(originalFirmwareFolder.isEmpty())
                {
                    QList<QPair<int, int> > vidpidlist = imxVidPidList(m_firmwareSettings, false, true);
                    QMap<QString, QString> mappings;

                    for (const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                    {
                        QJsonObject obj = val.toObject();

                        if(obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("imx"))
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
                        int index = mappings.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE).toString());
                        bool ok = mappings.size() == 1;
                        QString temp = (mappings.size() == 1) ? mappings.firstKey() : QInputDialog::getItem(Core::ICore::dialogParent(),
                            Tr::tr("Connect"), Tr::tr("Please select the board type"),
                            mappings.keys(), (index != -1) ? index : 0, false, &ok,
                            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(ok)
                        {
                            settings->setValue(LAST_BOARD_TYPE_STATE, temp);
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
                            QString secureBootloaderPath = Core::ICore::userResourcePath(QStringLiteral("firmware")).
                                    pathAppended(originalFirmwareFolder).
                                    pathAppended(bootloaderSettings.value(QStringLiteral("sdphost_flash_loader_path")).toString()).toString();
                            QString bootloaderPath = Core::ICore::userResourcePath(QStringLiteral("firmware")).
                                    pathAppended(originalFirmwareFolder).
                                    pathAppended(bootloaderSettings.value(QStringLiteral("blhost_secure_bootloader_path")).toString()).toString();
                            outObj = bootloaderSettings;
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
                                    if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
                                        Tr::tr("Connect"),
                                        Tr::tr("Firmware update complete!\n\n") +
                                        Tr::tr("Disconnect your OpenMV Cam from your computer, remove the jumper wire between the SBL and 3.3V pins, and then reconnect your OpenMV Cam to your computer.\n\n") +
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
                            if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
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
                    if(imxVidPidList(m_firmwareSettings, true, false).contains(entry))
                    {
                        if(imxDownloadBootloaderAndFirmware(outObj, forceFlashFSErase, justEraseFlashFs))
                        {
                            if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
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
                    else if(imxVidPidList(m_firmwareSettings, false, true).contains(entry))
                    {
                        if(imxDownloadFirmware(outObj, forceFlashFSErase, justEraseFlashFs))
                        {
                            if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
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

                if(isPortenta || isNiclav || isGiga)
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

                    QString boardTypeToDfuDeviceVidPid;
                    QStringList eraseCommands, extraProgramAddrCommands, extraProgramPathCommands;
                    QString binProgramCommand, dfuProgramCommand;

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

                                    binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();
                                    dfuProgramCommand = bootloaderSettings.value(QStringLiteral("dfuProgramCommand")).toString();
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

                            if((selectedDfuDeviceVidPid == obj.value(QStringLiteral("bootloaderVidPid")).toString())
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

                                binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();
                                dfuProgramCommand = bootloaderSettings.value(QStringLiteral("dfuProgramCommand")).toString();
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
                        // Extra Program Flash ////////////////////////////////
                        {
                            QString command;
                            Utils::Process process;

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
                        Utils::Process process;
                        downloadFirmware(Tr::tr("Flashing Firmware"), command, process, QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)), dfuDeviceVidPid, (firmwarePath.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive) ? binProgramCommand : dfuProgramCommand) + dfuDeviceSerial);

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

                        selectedDfuDeviceVidPid = QString(QStringLiteral("%1:%2").arg(ARDUINOCAM_VID, 4, 16, QLatin1Char('0')).arg(NRF_LDR_PID, 4, 16, QLatin1Char('0')));

                        QElapsedTimer elaspedTimer;
                        elaspedTimer.start();
                        dfuDevicePort = QString();

                        while(dfuDevicePort.isEmpty() && (!elaspedTimer.hasExpired(RESET_TO_DFU_SEARCH_TIME)))
                        {
                            QApplication::processEvents();

                            for(QSerialPortInfo raw_port : QSerialPortInfo::availablePorts())
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

                    QString boardTypeToDfuDeviceVidPid;
                    QString binProgramCommand;

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

                            if((selectedDfuDeviceVidPid == obj.value(QStringLiteral("bootloaderVidPid")).toString())
                            && (obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("bossac")))
                            {
                                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();
                                binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();
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
                        if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
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

                    QStringList eraseCommands;
                    QString binProgramCommand;

                    if(selectedDfuDevice.isEmpty())
                    {
                        bool foundMatch = false;

                        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                        {
                            QJsonObject obj = val.toObject();

                            if((m_boardType == obj.value(QStringLiteral("boardType")).toString())
                            && (obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("picotool")))
                            {
                                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();
                                QJsonArray eraseCommandsArray = bootloaderSettings.value(QStringLiteral("eraseCommands")).toArray();

                                for(const QJsonValue &command : eraseCommandsArray)
                                {
                                    eraseCommands.append(command.toString());
                                }

                                binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();
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

                        for(const QJsonValue &val : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray())
                        {
                            QJsonObject obj = val.toObject();

                            if((selectedDfuDeviceVidPid == obj.value(QStringLiteral("bootloaderVidPid")).toString())
                            && (obj.value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("picotool")))
                            {
                                QJsonObject bootloaderSettings = obj.value(QStringLiteral("bootloaderSettings")).toObject();
                                QJsonArray eraseCommandsArray = bootloaderSettings.value(QStringLiteral("eraseCommands")).toArray();

                                for(const QJsonValue &command : eraseCommandsArray)
                                {
                                    eraseCommands.append(command.toString());
                                }

                                binProgramCommand = bootloaderSettings.value(QStringLiteral("binProgramCommand")).toString();
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
                        QString command;
                        Utils::Process process;
                        picotoolDownloadFirmware(Tr::tr("Flashing Firmware"), command, process, QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)), binProgramCommand);

                        if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
                        {
                            if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QMessageBox::information(Core::ICore::dialogParent(),
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
                        Utils::Process process;
                        downloadFirmware(Tr::tr("Flashing Bootloader"), command, process, QDir::toNativeSeparators(QDir::cleanPath(firmwarePath)), originalDfuVidPid, QStringLiteral("-a 0 -s :leave"));

                        if((process.result() == Utils::ProcessResult::FinishedWithSuccess) || (command.contains(QStringLiteral("dfu-util")) && (process.result() == Utils::ProcessResult::FinishedWithError)))
                        {
                            if(repairingBootloader)
                            {
                                QMessageBox::information(Core::ICore::dialogParent(),
                                    Tr::tr("Connect"),
                                    Tr::tr("DFU bootloader reset complete!\n\n") +
                                    Tr::tr("Disconnect your OpenMV Cam from your computer and remove the jumper wire between the BOOT and RST pins.\n\n") +
                                    Tr::tr("Leave your OpenMV Cam unconnected until instructed to reconnect it."));

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
                        if (value.toObject().value(QStringLiteral("boardArchString")).toString() == temp)
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
                                if(QString::fromUtf8(data).contains(QStringLiteral("<p>No</p>")))
                                {
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

                        QNetworkRequest request = QNetworkRequest(QUrl(QString(QStringLiteral("https://upload.openmv.io/openmv-swd-ids-check.php?board=%L1&id=%L2")).arg(board).arg(id)));
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
                document->setPlainText(QString::fromUtf8(fixScriptForSensor(document->contents())));
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
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Firmware Update"),
            Tr::tr("Busy... please wait..."));
    }
}

} // namespace Internal
} // namespace OpenMV
