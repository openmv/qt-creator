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
#include <coreplugin/icore.h>
#include "openmvtr.h"
#include "openmvpluginconnect.h"

#include "tools/mpy-cross.h"
#include "tools/romfs.h"
#include "tools/stedgeai.h"
#include "tools/vela.h"
#include "openmvromfs.h"

#include <QGuiApplication>
#include "openmvmodelzoo.h"

#include "mcp/omvui.h"
#include "mcp/omvmcpcontext.h"

namespace OpenMV {
namespace Internal {

// The converters (mpy-cross, vela, stedgeai) hand back a file inside a
// QTemporaryDir they deliberately leave behind for the caller; drop that
// directory once the file has been copied into the editor's tree. Only a
// directory strictly below the temp root is ever removed.
static void removeConversionDir(const QString &src, const QString &convertedSrc)
{
    if (convertedSrc.isEmpty() || (convertedSrc == src))
    {
        return;
    }

    // The converted file may sit a level or two below the QTemporaryDir
    // (stedgeai writes st_ai_output/...), so remove the first directory
    // under the temp root, which is the QTemporaryDir itself.
    const QString dir = QFileInfo(convertedSrc).absolutePath();
    const QString tempRoot = QDir(QDir::tempPath()).absolutePath();
    const QString prefix = tempRoot + QLatin1Char('/');

    if ((dir != tempRoot) && dir.startsWith(prefix) && (dir.length() > prefix.length()))
    {
        const QString first = dir.mid(prefix.length()).section(QLatin1Char('/'), 0, 0);

        if (!first.isEmpty())
        {
            QDir(prefix + first).removeRecursively();
        }
    }
}


QJsonObject getROMFSConfig(const QString &title,
                           const QJsonObject &boardSettings,
                           Utils::QtcSettings *settings)
{
    // QMap::keys() is sorted alphabetically, so keep the names in a separate
    // list to present the targets in the order romfsConfig defines them.
    QMap<QString, QJsonObject> mappings;
    QStringList names;

    for (const QJsonValue &val : boardSettings.value(QStringLiteral("romfsConfig")).toArray())
    {
        QString name = val.toObject().value(QStringLiteral("name")).toString();

        if (!mappings.contains(name))
        {
            names.append(name);
        }

        mappings.insert(name, val.toObject());
    }

    int index = names.indexOf(settings->value(SETTINGS_GROUP "/" LAST_BOARD_TYPE_STATE_ROMFS).toString());

    bool ok = names.size() == 1;
    QString temp = (names.size() == 1) ? names.first() : OMVUI::getItem(Core::ICore::dialogParent(),
        title, Tr::tr("Please select the target"),
        names, (index != -1) ? index : 0, false, &ok,
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint),
        Qt::ImhNone, OMV_UI_SITE("romfs.target"));

    if(ok)
    {
        settings->setValue(SETTINGS_GROUP "/" LAST_BOARD_TYPE_STATE_ROMFS, temp);
        return mappings.value(temp);
    }

