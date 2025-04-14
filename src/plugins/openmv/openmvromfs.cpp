/* Copyright (C) 2023-2025 OpenMV, LLC.
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

#include "tools/romfs.h"
#include "tools/vela.h"
#include "openmvromfs.h"
#include "openmvmodelzoo.h"

#define ROMFS_FILE_ALIGNMENT    (32)

namespace OpenMV {
namespace Internal {

QJsonObject getROMFSConfig(const QString &title,
                           const QJsonObject &boardSettings,
                           Utils::QtcSettings *settings)
{
    QMap<QString, QJsonObject> mappings;

    for (const QJsonValue &val : boardSettings.value(QStringLiteral("romfsConfig")).toArray())
    {
        mappings.insert(val.toObject().value(QStringLiteral("name")).toString(), val.toObject());
    }

    int index = mappings.keys().indexOf(settings->value(LAST_BOARD_TYPE_STATE_ROMFS).toString());

    bool ok = mappings.size() == 1;
    QString temp = (mappings.size() == 1) ? mappings.keys().first() : QInputDialog::getItem(Core::ICore::dialogParent(),
        title, Tr::tr("Please select the target"),
        mappings.keys(), (index != -1) ? index : 0, false, &ok,
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

    if(ok)
    {
        settings->setValue(LAST_BOARD_TYPE_STATE_ROMFS, temp);
        return mappings.value(temp);
    }

    return QJsonObject();
}

QString convertModel(const QJsonObject &boardSettings,
                     const QString &model,
                     Utils::QtcSettings *settings)
{
    if (model.endsWith(".tflite"))
    {
        if (boardSettings.contains(QStringLiteral("romfsConfig")))
        {
            QJsonObject romfsConfig = boardSettings.value(QStringLiteral("romfsConfig")).toObject();

            if (romfsConfig.contains(QStringLiteral("npuAcceleratorConfig")))
            {
                QJsonObject npuAcceleratorConfig = romfsConfig.value(QStringLiteral("npuAcceleratorConfig")).toObject();

                if (npuAcceleratorConfig.value(QStringLiteral("type")).toString() == "vela")
                {
                    return velaCompile(model, npuAcceleratorConfig, settings);
                }
            }
        }
    }

    return model;
}

static void createRomfs(VfsRomWriter *writer, const QFileSystemModel *model, const QModelIndex &index) {
    for (int i = 0; i < model->rowCount(index); i++)
    {
        QModelIndex childIndex = model->index(i, 0, index);

        if (model->isDir(childIndex))
        {
            writer->opendir(model->fileName(childIndex));
            createRomfs(writer, model, childIndex);
            writer->closedir();
        }
        else
        {
            QFile file(model->filePath(childIndex));

            if (file.open(QIODevice::ReadOnly))
            {
                writer->mkfile(model->fileName(childIndex), file.readAll());
            }
        }
    }
}

static QString humanReadableSize(quint64 bytes) {
    const QStringList units = {"B", "KB", "MB", "GB", "TB"};
    int unitIndex = 0;
    double size = bytes;

    while ((size >= 1024) && (unitIndex < units.size() - 1))
    {
        size /= 1024;
        ++unitIndex;
    }

    return QString("%1 %2").arg(QString::number(size, 'f', 2)).arg(units[unitIndex]);
}

OpenMVROMFSEditor::OpenMVROMFSEditor(QWidget *parent, const QString &path, const QJsonObject &boardSettings) : QTreeView(parent), m_model(new QFileSystemModel(this))
{
    setContextMenuPolicy(Qt::DefaultContextMenu);
    m_model->setReadOnly(false);
    m_model->setRootPath(path);
    setModel(m_model);
    setRootIndex(m_model->index(path));
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    setColumnHidden(2, true); // Type
    setColumnHidden(3, true); // DateModified

    m_boardSettings = boardSettings;

#ifndef Q_OS_MAC
    m_styleSheet = QStringLiteral( // https://doc.qt.io/qt-5/stylesheet-examples.html#customizing-qtreeview
    "QTreeView::branch:has-children:!has-siblings:closed,QTreeView::branch:closed:has-children:has-siblings{border-image:none;image:url(:/core/images/branch-closed-%1.png);}"
    "QTreeView::branch:open:has-children:!has-siblings,QTreeView::branch:open:has-children:has-siblings{border-image:none;image:url(:/core/images/branch-open-%1.png);}"
    ).arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("dark") : QStringLiteral("light"));
#endif

    m_highDPIStyleSheet = QString(m_styleSheet).replace(QStringLiteral(".png"), QStringLiteral("_2x.png"));
    m_devicePixelRatio = 0;

    calculateFileSystemSize();
    connect(m_model, &QFileSystemModel::directoryLoaded, this, &OpenMVROMFSEditor::calculateFileSystemSize);
    connect(m_model, &QFileSystemModel::dataChanged, this, &OpenMVROMFSEditor::calculateFileSystemSize);
    connect(m_model, &QFileSystemModel::directoryLoaded, this, [this, path] (){
        preloadDirectories(m_model->index(path));
    });
}

void OpenMVROMFSEditor::preloadDirectories(const QModelIndex &index)
{
    for (int row = 0; row < m_model->rowCount(index); row++)
    {
        QModelIndex child = m_model->index(row, 0, index);
        setExpanded(child, true);
        preloadDirectories(child);
    }
}

void OpenMVROMFSEditor::calculateFileSystemSize()
{
    VfsRomWriter writer(ROMFS_FILE_ALIGNMENT);
    createRomfs(&writer, m_model, m_model->index(m_model->rootPath()));

    size_t sizeLimit = SIZE_MAX;

    if (m_boardSettings.contains(QStringLiteral("romfsConfig")))
    {
        QJsonObject romfsConfig = m_boardSettings.value(QStringLiteral("romfsConfig")).toObject();
        sizeLimit = romfsConfig.value(QStringLiteral("size")).toInt();
    }

    size_t size = writer.finalize().size();

    if (size <= sizeLimit)
    {
        emit fileSystemSize(QString(QStringLiteral("ROMFS Size: %1 / %2")).arg(humanReadableSize(size)).arg(humanReadableSize(sizeLimit)));
        emit commitEnabled(true);
    }
    else
    {
        emit fileSystemSize(QString(QStringLiteral("<p style=\"color:%1\">ROMFS Size: %2 / %3</p>"))
                            .arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface)
                                ? QStringLiteral("lightcoral")
                                : QStringLiteral("coral"))
                            .arg(humanReadableSize(size))
                            .arg(humanReadableSize(sizeLimit)));
        emit commitEnabled(false);
    }
}

void OpenMVROMFSEditor::addModel()
{
    QModelIndex index = currentIndex();

    if (!index.isValid()) {
        index = m_model->index(m_model->rootPath());
    }

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    // already in the settings group

    OpenMVModelZooBrowser dialog(settings, this);

    if (dialog.exec() == QDialog::Accepted)
    {
        QString src = dialog.selectedModel();
        QString convertedSrc = convertModel(m_boardSettings, src, settings);

        if (convertedSrc.isEmpty())
        {
            return;
        }

        QString path = m_model->isDir(index) ? m_model->filePath(index) : QFileInfo(m_model->filePath(index)).path();
        QString newFilePath = path + QDir::separator() + QString::fromLatin1(toAscii(QFileInfo(src).fileName()));

        if (QFileInfo(newFilePath).exists())
        {
            if (QFileInfo(newFilePath).isDir())
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Edit ROMFS"),
                    Tr::tr("A folder with the same name already exists!"));
                return;
            }

            if (QMessageBox::question(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("File already exists! Overwrite?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No)
            == QMessageBox::Yes)
            {
                if (!QFile::remove(newFilePath))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Edit ROMFS"),
                        Tr::tr("Failed to remove file!"));
                    return;
                }
            }
            else
            {
                return;
            }
        }

        if (QFile::copy(convertedSrc, newFilePath))
        {
            setCurrentIndex(m_model->index(newFilePath));
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("Failed to copy file!"));
        }
    }
}

void OpenMVROMFSEditor::addFile()
{
    QModelIndex index = currentIndex();

    if (!index.isValid()) {
        index = m_model->index(m_model->rootPath());
    }

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    // already in the settings group

    QString file = QFileDialog::getOpenFileName(Core::ICore::dialogParent(), Tr::tr("Edit ROMFS"),
                                                settings->value(LAST_ROMFS_DIALOG_OPEN_FILE_PATH, QDir::homePath()).toString());

    if (!file.isEmpty())
    {
        QString convertedSrc = convertModel(m_boardSettings, file, settings);

        if (convertedSrc.isEmpty())
        {
            return;
        }

        QString path = m_model->isDir(index) ? m_model->filePath(index) : QFileInfo(m_model->filePath(index)).path();
        QString newFilePath = path + QDir::separator() + QString::fromLatin1(toAscii(QFileInfo(file).fileName()));

        if (QFileInfo(newFilePath).exists())
        {
            if (QFileInfo(newFilePath).isDir())
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Edit ROMFS"),
                    Tr::tr("A folder with the same name already exists!"));
                return;
            }

            if (QMessageBox::question(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("File already exists! Overwrite?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No)
            == QMessageBox::Yes)
            {
                if (!QFile::remove(newFilePath))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Edit ROMFS"),
                        Tr::tr("Failed to remove file!"));
                    return;
                }
            }
            else
            {
                return;
            }
        }

        if (QFile::copy(convertedSrc, newFilePath))
        {
            setCurrentIndex(m_model->index(newFilePath));
            settings->setValue(LAST_ROMFS_DIALOG_OPEN_FILE_PATH, QFileInfo(file).path());
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("Failed to copy file!"));
        }
    }
}

void OpenMVROMFSEditor::newFolder()
{
    QModelIndex index = currentIndex();

    if (!index.isValid()) {
        index = m_model->index(m_model->rootPath());
    }

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    // already in the settings group

    bool ok;
    QString name = QString::fromLatin1(toAscii(QInputDialog::getText(Core::ICore::dialogParent(),
        Tr::tr("Edit ROMFS"), Tr::tr("Folder Name"),
        QLineEdit::Normal, settings->value(LAST_ROMFS_DIALOG_NEW_FOLDER_NAME).toString(), &ok,
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint))));

    if (ok && (!name.isEmpty()))
    {
        QString path = m_model->isDir(index) ? m_model->filePath(index) : QFileInfo(m_model->filePath(index)).path();
        QString newFilePath = path + QDir::separator() + name;

        if (QDir().mkdir(newFilePath))
        {
            setCurrentIndex(m_model->index(newFilePath));
            settings->setValue(LAST_ROMFS_DIALOG_NEW_FOLDER_NAME, name);
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("Failed to create folder!"));
        }
    }
}

void OpenMVROMFSEditor::remove()
{
    QModelIndex index = currentIndex();

    if (!index.isValid()) {
        QMessageBox::information(Core::ICore::dialogParent(),
            Tr::tr("Edit ROMFS"),
            Tr::tr("No file or folder selected."));
        return;
    }

    if (QMessageBox::question(Core::ICore::dialogParent(),
        Tr::tr("Edit ROMFS"),
        Tr::tr("Are you sure you want to permanetly delete \"%L1\"?").arg(m_model->fileName(index)),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No)
    == QMessageBox::Yes)
    {
        if (!m_model->remove(index))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("Failed to remove file or folder!"));
            return;
        }
    }
}

void OpenMVROMFSEditor::extractFile()
{
    QModelIndex index = currentIndex();

    if (!index.isValid()) {
        QMessageBox::information(Core::ICore::dialogParent(),
            Tr::tr("Edit ROMFS"),
            Tr::tr("No file or folder selected."));
        return;
    }

    if (m_model->isDir(index))
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Edit ROMFS"),
            Tr::tr("Cannot save a folder!"));
        return;
    }

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    // already in the settings group

    QString path = QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Extract File"),
        settings->value(LAST_ROMFS_DIALOG_SAVE_AS_PATH, QDir::homePath()).toString() + QDir::separator() + m_model->fileName(index));

    if(!path.isEmpty())
    {
        QFile file(m_model->filePath(index));

        if (file.copy(path))
        {
            settings->setValue(LAST_ROMFS_DIALOG_SAVE_AS_PATH, QFileInfo(path).path());
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Extract File"),
                file.errorString());
        }
    }
}

void OpenMVROMFSEditor::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex index = indexAt(event->pos());

    if(index.isValid())
    {
        QMenu menu;
        connect(menu.addAction(Tr::tr("Add File")), &QAction::triggered, this, &OpenMVROMFSEditor::addFile);
        connect(menu.addAction(Tr::tr("Add Model")), &QAction::triggered, this, &OpenMVROMFSEditor::addModel);
        connect(menu.addAction(Tr::tr("New Folder")), &QAction::triggered, this, &OpenMVROMFSEditor::newFolder);
        connect(menu.addAction(Tr::tr("Delete")), &QAction::triggered, this, &OpenMVROMFSEditor::remove);
        connect(menu.addAction(Tr::tr("Extract File")), &QAction::triggered, this, &OpenMVROMFSEditor::extractFile);
        menu.exec(event->globalPos());
    }

    QTreeView::contextMenuEvent(event);
}

void OpenMVROMFSEditor::keyPressEvent(QKeyEvent *event)
{
    switch(event->key())
    {
        case Qt::Key_Delete:
        {
            OpenMVROMFSEditor::remove();
            break;
        }
        default:
        {
            break;
        }
    }

    QTreeView::keyPressEvent(event);
}

// We have to do this because Qt does not update the icons when switching between
// a non-high dpi screen and a high-dpi screen.
void OpenMVROMFSEditor::paintEvent(QPaintEvent *event)
{
    qreal ratio = devicePixelRatioF();
    if (!qFuzzyCompare(ratio, m_devicePixelRatio))
    {
        m_devicePixelRatio = ratio;
        setStyleSheet(qFuzzyCompare(1.0, ratio) ? m_styleSheet : m_highDPIStyleSheet); // reload icons
    }

    QTreeView::paintEvent(event);
}

void OpenMVPlugin::editRomfsClicked(bool fromConnect, bool newRomfs)
{
    if (m_working)
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Edit ROMFS"),
            Tr::tr("Busy... please wait..."));

        return;
    }

    QTemporaryDir tempDir;

    if(!tempDir.isValid())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Edit ROMFS"),
            tempDir.errorString());

        return;
    }

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(SETTINGS_GROUP);

    QJsonObject boardSettings = getBoardSettings(Tr::tr("Edit ROMFS"), settings);

    if (boardSettings.isEmpty())
    {
        settings->endGroup();
        return;
    }

    int romfsIndex = 0;
    int romfsImageSize = 0;

    if (boardSettings.contains(QStringLiteral("romfsConfig")))
    {
        QJsonObject romfsConfigSettings = getROMFSConfig(Tr::tr("Edit ROMFS"), boardSettings, settings);

        if (romfsConfigSettings.isEmpty())
        {
            settings->endGroup();
            return;
        }

        boardSettings[QStringLiteral("romfsConfig")] = romfsConfigSettings;

        if (!romfsConfigSettings.value(QStringLiteral("size")).toInt())
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("ROMFS is not supported on this board!"));

            settings->endGroup();
            return;
        }

        romfsIndex = romfsConfigSettings.value(QStringLiteral("index")).toInt();
        romfsImageSize = romfsConfigSettings.value(QStringLiteral("size")).toInt();
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Edit ROMFS"),
            Tr::tr("ROMFS is not supported on this board!"));

        settings->endGroup();
        return;
    }

    bool wasConnected = m_connected;

    if (!newRomfs)
    {
        if (fromConnect)
        {
            QFile romfsFile(QDir::tempPath() + QDir::separator() + QString(QStringLiteral("romfs%1.img").arg(romfsIndex)));

            if ((!romfsFile.exists()) || romfsFile.remove())
            {
                settings->endGroup();

                QEventLoop loop;
                connect(this, &OpenMVPlugin::workingDone, &loop, &QEventLoop::quit);

                QString path = QFileInfo(romfsFile).filePath();

                QTimer::singleShot(0, this, [this, path] {
                    connectClicked(true, path, false, false, false, false, QString(), OPENMV_ROMFS_READ);
                });

                loop.exec();

                Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
                settings->beginGroup(SETTINGS_GROUP);

                if (romfsFile.open(QFile::ReadOnly))
                {
                    QByteArray data = romfsFile.readAll();
                    VfsRomReader reader(data);
                    bool ok = (data.size() >= romfsImageSize) && reader.unpack(tempDir.path());
                    romfsFile.close();

                    if (!ok)
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("Edit ROMFS"), Tr::tr("Failed to unpack ROMFS!"));

                        settings->endGroup();
                        if (wasConnected) connectClicked(false, QString(), false, false, false, true);
                        return;
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Edit ROMFS"),
                        romfsFile.errorString());

                    settings->endGroup();
                    if (wasConnected) connectClicked(false, QString(), false, false, false, true);
                    return;
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Edit ROMFS"),
                    romfsFile.errorString());

                settings->endGroup();
                return;
            }
        }
        else
        {
            QString path = QFileDialog::getOpenFileName(Core::ICore::dialogParent(), Tr::tr("OpenMV ROMFS"),
                settings->value(LAST_ROMFS_DIALOG_OPEN_PATH, QDir::homePath()).toString(),
                Tr::tr("ROMFS Images (*.img)"));

            if (!path.isEmpty())
            {
                QFile romfsFile(path);

                if (romfsFile.open(QIODevice::ReadOnly))
                {
                    VfsRomReader reader(romfsFile.readAll());
                    bool ok = reader.unpack(tempDir.path());
                    romfsFile.close();

                    settings->setValue(LAST_ROMFS_DIALOG_OPEN_PATH, path);

                    if (!ok)
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("Edit ROMFS"), Tr::tr("Failed to unpack ROMFS!"));

                        settings->endGroup();
                        return;
                    }
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("OpenMV ROMFS"),
                        romfsFile.errorString());

                    settings->endGroup();
                    return;
                }
            }
            else
            {
                settings->endGroup();
                return;
            }
        }
    }

    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
        Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog->setWindowTitle(Tr::tr("Edit ROMFS"));
    dialog->setMinimumSize(QSize(320, 240));
    QVBoxLayout *layout = new QVBoxLayout(dialog);

    OpenMVROMFSEditor *romfsEditor = new OpenMVROMFSEditor(dialog, tempDir.path(), boardSettings);
    layout->addWidget(romfsEditor);

    QLabel *romfsSize = new QLabel();
    romfsSize->setAlignment(Qt::AlignRight);
    connect(romfsEditor, &OpenMVROMFSEditor::fileSystemSize, romfsSize, &QLabel::setText);
    layout->addWidget(romfsSize);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *addFile = new QPushButton(Tr::tr("Add File"));
    box->addButton(addFile, QDialogButtonBox::ActionRole);
    QPushButton *addModel = new QPushButton(Tr::tr("Add Model"));
    box->addButton(addModel, QDialogButtonBox::ActionRole);
    QPushButton *newFolder = new QPushButton(Tr::tr("New Folder"));
    box->addButton(newFolder, QDialogButtonBox::ActionRole);
    QPushButton *remove = new QPushButton(Tr::tr("Delete"));
    box->addButton(remove, QDialogButtonBox::ActionRole);
    QPushButton *extractFile = new QPushButton(Tr::tr("Extract File"));
    box->addButton(extractFile, QDialogButtonBox::ActionRole);
    QPushButton *commit = new QPushButton(Tr::tr("Commit"));
    box->addButton(commit, QDialogButtonBox::AcceptRole);
    connect(romfsEditor, &OpenMVROMFSEditor::commitEnabled, commit, &QPushButton::setEnabled);
    connect(addFile, &QPushButton::clicked, romfsEditor, &OpenMVROMFSEditor::addFile);
    connect(addModel, &QPushButton::clicked, romfsEditor, &OpenMVROMFSEditor::addModel);
    connect(newFolder, &QPushButton::clicked, romfsEditor, &OpenMVROMFSEditor::newFolder);
    connect(remove, &QPushButton::clicked, romfsEditor, &OpenMVROMFSEditor::remove);
    connect(extractFile, &QPushButton::clicked, romfsEditor, &OpenMVROMFSEditor::extractFile);
    connect(box, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(box);

    if(settings->contains(LAST_ROMFS_DIALOG_GEOMETRY))
    {
        dialog->restoreGeometry(settings->value(LAST_ROMFS_DIALOG_GEOMETRY).toByteArray());
    }
    else
    {
        dialog->resize(640, 480);
    }

    bool ok = dialog->exec() == QDialog::Accepted;

    settings->setValue(LAST_ROMFS_DIALOG_GEOMETRY, dialog->saveGeometry());

    if (ok)
    {
        QDialog *dialog2 = new QDialog(Core::ICore::dialogParent(),
            Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
            (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
        dialog2->setWindowTitle(Tr::tr("Edit ROMFS"));
        QFormLayout *layout2 = new QFormLayout(dialog2);
        layout2->setVerticalSpacing(0);

        layout2->addWidget(new QLabel(Tr::tr("What would you like to do?")));
        layout2->addItem(new QSpacerItem(0, 6));

        QComboBox *combo2 = new QComboBox();
        combo2->addItem(Tr::tr("Commit ROMFS to OpenMV Cam"));
        combo2->addItem(Tr::tr("Save ROMFS to File"));
        combo2->setCurrentIndex(settings->value(LAST_ROMFS_DIALOG_ACTION, 0).toInt());
        layout2->addWidget(combo2);
        layout2->addItem(new QSpacerItem(0, 6));

        QHBoxLayout *layout3 = new QHBoxLayout;
        layout3->setContentsMargins(0, 0, 0, 0);
        QWidget *widget2 = new QWidget;
        widget2->setLayout(layout3);

        QDialogButtonBox *box2 = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout3->addSpacing(160);
        layout3->addWidget(box2);
        layout2->addRow(widget2);

        connect(box2, &QDialogButtonBox::accepted, dialog2, &QDialog::accept);
        connect(box2, &QDialogButtonBox::rejected, dialog2, &QDialog::reject);
        connect(combo2, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged), this, [this, dialog2] () {
            QTimer::singleShot(0, this, [dialog2] { dialog2->adjustSize(); });
        });

        if (dialog2->exec() == QDialog::Accepted)
        {
            settings->setValue(LAST_ROMFS_DIALOG_ACTION, combo2->currentIndex());

            if(combo2->currentIndex() == 0)
            {
                QFile romfsFile(QDir::tempPath() + QDir::separator() + QString(QStringLiteral("romfs%1.img").arg(romfsIndex)));

                if (romfsFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
                {
                    VfsRomWriter writer(ROMFS_FILE_ALIGNMENT);
                    createRomfs(&writer, romfsEditor->model(), romfsEditor->model()->index(romfsEditor->model()->rootPath()));
                    romfsFile.write(writer.finalize());
                    romfsFile.close();

                    settings->endGroup();

                    QEventLoop loop;
                    connect(this, &OpenMVPlugin::workingDone, &loop, &QEventLoop::quit);

                    QString path = QFileInfo(romfsFile).filePath();

                    QTimer::singleShot(0, this, [this, path] {
                        connectClicked(true, path, false, false, false, false, QString(), OPENMV_ROMFS_WRITE);
                    });

                    loop.exec();

                    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
                    settings->beginGroup(SETTINGS_GROUP);
                }
                else
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        Tr::tr("Edit ROMFS"),
                        romfsFile.errorString());
                }
            }
            else if(combo2->currentIndex() == 1)
            {
                QString path = QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Edit ROMFS"),
                    settings->value(LAST_ROMFS_DIALOG_SAVE_PATH, QDir::homePath()).toString(),
                    Tr::tr("ROMFS Images (*.img)"));

                if(!path.isEmpty())
                {
                    QFile romfsFile(path);

                    if (romfsFile.open(QIODevice::WriteOnly))
                    {
                        VfsRomWriter writer(ROMFS_FILE_ALIGNMENT);
                        createRomfs(&writer, romfsEditor->model(), romfsEditor->model()->index(romfsEditor->model()->rootPath()));
                        romfsFile.write(writer.finalize());
                        romfsFile.close();

                        settings->setValue(LAST_ROMFS_DIALOG_SAVE_PATH, path);
                    }
                    else
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            Tr::tr("Edit ROMFS"),
                            romfsFile.errorString());
                    }
                }
            }
        }

        delete dialog2;
    }

    settings->endGroup();
    delete dialog;

    if (wasConnected) connectClicked(false, QString(), false, false, false, true);
}

void OpenMVPlugin::resetRomfsClicked()
{
    if (m_working)
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Reset ROMFS"),
            Tr::tr("Busy... please wait..."));

        return;
    }

    if(QMessageBox::warning(Core::ICore::dialogParent(),
        Tr::tr("Reset ROMFS"),
        Tr::tr("Are you sure you want to reset your OpenMV Cam's ROM file system?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
    == QMessageBox::Yes)
    {
        Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
        settings->beginGroup(SETTINGS_GROUP);

        QJsonObject boardSettings = getBoardSettings(Tr::tr("Reset ROMFS"), settings);

        if (boardSettings.isEmpty())
        {
            settings->endGroup();
            return;
        }

        int romfsIndex = 0;

        if (boardSettings.contains(QStringLiteral("romfsConfig")))
        {
            QJsonObject romfsConfigSettings = getROMFSConfig(Tr::tr("Reset ROMFS"), boardSettings, settings);

            if (romfsConfigSettings.isEmpty())
            {
                settings->endGroup();
                return;
            }

            boardSettings[QStringLiteral("romfsConfig")] = romfsConfigSettings;

            if (!romfsConfigSettings.value(QStringLiteral("size")).toInt())
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Reset ROMFS"),
                    Tr::tr("ROMFS is not supported on this board!"));

                settings->endGroup();
                return;
            }

            romfsIndex = romfsConfigSettings.value(QStringLiteral("index")).toInt();
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Reset ROMFS"),
                Tr::tr("ROMFS is not supported on this board!"));

            settings->endGroup();
            return;
        }

        settings->endGroup();

        bool wasConnected = m_connected;

        QEventLoop loop;
        connect(this, &OpenMVPlugin::workingDone, &loop, &QEventLoop::quit);

        QString path = Core::ICore::userResourcePath(QStringLiteral("firmware")).
                pathAppended(boardSettings.value(QStringLiteral("boardFirmwareFolder")).toString()).
                pathAppended(QString(QStringLiteral("romfs%1.img").arg(romfsIndex))).toString();

        QTimer::singleShot(0, this, [this, path] {
            connectClicked(true, path, false, false, false, false, QString(), OPENMV_ROMFS_WRITE);
        });

        loop.exec();

        if (wasConnected) connectClicked(false, QString(), false, false, false, true);
    }
}

} // namespace Internal
} // namespace OpenMV
