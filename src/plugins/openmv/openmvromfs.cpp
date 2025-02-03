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
#include "openmvromfs.h"

#define ROMFS_FILE_ALIGNMENT    (32)

namespace OpenMV {
namespace Internal {

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

OpenMVROMFSEditor::OpenMVROMFSEditor(QWidget *parent, const QString &path) : QTreeView(parent), m_model(new QFileSystemModel(this))
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
}

void OpenMVROMFSEditor::calculateFileSystemSize()
{
    VfsRomWriter writer(ROMFS_FILE_ALIGNMENT);
    createRomfs(&writer, m_model, m_model->index(m_model->rootPath()));
    emit fileSystemSize(QString(QStringLiteral("ROMFS Size: %1")).arg(humanReadableSize(writer.finalize().size())));
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

        if (QFile::copy(file, newFilePath))
        {
            setCurrentIndex(m_model->index(newFilePath));
            settings->setValue(LAST_ROMFS_DIALOG_OPEN_FILE_PATH, file);
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

void OpenMVROMFSEditor::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex index = indexAt(event->pos());

    if(index.isValid())
    {
        QMenu menu;
        connect(menu.addAction(Tr::tr("Add File")), &QAction::triggered, this, &OpenMVROMFSEditor::addFile);
        connect(menu.addAction(Tr::tr("New Folder")), &QAction::triggered, this, &OpenMVROMFSEditor::newFolder);
        connect(menu.addAction(Tr::tr("Delete")), &QAction::triggered, this, &OpenMVROMFSEditor::remove);
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

void OpenMVPlugin::romfsClicked()
{
    QDialog *dialog = new QDialog(Core::ICore::dialogParent(),
        Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    dialog->setWindowTitle(Tr::tr("Edit ROMFS"));
    dialog->setMinimumSize(QSize(320, 240));
    QVBoxLayout *layout = new QVBoxLayout(dialog);

    Utils::QtcSettings *settings = ExtensionSystem::PluginManager::settings();
    settings->beginGroup(SETTINGS_GROUP);

    QTemporaryDir tempDir;

    if(!tempDir.isValid())
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            Tr::tr("Edit ROMFS"),
            tempDir.errorString());
        return;
    }

    OpenMVROMFSEditor *romfsEditor = new OpenMVROMFSEditor(dialog, tempDir.path());
    layout->addWidget(romfsEditor);

    QLabel *romfsSize = new QLabel();
    romfsSize->setAlignment(Qt::AlignRight);
    connect(romfsEditor, &OpenMVROMFSEditor::fileSystemSize, romfsSize, &QLabel::setText);
    layout->addWidget(romfsSize);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *addFile = new QPushButton(Tr::tr("Add File"));
    box->addButton(addFile, QDialogButtonBox::ActionRole);
    QPushButton *newFolder = new QPushButton(Tr::tr("New Folder"));
    box->addButton(newFolder, QDialogButtonBox::ActionRole);
    QPushButton *remove = new QPushButton(Tr::tr("Delete"));
    box->addButton(remove, QDialogButtonBox::ActionRole);
    QPushButton *commit = new QPushButton(Tr::tr("Commit ROMFS"));
    box->addButton(commit, QDialogButtonBox::AcceptRole);
    connect(addFile, &QPushButton::clicked, romfsEditor, &OpenMVROMFSEditor::addFile);
    connect(newFolder, &QPushButton::clicked, romfsEditor, &OpenMVROMFSEditor::newFolder);
    connect(remove, &QPushButton::clicked, romfsEditor, &OpenMVROMFSEditor::remove);
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
    settings->endGroup();

    if (ok)
    {
        QTemporaryFile romfsFile;

        if (romfsFile.open())
        {
            VfsRomWriter writer(ROMFS_FILE_ALIGNMENT);
            createRomfs(&writer, romfsEditor->model(), romfsEditor->model()->index(romfsEditor->model()->rootPath()));
            romfsFile.write(writer.finalize());
            romfsFile.close();

            // QString file = QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Save ROMFS"));
            // QFile::remove(file);
            // QFile::copy(romfsFile.fileName(), file);

            // QString folder = QFileDialog::getExistingDirectory(Core::ICore::dialogParent(), Tr::tr("Extract ROMFS"));
            // QFile f(file);
            // f.open(QFile::ReadOnly);
            // VfsRomReader reader(f.readAll());
            // reader.unpack(folder);
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                romfsFile.errorString());
        }
    }

    delete dialog;
}

} // namespace Internal
} // namespace OpenMV