    return QJsonObject();
}

QString convertScript(const QJsonObject &boardSettings,
                      const QString &script,
                      Utils::QtcSettings *settings)
{
    if (script.endsWith(".py"))
    {
        return mpyCompile(script, boardSettings.value(QStringLiteral("mpyConfig")).toObject(), settings);
    }

    return script;
}

QString convertModel(const QJsonObject &boardSettings,
                     const QString &model,
                     Utils::QtcSettings *settings)
{
    if (boardSettings.contains(QStringLiteral("romfsConfig")))
    {
        QJsonObject romfsConfig = boardSettings.value(QStringLiteral("romfsConfig")).toObject();

        if (romfsConfig.contains(QStringLiteral("npuAcceleratorConfig")))
        {
            QJsonObject npuAcceleratorConfig = romfsConfig.value(QStringLiteral("npuAcceleratorConfig")).toObject();
            QString npuAcceleratorType = npuAcceleratorConfig.value(QStringLiteral("type")).toString();

            if (npuAcceleratorType == "vela")
            {
                if (model.endsWith(".tflite") || model.endsWith(".lite"))
                {
                    QFile file(model);

                    if (file.open(QIODevice::ReadOnly))
                    {
                        bool needs_conversion = !file.readAll().contains(QStringLiteral("ethos-u").toUtf8());

                        file.close();

                        if (needs_conversion)
                        {
                            return velaCompile(model, npuAcceleratorConfig, settings);
                        }
                        else
                        {
                            OMVUI::information(Core::ICore::dialogParent(),
                                Tr::tr("Convert Model"),
                                Tr::tr("The model has already been converted for the Ethos-U NPU."
                                       "\n\n%1 will just copy the model as is.").arg(QGuiApplication::applicationDisplayName()));
                        }
                    }
                }
            }

            if (npuAcceleratorType == "stedgeai")
            {
                if (model.endsWith(".tflite") || model.endsWith(".lite") || model.endsWith(".onnx"))
                {
                    QFile file(model);

                    if (file.open(QIODevice::ReadOnly))
                    {
                        bool needs_conversion = !file.readAll().startsWith(QStringLiteral("NBIN").toUtf8());

                        file.close();

                        if (needs_conversion)
                        {
                            return stedgeaiCompile(model, npuAcceleratorConfig, settings);
                        }
                        else
                        {
                            OMVUI::information(Core::ICore::dialogParent(),
                                Tr::tr("Convert Model"),
                                Tr::tr("The model has already been converted for the Neural-ART NPU."
                                       "\n\n%1 will just copy the model as is.").arg(QGuiApplication::applicationDisplayName()));
                        }
                    }
                }
            }
        }
    }

    return model;
}

void OpenMVROMFSEditor::createRomfs(VfsRomWriter *writer, const QModelIndex &index)
{
    for (int i = 0; i < m_filter->rowCount(index); i++)
    {
        QModelIndex filterIndex = m_filter->index(i, 0, index);
        QModelIndex childIndex = m_filter->mapToSource(filterIndex);

        if (m_model->isDir(childIndex))
        {
            writer->opendir(m_model->fileName(childIndex));
            createRomfs(writer, filterIndex);
            writer->closedir();
        }
        else
        {
            QFile file(m_model->filePath(childIndex));

            if (file.open(QIODevice::ReadOnly))
            {
                writer->mkfile(m_model->fileName(childIndex), file.readAll());
            }
        }
    }
}

static QString humanReadableSize(quint64 bytes)
{
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
    setSortingEnabled(true);
    sortByColumn(0, Qt::AscendingOrder);
    m_filter = new OpenMVROMFSEditorFilter(this);
    m_filter->setSourceModel(m_model);
    m_filter->setDynamicSortFilter(true);
    m_model->setReadOnly(false);
    m_model->setRootPath(path);
    setModel(m_filter);
    setRootIndex(m_filter->mapFromSource(m_model->index(path)));
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    setColumnHidden(2, true); // Type
    setColumnHidden(3, true); // DateModified

    // Files dragged in from the desktop/file manager go through addFileAt()
    // (same conversion as Add File) into the folder they were dropped on.
    setAcceptDrops(true);
    setDropIndicatorShown(true);

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
        QTimer::singleShot(0, this, [this, path] {
            preloadDirectories(m_filter->mapFromSource(m_model->index(path)));
        });
    });
}

void OpenMVROMFSEditor::preloadDirectories(const QModelIndex &index)
{
    // Expanding rows on a hidden tree trips the macOS Cocoa a11y bridge
    // (NSRangeException) - e.g. when a queued directoryLoaded lands after
    // the dialog was closed. directoryLoaded re-fires as dirs keep loading
    // while visible, so skipped preloads are retried naturally.
    if (!isVisible())
        return;
    for (int row = 0; row < m_filter->rowCount(index); row++)
    {
        QModelIndex child = m_filter->index(row, 0, index);
        setExpanded(child, true);
        preloadDirectories(child);
    }
}

