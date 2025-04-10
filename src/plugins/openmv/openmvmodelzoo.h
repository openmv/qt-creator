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

#ifndef OPENMVMODELZOO_H
#define OPENMVMODELZOO_H

#include <QtCore>
#include <QtWidgets>

#include <coreplugin/minisplitter.h>
#include <utils/qtcsettings.h>

namespace OpenMV {
namespace Internal {

class OpenMVModelZooBrowserFilter : public QSortFilterProxyModel {
    Q_OBJECT

public:
    OpenMVModelZooBrowserFilter(QObject *parent = Q_NULLPTR) : QSortFilterProxyModel(parent) {}

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override
    {
        QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        QFileSystemModel *fileModel = qobject_cast<QFileSystemModel *>(sourceModel());

        if (!fileModel)
        {
            return false;
        }

        if (fileModel->isDir(index))
        {
            return true;
        }

        QString fileName = fileModel->fileName(index);

        if (fileName.endsWith(".tflite"))
        {
            return true;
        }

        return false;
    }
};

class OpenMVModelZooBrowserTreeView : public QTreeView {
    Q_OBJECT

public:
    OpenMVModelZooBrowserTreeView(QWidget *parent = Q_NULLPTR) : QTreeView(parent) {}

signals:
    void selectionCleared();
    void paintEventSignal();

protected:

    void mousePressEvent(QMouseEvent *event) override
    {
        QModelIndex index = indexAt(event->pos());

        if (!index.isValid())
        {
            clearSelection();
            emit selectionCleared();
        }

        QTreeView::mousePressEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        QTreeView::paintEvent(event);
        emit paintEventSignal();
    }
};

class OpenMVModelZooBrowser : public QDialog
{
    Q_OBJECT

public:

    explicit OpenMVModelZooBrowser(Utils::QtcSettings *settings, QWidget *parent = Q_NULLPTR, bool saveDialog = false);
    ~OpenMVModelZooBrowser();

    QString selectedModel() const { return m_selectedModel; }

protected:

    void paintEvent(QPaintEvent *event) override;

private:

    void saveExpandedState(const QString &path, QStringList &list, const QModelIndex &index);
    void restoreExpandedState(const QString &path, const QModelIndex &index);

    Utils::QtcSettings *m_settings;
    QFileSystemModel *m_model;
    OpenMVModelZooBrowserTreeView *m_treeView;
    Core::MiniSplitter *m_splitter;
    OpenMVModelZooBrowserFilter *m_filter;
    QStringList m_listToExpand;
    QString m_selectedModel;
    bool m_initialized;
    QString m_styleSheet, m_highDPIStyleSheet;
    qreal m_devicePixelRatio;
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVMODELZOO_H
