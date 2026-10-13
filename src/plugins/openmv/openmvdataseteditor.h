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

#ifndef OPENMVDATASETEDITOR_H
#define OPENMVDATASETEDITOR_H

#include <QtCore>
#include <QtWidgets>

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/mimetypes2/mimetype.h>
#include <utils/mimetypes2/mimedatabase.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditor.h>
#include <texteditor/texteditorsettings.h>

namespace OpenMV {
namespace Internal {

class OpenMVDatasetEditorModel : public QFileSystemModel
{
    Q_OBJECT

public:

    explicit OpenMVDatasetEditorModel(QObject *parent = Q_NULLPTR);
    virtual int columnCount(const QModelIndex &parent = QModelIndex()) const override;
};

class OpenMVDatasetEditor : public QTreeView
{
    Q_OBJECT

public:

    explicit OpenMVDatasetEditor(QWidget *parent = Q_NULLPTR);
    QStringList classFolderList();
    QStringList snapshotList(const QString &classFolder);
    QString rootPath();

public slots:

    void setRootPath(const QString &path);
    void frameBufferData(const QPixmap &data);
    void newClassFolder();
    void snapshot();

signals:

    void rootPathClosed(const QString &path);
    void rootPathSet(const QString &path);
    void snapshotEnable(bool enable);
    void pixmapUpdate(const QPixmap &data);
    void visibilityChanged(bool visible);

protected:

    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:

    QString getClassFolderPath();
    void updateLabels();

    OpenMVDatasetEditorModel *m_model;
    QPixmap m_pixmap;

    QRegularExpression m_classFolderRegex;
    QRegularExpression m_snapshotRegex;
    QString datasetCaptureScriptPath;
    QString labelsPath;

    QString m_styleSheet, m_highDPIStyleSheet;
    qreal m_devicePixelRatio;
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVDATASETEDITOR_H