void OpenMVROMFSEditor::calculateFileSystemSize()
{
    size_t sizeLimit = SIZE_MAX;
    QJsonArray alignmentRules;

    if (m_boardSettings.contains(QStringLiteral("romfsConfig")))
    {
        QJsonObject romfsConfig = m_boardSettings.value(QStringLiteral("romfsConfig")).toObject();
        sizeLimit = romfsConfig.value(QStringLiteral("size")).toInt();
        alignmentRules = romfsConfig.value(QStringLiteral("alignmentRules")).toArray();
    }

    VfsRomWriter writer(alignmentRules);
    createRomfs(&writer, m_filter->mapFromSource(m_model->index(m_model->rootPath())));
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

void OpenMVROMFSEditor::viewEdit()
{
    QModelIndex index = m_filter->mapToSource(currentIndex());

    if (!index.isValid()) {
        index = m_model->index(m_model->rootPath());
    }

    QUrl url = QUrl::fromLocalFile(m_model->filePath(index));

    if(!QDesktopServices::openUrl(url))
    {
        OMVUI::critical(Core::ICore::dialogParent(),
                              Tr::tr("Edit ROMFS"),
                              Tr::tr("Failed to open: \"%L1\"").arg(url.toString()));
    }
}

void OpenMVROMFSEditor::addModel()
{
    QModelIndex index = m_filter->mapToSource(currentIndex());

    if (!index.isValid()) {
        index = m_model->index(m_model->rootPath());
    }

    Utils::QtcSettings *settings = Core::ICore::settings();

    OpenMVModelZooBrowser dialog(m_boardSettings, settings, this);

    if (OMVUI::exec(dialog) == QDialog::Accepted)
    {
        QString src = dialog.selectedModel();
        QString convertedSrc = convertModel(m_boardSettings, src, settings);

        if (convertedSrc.isEmpty())
        {
            return;
        }

        QString path = m_model->isDir(index) ? m_model->filePath(index) : QFileInfo(m_model->filePath(index)).path();
        QString newFilePath = path + QDir::separator() + QString::fromLatin1(toAscii(QFileInfo(src).baseName() + QChar('.') + QFileInfo(convertedSrc).suffix()));

        if (QFileInfo(newFilePath).exists())
        {
            if (QFileInfo(newFilePath).isDir())
            {
                OMVUI::critical(Core::ICore::dialogParent(),
                    Tr::tr("Edit ROMFS"),
                    Tr::tr("A folder with the same name already exists!"));
                return;
            }

            if (OMVUI::question(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("File already exists! Overwrite?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No)
            == QMessageBox::Yes)
            {
                if (!QFile::remove(newFilePath))
                {
                    OMVUI::critical(Core::ICore::dialogParent(),
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

        bool copied = QFile::copy(convertedSrc, newFilePath);
        removeConversionDir(src, convertedSrc);

        if (copied)
        {
            // Required to make sure creation timestamps are different on created files.
            QThread::msleep(10);

            const QModelIndex newIndex = m_filter->mapFromSource(m_model->index(newFilePath));
            if (newIndex.isValid())
                setCurrentIndex(newIndex);

            // Copy labels over too if they exist.
            QString labels = dialog.selectedModelLabels();

            if (!labels.isEmpty())
            {
                QFileInfo fileInfo(newFilePath);
                QString path = fileInfo.absolutePath() + QDir::separator() + fileInfo.baseName() + ".txt";

                if ((!QFile(path).exists()) || QFile::remove(path))
                {
                    QFile::copy(labels, path);

                    // Required to make sure creation timestamps are different on created files.
                    QThread::msleep(10);
                }
            }

            m_filter->invalidate();
        }
        else
        {
            OMVUI::critical(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("Failed to copy file!"));
        }
    }
}

void OpenMVROMFSEditor::addFile()
{
    QModelIndex index = m_filter->mapToSource(currentIndex());

    if (!index.isValid()) {
        index = m_model->index(m_model->rootPath());
    }

    Utils::QtcSettings *settings = Core::ICore::settings();

    QString file = OMVUI::getOpenFileName(Core::ICore::dialogParent(), Tr::tr("Edit ROMFS"),
                                                settings->value(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_OPEN_FILE_PATH,
                                                                QDir::homePath()).toString());

    if (!file.isEmpty())
    {
        addFileAt(file, index);
    }
}

// The work behind Add File, also used for files dropped onto the tree: scripts
// are compiled and models converted exactly as when picked through the dialog.
void OpenMVROMFSEditor::addFileAt(const QString &file, QModelIndex index)
{
    Utils::QtcSettings *settings = Core::ICore::settings();

    if (!index.isValid()) {
        index = m_model->index(m_model->rootPath());
    }

    {
        QString convertedSrc = convertModel(m_boardSettings, convertScript(m_boardSettings, file, settings), settings);

        if (convertedSrc.isEmpty())
        {
            return;
        }

        QString path = m_model->isDir(index) ? m_model->filePath(index) : QFileInfo(m_model->filePath(index)).path();
        QString newFilePath = path + QDir::separator() + QString::fromLatin1(toAscii(QFileInfo(file).baseName() + QChar('.') + QFileInfo(convertedSrc).suffix()));

        if (QFileInfo(newFilePath).exists())
        {
            if (QFileInfo(newFilePath).isDir())
            {
                OMVUI::critical(Core::ICore::dialogParent(),
                    Tr::tr("Edit ROMFS"),
                    Tr::tr("A folder with the same name already exists!"));
                return;
            }

            if (OMVUI::question(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("File already exists! Overwrite?"),
                QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No)
            == QMessageBox::Yes)
            {
                if (!QFile::remove(newFilePath))
                {
                    OMVUI::critical(Core::ICore::dialogParent(),
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

        bool copied = QFile::copy(convertedSrc, newFilePath);
        removeConversionDir(file, convertedSrc);

        if (copied)
        {
            const QModelIndex newIndex = m_filter->mapFromSource(m_model->index(newFilePath));
            if (newIndex.isValid())
                setCurrentIndex(newIndex);
            settings->setValue(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_OPEN_FILE_PATH, QFileInfo(file).path());

            m_filter->invalidate();
        }
        else
        {
            OMVUI::critical(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("Failed to copy file!"));
        }
    }
}

void OpenMVROMFSEditor::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        return;
    }

    QTreeView::dragEnterEvent(event);
}

void OpenMVROMFSEditor::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
        return;
    }

    QTreeView::dragMoveEvent(event);
}

void OpenMVROMFSEditor::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls())
    {
        QTreeView::dropEvent(event);
        return;
    }

    // Target: the folder under the cursor (or the folder containing the file
    // under the cursor); empty space means the ROMFS root.
    QModelIndex index = m_filter->mapToSource(indexAt(event->position().toPoint()));

    for (const QUrl &url : event->mimeData()->urls())
    {
        if (!url.isLocalFile())
        {
            continue;
        }

        QFileInfo info(url.toLocalFile());

        if (info.isDir())
        {
            OMVUI::critical(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("Dropping folders is not supported - drop the files inside them instead."));
            continue;
        }

        if (info.isFile())
        {
            addFileAt(info.filePath(), index);
        }
    }

    event->acceptProposedAction();
}

void OpenMVROMFSEditor::newFolder()
{
    QModelIndex index = m_filter->mapToSource(currentIndex());

    if (!index.isValid()) {
        index = m_model->index(m_model->rootPath());
    }

    Utils::QtcSettings *settings = Core::ICore::settings();

    bool ok;
    QString name = QString::fromLatin1(toAscii(OMVUI::getText(Core::ICore::dialogParent(),
        Tr::tr("Edit ROMFS"), Tr::tr("Folder Name"),
        QLineEdit::Normal, settings->value(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_NEW_FOLDER_NAME).toString(), &ok,
        Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
        (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint))));

    if (ok && (!name.isEmpty()))
    {
        QString path = m_model->isDir(index) ? m_model->filePath(index) : QFileInfo(m_model->filePath(index)).path();
        QString newFilePath = path + QDir::separator() + name;

        if (QDir().mkdir(newFilePath))
        {
            const QModelIndex newIndex = m_filter->mapFromSource(m_model->index(newFilePath));
            if (newIndex.isValid())
                setCurrentIndex(newIndex);
            settings->setValue(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_NEW_FOLDER_NAME, name);

            m_filter->invalidate();
        }
        else
        {
            OMVUI::critical(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("Failed to create folder!"));
        }
    }
}

void OpenMVROMFSEditor::remove()
{
    QModelIndex index = m_filter->mapToSource(currentIndex());

    if (!index.isValid()) {
        OMVUI::information(Core::ICore::dialogParent(),
            Tr::tr("Edit ROMFS"),
            Tr::tr("No file or folder selected."));
        return;
    }

    if (OMVUI::question(Core::ICore::dialogParent(),
        Tr::tr("Edit ROMFS"),
        Tr::tr("Are you sure you want to permanetly delete \"%L1\"?").arg(m_model->fileName(index)),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No)
    == QMessageBox::Yes)
    {
        if (!m_model->remove(index))
        {
            OMVUI::critical(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("Failed to remove file or folder!"));
            return;
        }

        m_filter->invalidate();
    }
}

void OpenMVROMFSEditor::extractFile()
{
    QModelIndex index = m_filter->mapToSource(currentIndex());

    if (!index.isValid()) {
        OMVUI::information(Core::ICore::dialogParent(),
            Tr::tr("Edit ROMFS"),
            Tr::tr("No file or folder selected."));
        return;
    }

    if (m_model->isDir(index))
    {
        OMVUI::critical(Core::ICore::dialogParent(),
            Tr::tr("Edit ROMFS"),
            Tr::tr("Cannot save a folder!"));
        return;
    }

    Utils::QtcSettings *settings = Core::ICore::settings();

    QString path = OMVUI::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Extract File"),
        settings->value(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_SAVE_AS_PATH, QDir::homePath()).toString()
            + QDir::separator() + m_model->fileName(index));

    if(!path.isEmpty())
    {
        QFile file(m_model->filePath(index));

        if (file.copy(path))
        {
            settings->setValue(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_SAVE_AS_PATH, QFileInfo(path).path());
        }
        else
        {
            OMVUI::critical(Core::ICore::dialogParent(),
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
        connect(menu.addAction(Tr::tr("View/Edit")), &QAction::triggered, this, &OpenMVROMFSEditor::viewEdit);
        menu.addSeparator();
        connect(menu.addAction(Tr::tr("Model Zoo")), &QAction::triggered, this, &OpenMVROMFSEditor::addModel);
        connect(menu.addAction(Tr::tr("Add File")), &QAction::triggered, this, &OpenMVROMFSEditor::addFile);
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

// Pack a directory tree into a ROMFS writer straight from the filesystem. This
// mirrors OpenMVROMFSEditor::createRomfs (opendir/recurse/closedir, mkfile) but
// without the editor's async QFileSystemModel -- used by the viewer-mode load-only
// path, which has no editor dialog to populate that model. The editor's proxy
// applies no row filtering, so this packs the same set of files.
static void packDirIntoRomfs(VfsRomWriter *writer, const QDir &dir)
{
    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo &entry : entries)
    {
        if (entry.isDir())
        {
            writer->opendir(entry.fileName());
            packDirIntoRomfs(writer, QDir(entry.filePath()));
            writer->closedir();
        }
        else
        {
            QFile file(entry.filePath());

            if (file.open(QIODevice::ReadOnly))
            {
                writer->mkfile(entry.fileName(), file.readAll());
            }
        }
    }
}

// The staging path for a romfs image the ROMFS editor dumps from / commits to
// and then hands the bootloader to flash. A private per-user subdirectory (not
// the shared %TEMP% root) so the fixed romfsN.img name can't collide with a
// second IDE instance or unrelated files, and nothing external churns it during
// the multi-minute flash. Kept separate from the firmware-flash staging dir
// (openmv-fw-staging-<pid>), which is wiped on every flash.
static QString romfsStagingPath(int romfsIndex)
{
    const QString dir = QDir::cleanPath(QDir::tempPath() + QStringLiteral("/openmv-romfs-staging"));
    QDir().mkpath(dir);
    return QDir::cleanPath(dir + QDir::separator() + QString(QStringLiteral("romfs%1.img")).arg(romfsIndex));
}

void OpenMVPlugin::romfsOperationIncomplete(const QString &title, bool write)
{
    if (write)
    {
        OMVUI::warning(Core::ICore::dialogParent(), title,
            Tr::tr("The ROMFS write did not complete, so the ROM file system on your OpenMV Cam may be incomplete.\n\n") +
            (m_romfsBootloaderLeft
             ? Tr::tr("Reset the ROM file system, or edit and commit it again, before relying on it.")
             : Tr::tr("Your OpenMV Cam is still in bootloader mode: click Connect and use the bootloader options "
                      "to reset the ROM file system or to edit and commit it again.")));
    }
    else
    {
        OMVUI::information(Core::ICore::dialogParent(), title,
            Tr::tr("The ROMFS read did not complete.\n\n"
                   "Your OpenMV Cam is still in bootloader mode: click Connect and use the bootloader options "
                   "to edit the ROM file system again."));
    }
}

// The board's ROMFS target (index, size, alignment rules) for the attached or
// chosen board, or an empty object with `error` set.
QJsonObject OpenMVPlugin::romfsTarget(const QString &title, QJsonObject *boardSettings, QString *error)
{
    Utils::QtcSettings *settings = Core::ICore::settings();
    *boardSettings = getBoardSettings(title, settings, true);

    if (boardSettings->isEmpty())
    {
        *error = Tr::tr("No board type was chosen.");
        return QJsonObject();
    }

    if (!boardSettings->contains(QStringLiteral("romfsConfig")))
    {
        *error = Tr::tr("ROMFS is not supported on this board!");
        return QJsonObject();
    }

    QJsonObject romfsConfigSettings = getROMFSConfig(title, *boardSettings, settings);

    if (romfsConfigSettings.isEmpty() || (!romfsConfigSettings.value(QStringLiteral("size")).toInt()))
    {
        *error = romfsConfigSettings.isEmpty() ? Tr::tr("No ROMFS target was chosen.") : Tr::tr("ROMFS is not supported on this board!");
        return QJsonObject();
    }

    (*boardSettings)[QStringLiteral("romfsConfig")] = romfsConfigSettings;
    return romfsConfigSettings;
}

bool OpenMVPlugin::romfsReadToDir(const QString &dir, QString *error)
{
    const QString title = Tr::tr("Edit ROMFS");
    QJsonObject boardSettings;
    QJsonObject romfs = romfsTarget(title, &boardSettings, error);
    if (romfs.isEmpty()) return false;

    const int romfsIndex = romfs.value(QStringLiteral("index")).toInt();
    const int romfsImageSize = romfs.value(QStringLiteral("size")).toInt();

    RomfsSessionGuard romfsSession(this);
    bool wasConnected = m_connected;

    QFile romfsFile(romfsStagingPath(romfsIndex));

    if (romfsFile.exists() && (!romfsFile.remove()))
    {
        *error = romfsFile.errorString();
        return false;
    }

    {
        QEventLoop loop;
        connect(this, &OpenMVPlugin::workingDone, &loop, &QEventLoop::quit);
        QString path = QFileInfo(romfsFile).filePath();
        QTimer::singleShot(0, this, [this, path, boardSettings] {
            connectClicked(true, path, false, false, false, m_nonDFUBoardPresent,
                           boardSettings.value(QStringLiteral("boardDisplayName")).toString(),
                           OPENMV_ROMFS_READ);
        });
        loop.exec();
    }

    if (m_romfsOperationFailed)
    {
        romfsFile.remove();

        if (m_romfsBootloaderLeft)
        {
            if (wasConnected) connectClicked(false, QString(), false, false, false, true);
        }
        else
        {
            romfsOperationIncomplete(title, false);
        }

        *error = Tr::tr("Reading the ROMFS from the camera failed.");
        return false;
    }

    bool ok = false;

    if (romfsFile.open(QFile::ReadOnly))
    {
        QByteArray data = romfsFile.readAll();
        VfsRomReader reader(data);
        ok = (data.size() >= romfsImageSize) && reader.unpack(dir);
        romfsFile.close();
        if (!ok) *error = Tr::tr("Failed to unpack ROMFS!");
    }
    else
    {
        *error = romfsFile.errorString();
    }

    if (wasConnected) connectClicked(false, QString(), false, false, false, true);
    return ok;
}

bool OpenMVPlugin::romfsWriteFromDir(const QString &dir, QString *error)
{
    const QString title = Tr::tr("Edit ROMFS");
    QJsonObject boardSettings;
    QJsonObject romfs = romfsTarget(title, &boardSettings, error);
    if (romfs.isEmpty()) return false;

    const int romfsIndex = romfs.value(QStringLiteral("index")).toInt();
    const int romfsImageSize = romfs.value(QStringLiteral("size")).toInt();
    const QJsonArray alignmentRules = romfs.value(QStringLiteral("alignmentRules")).toArray();

    // Pack first: an over-size image must not touch the camera.
    VfsRomWriter writer(alignmentRules);
    packDirIntoRomfs(&writer, QDir(dir));
    const QByteArray image = writer.finalize();

    if (image.size() > romfsImageSize)
    {
        *error = Tr::tr("The ROMFS image is %L1 bytes but the board's ROMFS holds %L2 bytes.").arg(image.size()).arg(romfsImageSize);
        return false;
    }

    RomfsSessionGuard romfsSession(this);
    bool wasConnected = m_connected;

    QFile romfsFile(romfsStagingPath(romfsIndex));

    if (!romfsFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        *error = romfsFile.errorString();
        return false;
    }

    romfsFile.write(image);
    romfsFile.close();

    {
        QEventLoop loop;
        connect(this, &OpenMVPlugin::workingDone, &loop, &QEventLoop::quit);
        QString path = QFileInfo(romfsFile).filePath();
        QTimer::singleShot(0, this, [this, path, boardSettings] {
            connectClicked(true, path, false, false, false, true,
                           boardSettings.value(QStringLiteral("boardDisplayName")).toString(),
                           OPENMV_ROMFS_WRITE);
        });
        loop.exec();
    }

    if (m_romfsOperationFailed)
    {
        romfsOperationIncomplete(title, true);
        if (m_romfsBootloaderLeft && wasConnected) connectClicked(false, QString(), false, false, false, true);
        *error = Tr::tr("Writing the ROMFS to the camera failed.");
        return false;
    }

    if (wasConnected) connectClicked(false, QString(), false, false, false, true);
    return true;
}

void OpenMVPlugin::editRomfsClicked(bool fromConnect, bool newRomfs)
{
    if (m_working)
    {
        deferNormal([this, fromConnect, newRomfs] { editRomfsClicked(fromConnect, newRomfs); });
        return;
    }

    QTemporaryDir tempDir;

    if(!tempDir.isValid())
    {
        OMVUI::critical(Core::ICore::dialogParent(),
            Tr::tr("Edit ROMFS"),
            tempDir.errorString());

        return;
    }

    Utils::QtcSettings *settings = Core::ICore::settings();

    QJsonObject boardSettings = getBoardSettings(Tr::tr("Edit ROMFS"), settings, (!newRomfs) && fromConnect);

    if (boardSettings.isEmpty())
    {
        return;
    }

    int romfsIndex = 0;
    int romfsImageSize = 0;
    QJsonArray alignmentRules;

    if (boardSettings.contains(QStringLiteral("romfsConfig")))
    {
        QJsonObject romfsConfigSettings = getROMFSConfig(Tr::tr("Edit ROMFS"), boardSettings, settings);

        if (romfsConfigSettings.isEmpty())
        {
            return;
        }

        boardSettings[QStringLiteral("romfsConfig")] = romfsConfigSettings;

        if (!romfsConfigSettings.value(QStringLiteral("size")).toInt())
        {
            OMVUI::critical(Core::ICore::dialogParent(),
                Tr::tr("Edit ROMFS"),
                Tr::tr("ROMFS is not supported on this board!"));

            return;
        }

        romfsIndex = romfsConfigSettings.value(QStringLiteral("index")).toInt();
        romfsImageSize = romfsConfigSettings.value(QStringLiteral("size")).toInt();
        alignmentRules = romfsConfigSettings.value(QStringLiteral("alignmentRules")).toArray();
    }
    else
    {
        OMVUI::critical(Core::ICore::dialogParent(),
            Tr::tr("Edit ROMFS"),
            Tr::tr("ROMFS is not supported on this board!"));

        return;
    }

    // Marks the session for the auto-reconnect gate (the editor's own connect
    // calls below are direct) and clears the bootloaders' failure flag.
    RomfsSessionGuard romfsSession(this);
    bool wasConnected = m_connected;

    if (!newRomfs)
    {
        if (fromConnect)
        {
            QFile romfsFile(romfsStagingPath(romfsIndex));

            if ((!romfsFile.exists()) || romfsFile.remove())
            {

                QEventLoop loop;
                connect(this, &OpenMVPlugin::workingDone, &loop, &QEventLoop::quit);

                QString path = QFileInfo(romfsFile).filePath();

                QTimer::singleShot(0, this, [this, path, boardSettings] {
                    connectClicked(true, path, false, false, false, m_nonDFUBoardPresent,
                                   boardSettings.value(QStringLiteral("boardDisplayName")).toString(),
                                   OPENMV_ROMFS_READ);
                });

                loop.exec();

                if (m_romfsOperationFailed)
                {
                    // Cancelled or failed in the bootloader: no "failed to
                    // unpack" for a read the user stopped. If the bootloader
                    // was sent back to the app, wait for it like a normal
                    // reconnect; otherwise the cam is still in DFU (a wait
                    // would spin for 10 s and raise the DFU dialog), so say so.
                    romfsFile.remove();

                    if (m_romfsBootloaderLeft)
                    {
                        if (wasConnected) connectClicked(false, QString(), false, false, false, true);
                    }
                    else
                    {
                        romfsOperationIncomplete(Tr::tr("Edit ROMFS"), false);
                    }

                    return;
                }

                if (romfsFile.open(QFile::ReadOnly))
                {
                    QByteArray data = romfsFile.readAll();
                    VfsRomReader reader(data);
                    bool ok = (data.size() >= romfsImageSize) && reader.unpack(tempDir.path());
                    romfsFile.close();

                    if (!ok)
                    {
                        OMVUI::critical(Core::ICore::dialogParent(),
                            Tr::tr("Edit ROMFS"), Tr::tr("Failed to unpack ROMFS!"));

                        if (wasConnected) connectClicked(false, QString(), false, false, false, true);
                        return;
                    }
                }
                else
                {
                    if (wasConnected) connectClicked(false, QString(), false, false, false, true);
                    return;
                }
            }
            else
            {
                OMVUI::critical(Core::ICore::dialogParent(),
                    Tr::tr("Edit ROMFS"),
                    romfsFile.errorString());

                return;
            }
        }
        else
        {
            QString path = OMVUI::getOpenFileName(Core::ICore::dialogParent(), Tr::tr("OpenMV ROMFS"),
                settings->value(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_OPEN_PATH, QDir::homePath()).toString(),
                Tr::tr("ROMFS Images (*.img)"));

            if (!path.isEmpty())
            {
                QFile romfsFile(path);

                if (romfsFile.open(QIODevice::ReadOnly))
                {
                    VfsRomReader reader(romfsFile.readAll());
                    bool ok = reader.unpack(tempDir.path());
                    romfsFile.close();

                    settings->setValue(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_OPEN_PATH, path);

                    if (!ok)
                    {
                        OMVUI::critical(Core::ICore::dialogParent(),
                            Tr::tr("Edit ROMFS"), Tr::tr("Failed to unpack ROMFS!"));

                        return;
                    }
                }
                else
                {
                    OMVUI::critical(Core::ICore::dialogParent(),
                        Tr::tr("OpenMV ROMFS"),
                        romfsFile.errorString());

                    return;
                }
            }
            else
            {
                return;
            }
        }
    }

    if (m_viewerMode)
    {
        // Load-only: skip the editor dialog and the commit/save choice. Re-pack the
        // unpacked image (re-aligned for the board, exactly as the IDE's commit
        // does) and write it straight to the cam.
        QFile romfsFile(romfsStagingPath(romfsIndex));

        if (romfsFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            VfsRomWriter writer(alignmentRules);
            packDirIntoRomfs(&writer, QDir(tempDir.path()));
            romfsFile.write(writer.finalize());
            romfsFile.close();

            QEventLoop loop;
            connect(this, &OpenMVPlugin::workingDone, &loop, &QEventLoop::quit);

            QString path = QFileInfo(romfsFile).filePath();

            QTimer::singleShot(0, this, [this, path, boardSettings] {
                connectClicked(true, path, false, false, false, true,
                               boardSettings.value(QStringLiteral("boardDisplayName")).toString(),
                               OPENMV_ROMFS_WRITE);
            });

            loop.exec();
        }
        else
        {
            OMVUI::critical(Core::ICore::dialogParent(),
                Tr::tr("Load ROMFS onto OpenMV Cam"),
                romfsFile.errorString());
        }

        if (m_romfsOperationFailed)
        {
            romfsOperationIncomplete(Tr::tr("Load ROMFS onto OpenMV Cam"), true);
            if (m_romfsBootloaderLeft && wasConnected) connectClicked(false, QString(), false, false, false, true);
            return;
        }

        if (wasConnected) connectClicked(false, QString(), false, false, false, true);
        return;
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
    QPushButton *addModel = new QPushButton(Tr::tr("Model Zoo"));
    box->addButton(addModel, QDialogButtonBox::ActionRole);
    QPushButton *addFile = new QPushButton(Tr::tr("Add File"));
    box->addButton(addFile, QDialogButtonBox::ActionRole);
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

    if(settings->contains(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_GEOMETRY))
    {
        dialog->restoreGeometry(settings->value(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_GEOMETRY).toByteArray());
    }
    else
    {
        dialog->resize(640, 480);
    }

    while (OMVUI::exec(dialog) == QDialog::Accepted)
    {
        settings->setValue(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_GEOMETRY, dialog->saveGeometry());
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
        combo2->setCurrentIndex(settings->value(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_ACTION, 0).toInt());
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

        // Cancel in this dialog, in the Save As chooser, or an error opening
        // the output means "back to the editor", not "discard the edits".
        bool done = false;

        if (OMVUI::exec(dialog2) == QDialog::Accepted)
        {
            settings->setValue(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_ACTION, combo2->currentIndex());

            if(combo2->currentIndex() == 0)
            {
                QFile romfsFile(romfsStagingPath(romfsIndex));

                if (romfsFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
                {
                    VfsRomWriter writer(alignmentRules);
                    romfsEditor->createRomfs(&writer,
                        romfsEditor->filter()->mapFromSource(romfsEditor->model()->index(romfsEditor->model()->rootPath())));
                    romfsFile.write(writer.finalize());
                    romfsFile.close();


                    QEventLoop loop;
                    connect(this, &OpenMVPlugin::workingDone, &loop, &QEventLoop::quit);

                    QString path = QFileInfo(romfsFile).filePath();

                    QTimer::singleShot(0, this, [this, path, boardSettings] {
                        connectClicked(true, path, false, false, false, true,
                                       boardSettings.value(QStringLiteral("boardDisplayName")).toString(),
                                       OPENMV_ROMFS_WRITE);
                    });

                    loop.exec();
                    done = true;
                }
                else
                {
                    OMVUI::critical(Core::ICore::dialogParent(),
                        Tr::tr("Edit ROMFS"),
                        romfsFile.errorString());
                }
            }
            else if(combo2->currentIndex() == 1)
            {
                QString path = OMVUI::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Edit ROMFS"),
                    settings->value(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_SAVE_PATH, QDir::homePath()).toString(),
                    Tr::tr("ROMFS Images (*.img)"));

                if(!path.isEmpty())
                {
                    QFile romfsFile(path);

                    if (romfsFile.open(QIODevice::WriteOnly))
                    {
                        VfsRomWriter writer(alignmentRules);
                        romfsEditor->createRomfs(&writer,
                            romfsEditor->filter()->mapFromSource(romfsEditor->model()->index(romfsEditor->model()->rootPath())));
                        romfsFile.write(writer.finalize());
                        romfsFile.close();

                        settings->setValue(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_SAVE_PATH, path);
                        done = true;
                    }
                    else
                    {
                        OMVUI::critical(Core::ICore::dialogParent(),
                            Tr::tr("Edit ROMFS"),
                            romfsFile.errorString());
                    }
                }
            }
        }

        delete dialog2;

        if (done)
        {
            break;
        }
    }

    settings->setValue(SETTINGS_GROUP "/" LAST_ROMFS_DIALOG_GEOMETRY, dialog->saveGeometry());

    delete dialog;

    if (m_romfsOperationFailed)
    {
        romfsOperationIncomplete(Tr::tr("Edit ROMFS"), true);
        if (m_romfsBootloaderLeft && wasConnected) connectClicked(false, QString(), false, false, false, true);
        return;
    }

    if (wasConnected) connectClicked(false, QString(), false, false, false, true);
}

void OpenMVPlugin::resetRomfsClicked()
{
    if (m_working)
    {
        deferNormal([this] { resetRomfsClicked(); });
        return;
    }

    if(OMVUI::warning(Core::ICore::dialogParent(),
        Tr::tr("Reset ROMFS"),
        Tr::tr("Are you sure you want to reset your OpenMV Cam's ROM file system?"),
        QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
    == QMessageBox::Yes)
    {
        Utils::QtcSettings *settings = Core::ICore::settings();

        QJsonObject boardSettings = getBoardSettings(Tr::tr("Reset ROMFS"), settings, true);

        if (boardSettings.isEmpty())
        {
            return;
        }

        int romfsIndex = 0;

        if (boardSettings.contains(QStringLiteral("romfsConfig")))
        {
            QJsonObject romfsConfigSettings = getROMFSConfig(Tr::tr("Reset ROMFS"), boardSettings, settings);

            if (romfsConfigSettings.isEmpty())
            {
                return;
            }

            boardSettings[QStringLiteral("romfsConfig")] = romfsConfigSettings;

            if (!romfsConfigSettings.value(QStringLiteral("size")).toInt())
            {
                OMVUI::critical(Core::ICore::dialogParent(),
                    Tr::tr("Reset ROMFS"),
                    Tr::tr("ROMFS is not supported on this board!"));

                return;
            }

            romfsIndex = romfsConfigSettings.value(QStringLiteral("index")).toInt();
        }
        else
        {
            OMVUI::critical(Core::ICore::dialogParent(),
                Tr::tr("Reset ROMFS"),
                Tr::tr("ROMFS is not supported on this board!"));

            return;
        }


        // Marks the session for the auto-reconnect gate (the editor's own connect
        // calls below are direct) and clears the bootloaders' failure flag.
        RomfsSessionGuard romfsSession(this);
        bool wasConnected = m_connected;

        QEventLoop loop;
        connect(this, &OpenMVPlugin::workingDone, &loop, &QEventLoop::quit);

        QString path = OpenMVThirdParty::firmwareRootForBoard(boardSettings).
                pathAppended(boardSettings.value(QStringLiteral("boardFirmwareFolder")).toString()).
                pathAppended(QString(QStringLiteral("romfs%1.img").arg(romfsIndex))).toString();

        QTimer::singleShot(0, this, [this, path, boardSettings] {
            connectClicked(true, path, false, false, false, true,
                           boardSettings.value(QStringLiteral("boardDisplayName")).toString(),
                           OPENMV_ROMFS_WRITE);
        });

        loop.exec();

        if (m_romfsOperationFailed)
        {
            romfsOperationIncomplete(Tr::tr("Reset ROMFS"), true);
            if (m_romfsBootloaderLeft && wasConnected) connectClicked(false, QString(), false, false, false, true);
            return;
        }

        if (wasConnected) connectClicked(false, QString(), false, false, false, true);
    }
}

} // namespace Internal
} // namespace OpenMV
