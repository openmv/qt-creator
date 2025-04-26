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

#ifndef OPENMVROMFS_H
#define OPENMVROMFS_H

#include <QtCore>
#include <QtWidgets>

#include "tools/romfs.h"

#include <utils/qtcsettings.h>

enum OpenMVROMFSAccess
{
    OPENMV_ROMFS_NONE,
    OPENMV_ROMFS_READ,
    OPENMV_ROMFS_WRITE,
    OPENMV_ROMFS_RESET
};

namespace OpenMV {
namespace Internal {

QJsonObject getROMFSConfig(const QString &title,
                           const QJsonObject &boardSettings,
                           Utils::QtcSettings *settings);

QString convertModel(const QJsonObject &boardSettings,
                     const QString &model,
                     Utils::QtcSettings *settings);

class OpenMVROMFSEditorFilter : public QSortFilterProxyModel
{
    Q_OBJECT

public:

    OpenMVROMFSEditorFilter(QObject *parent = Q_NULLPTR) : QSortFilterProxyModel(parent) {}

protected:

    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override
    {
        QFileSystemModel *fsModel = qobject_cast<QFileSystemModel *>(sourceModel());

        if (!fsModel)
        {
            return false;
        }

        QFileInfo leftInfo(fsModel->filePath(left));
        QFileInfo rightInfo(fsModel->filePath(right));

        QDateTime leftTime = leftInfo.birthTime();
        QDateTime rightTime = rightInfo.birthTime();

        return leftTime < rightTime; // Sort by creation timestamp
    }
};

class OpenMVROMFSEditor : public QTreeView
{
    Q_OBJECT

public:

    explicit OpenMVROMFSEditor(QWidget *parent = Q_NULLPTR,
                               const QString &path = QString(),
                               const QJsonObject &boardSettings = QJsonObject());

    OpenMVROMFSEditorFilter *filter() { return m_filter; }
    QFileSystemModel *model() { return m_model; }

    void createRomfs(VfsRomWriter *writer, const QModelIndex &index);

public slots:

    void viewEdit();
    void addFile();
    void addModel();
    void newFolder();
    void remove();
    void extractFile();

signals:

    void fileSystemSize(const QString &sizeString);
    void commitEnabled(bool enabled);

protected:

    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:

    void preloadDirectories(const QModelIndex &index);
    void calculateFileSystemSize();

    OpenMVROMFSEditorFilter *m_filter;
    QFileSystemModel *m_model;
    QJsonObject m_boardSettings;
    QString m_styleSheet, m_highDPIStyleSheet;
    qreal m_devicePixelRatio;
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVROMFS_H
