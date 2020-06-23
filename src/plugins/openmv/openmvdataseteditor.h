#ifndef OPENMVDATASETEDITOR_H
#define OPENMVDATASETEDITOR_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

#include <coreplugin/icore.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/mimetypes/mimetype.h>
#include <utils/mimetypes/mimedatabase.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>

class OpenMVDatasetEditorModel : public QFileSystemModel
{
    Q_OBJECT

public:

    explicit OpenMVDatasetEditorModel(QObject *parent = Q_NULLPTR);
    virtual int columnCount(const QModelIndex &parent = QModelIndex()) const override;

signals:

public slots:

};

class OpenMVDatasetEditor : public QTreeView
{
    Q_OBJECT

public:

    explicit OpenMVDatasetEditor(QWidget *parent = Q_NULLPTR);
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

    void keyPressEvent(QKeyEvent *event) override;
    void selectionChanged(const QItemSelection &selected, const QItemSelection &deselected) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:

    QString getClassFolderPath();

    OpenMVDatasetEditorModel *m_model;
    QPixmap m_pixmap;
};

#endif // OPENMVDATASETEDITOR_H
