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

private:

    QString getClassFolderPath();
    void updateLabels();

    OpenMVDatasetEditorModel *m_model;
    QPixmap m_pixmap;

    QRegularExpression m_classFolderRegex;
    QRegularExpression m_snapshotRegex;
    QString datasetCaptureScriptPath;
    QString labelsPath;
};

#endif // OPENMVDATASETEDITOR_H
