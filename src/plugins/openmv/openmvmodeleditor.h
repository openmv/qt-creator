#ifndef OPENMVMODELEDITOR_H
#define OPENMVMODELEDITOR_H

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

class OpenMVModelEditorModel : public QFileSystemModel
{
    Q_OBJECT

public:

    explicit OpenMVModelEditorModel(QObject *parent = Q_NULLPTR);
    virtual int columnCount(const QModelIndex &parent = QModelIndex()) const override;

signals:

public slots:

};

class OpenMVModelEditor : public QTreeView
{
    Q_OBJECT

public:

    explicit OpenMVModelEditor(QWidget *parent = Q_NULLPTR);

public slots:

    void setRootPath(const QString &path);
    void frameBufferData(const QPixmap &data);
    void newClassFolder();
    void snapshot();

signals:

    void rootPathSet();
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

    OpenMVModelEditorModel *m_model;
    QPixmap m_pixmap;
};

#endif // OPENMVMODELEDITOR_H
