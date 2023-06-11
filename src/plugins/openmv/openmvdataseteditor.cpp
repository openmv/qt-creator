#include "openmvdataseteditor.h"

OpenMVDatasetEditorModel::OpenMVDatasetEditorModel(QObject *parent) : QFileSystemModel(parent)
{
    setRootPath(QString()); // Force empty...
}

int OpenMVDatasetEditorModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)

    return 1;
}

OpenMVDatasetEditor::OpenMVDatasetEditor(QWidget *parent) : QTreeView(parent), m_model(new OpenMVDatasetEditorModel(this)), m_pixmap(QPixmap())
{
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setFrameStyle(QFrame::NoFrame);
    setHeaderHidden(true);
    setModel(m_model);
    setStyleSheet(QStringLiteral("QAbstractScrollArea{background-color:#1E1E27;color:#FFFFFF;}" // https://doc.qt.io/qt-5/stylesheet-examples.html#customizing-qtreeview
                                 "QTreeView::branch:has-children:!has-siblings:closed,QTreeView::branch:closed:has-children:has-siblings{border-image:none;image:url(:/openmv/images/branch-closed.png);}"
                                 "QTreeView::branch:open:has-children:!has-siblings,QTreeView::branch:open:has-children:has-siblings{border-image:none;image:url(:/openmv/images/branch-open.png);}"));
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    m_classFolderRegex = QRegularExpression(QStringLiteral("^(.+?)\\.class$"));
    m_classFolderRegex.optimize();

    m_snapshotRegex = QRegularExpression(QStringLiteral("^.*?(\\d+).*?\\.(jpg|jpeg|png|bmp)$"));
    m_snapshotRegex.optimize();

    connect(this, &OpenMVDatasetEditor::doubleClicked, this, [this] (const QModelIndex &index) {
        QString file = m_model->fileInfo(index).filePath();

        if (QFileInfo(file).isFile()) {
            TextEditor::BaseTextEditor *editor = qobject_cast<TextEditor::BaseTextEditor *>(Core::EditorManager::openEditor(Utils::FilePath::fromString(file)));

            if(editor) {
                Core::EditorManager::addCurrentPositionToNavigationHistory();
                Core::EditorManager::activateEditor(editor);
            }
        }
    });
}

QStringList OpenMVDatasetEditor::classFolderList()
{
    return m_model->rootDirectory().entryList(QStringList() << QStringLiteral("*.class"), QDir::Dirs, QDir::Name);
}

QStringList OpenMVDatasetEditor::snapshotList(const QString &classFolder)
{
    return QDir(m_model->rootPath() + QDir::separator() + classFolder).entryList(QStringList() << QStringLiteral("*.jpg") << QStringLiteral("*.jpeg") << QStringLiteral("*.png") << QStringLiteral("*.bmp"), QDir::Files, QDir::Name).filter(m_snapshotRegex);
}

QString OpenMVDatasetEditor::rootPath()
{
    return m_model->rootPath();
}

void OpenMVDatasetEditor::setRootPath(const QString &path)
{
    if(m_model->rootPath() != path)
    {
        emit rootPathClosed(m_model->rootPath());
    }

    if(path.isEmpty()) // Prevents locking of closed folder.
    {
        delete m_model;
        m_model = new OpenMVDatasetEditorModel(this);
        setModel(m_model);
    }

    setRootIndex(m_model->setRootPath(path));

    if(!path.isEmpty())
    {
        datasetCaptureScriptPath = QDir::cleanPath(QDir::fromNativeSeparators(m_model->rootPath() + QStringLiteral("/dataset_capture_script.py")));
        labelsPath = QDir::cleanPath(QDir::fromNativeSeparators(m_model->rootPath() + QStringLiteral("/labels.txt")));

        emit rootPathSet(path);
    }
}

void OpenMVDatasetEditor::frameBufferData(const QPixmap &data)
{
    if(!m_model->rootPath().isEmpty())
    {
        m_pixmap = data;

        emit snapshotEnable((!getClassFolderPath().isEmpty()) && (!m_pixmap.isNull()));
    }
}

void OpenMVDatasetEditor::newClassFolder()
{
    bool ok;
    QString name = QInputDialog::getText(Core::ICore::dialogParent(),
                                         tr("Dataset Editor"), tr("Please enter a class name"),
                                         QLineEdit::Normal, QString(), &ok,
                                         Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                         (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

    if(ok)
    {
        QStringList list = classFolderList();
//      int number = 0;

//      if(!list.isEmpty())
//      {
//          QRegularExpressionMatch match = m_classFolderRegex.match(list.last());

//          if(match.hasMatch())
//          {
//              number = match.captured(1).toInt() + 1;
//          }
//      }

        QString dir_name = QString(QStringLiteral("%1.class")).arg(name);

        if(m_model->rootDirectory().mkdir(dir_name))
        {
            Utils::FileSaver file(Utils::FilePath::fromString(m_model->rootPath()).pathAppended(QStringLiteral("labels.txt")));

            if(!file.hasError())
            {
                bool write_ok = true;

                foreach(const QString &string, list << dir_name)
                {
                    QRegularExpressionMatch match = m_classFolderRegex.match(string);

                    if(match.hasMatch())
                    {
                        if(!file.write(QString(match.captured(1) + QLatin1Char('\n')).toUtf8())) write_ok = false;
                    }
                }

                if((!write_ok) || (!file.finalize()))
                {
                    QMessageBox::critical(Core::ICore::dialogParent(),
                        tr("Dataset Editor"),
                        tr("Error: %L1!").arg(file.errorString()));
                }
            }
            else
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    tr("Dataset Editor"),
                    tr("Error: %L1!").arg(file.errorString()));
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                tr("Dataset Editor"),
                tr("Failed to create \"%L1\"!").arg(name));
        }
    }
}

void OpenMVDatasetEditor::snapshot()
{
    QString path = getClassFolderPath();
    QStringList list = snapshotList(QDir(path).dirName());
    int number = 0;

    if(!list.isEmpty())
    {
        QRegularExpressionMatch match = m_snapshotRegex.match(list.last());

        if(match.hasMatch())
        {
            number = match.captured(1).toInt() + 1;
        }
    }

    QString filePath = QDir::cleanPath(QDir::fromNativeSeparators(path + QString(QStringLiteral("/%1.jpg"))).arg(number, 5, 10, QLatin1Char('0')));

    if(m_pixmap.save(filePath))
    {
        setCurrentIndex(m_model->index(filePath));
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            tr("Dataset Editor"),
            tr("Failed to save the image file for an unknown reason!"));
    }
}

void OpenMVDatasetEditor::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex index = indexAt(event->pos());

    if(index.isValid())
    {
        QString path = QDir::cleanPath(QDir::fromNativeSeparators(m_model->fileInfo(index).canonicalFilePath()));

        if((path != datasetCaptureScriptPath) && (path != labelsPath))
        {
            QMenu menu;

            connect(menu.addAction(tr("Delete")), &QAction::triggered, this, [this] {
                foreach(const QModelIndex &index, selectedIndexes())
                {
                    if(QMessageBox::question(Core::ICore::dialogParent(),
                                             tr("Dataset Editor"),
                                             tr("Are you sure you want to permanetly delete \"%L1\"?").arg(m_model->fileName(index)),
                                             QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No)
                    == QMessageBox::Yes)
                    {
                        bool wasClassFolder = getClassFolderPath() == m_model->fileInfo(index).canonicalFilePath();
                        QString error;

                        if(!Utils::FilePath::fromString(m_model->fileInfo(index).canonicalFilePath()).removeRecursively(&error))
                        {
                            QMessageBox::critical(Core::ICore::dialogParent(),
                                tr("Dataset Editor"),
                                tr("Error: %L1!").arg(error));
                        }
                        else if(wasClassFolder)
                        {
                            updateLabels();
                        }
                    }
                }
            });

            connect(menu.addAction(tr("Rename")), &QAction::triggered, this, [this, index] {
                if(getClassFolderPath() == m_model->fileInfo(index).canonicalFilePath())
                {
                    QRegularExpressionMatch match = m_classFolderRegex.match(m_model->fileName(index));

                    if(Q_LIKELY(match.hasMatch()))
                    {
                        bool ok;
                        QString name = QInputDialog::getText(Core::ICore::dialogParent(),
                                                             tr("Dataset Editor"), tr("Please enter a new name"),
                                                             QLineEdit::Normal, match.captured(1), &ok,
                                                             Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                                             (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                        if(ok)
                        {
                            if(!QDir().rename(m_model->fileInfo(index).filePath(), QString(QStringLiteral("%1/%2.class")).arg(m_model->fileInfo(index).path()).arg(name)))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Dataset Editor"),
                                    tr("Failed to rename the folder for an unknown reason!"));
                            }
                            else
                            {
                                updateLabels();
                            }
                        }
                    }
                }
                else
                {
                    bool ok;
                    QString name = QInputDialog::getText(Core::ICore::dialogParent(),
                                                         tr("Dataset Editor"), tr("Please enter a new name"),
                                                         QLineEdit::Normal, m_model->fileName(index), &ok,
                                                         Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                                         (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

                    if(ok)
                    {
                        if(m_model->fileInfo(index).isDir())
                        {
                            if(!QDir().rename(m_model->fileInfo(index).filePath(), m_model->fileInfo(index).path() + QDir::separator() + name))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Dataset Editor"),
                                    tr("Failed to rename the folder for an unknown reason!"));
                            }
                        }
                        else
                        {
                            if(!QFile(m_model->fileInfo(index).filePath()).rename(m_model->fileInfo(index).path() + QDir::separator() + name))
                            {
                                QMessageBox::critical(Core::ICore::dialogParent(),
                                    tr("Dataset Editor"),
                                    tr("Failed to rename the file for an unknown reason!"));
                            }
                        }
                    }
                }
            });

            menu.exec(event->globalPos());
        }
    }

    QTreeView::contextMenuEvent(event);
}

void OpenMVDatasetEditor::keyPressEvent(QKeyEvent *event)
{
    switch(event->key())
    {
        case Qt::Key_Delete:
        {
            foreach(const QModelIndex &index, selectedIndexes())
            {
                QString path = QDir::cleanPath(QDir::fromNativeSeparators(m_model->fileInfo(index).canonicalFilePath()));

                if((path == datasetCaptureScriptPath) || (path == labelsPath))
                {
                    continue;
                }

                if(QMessageBox::question(Core::ICore::dialogParent(),
                                         tr("Dataset Editor"),
                                         tr("Are you sure you want to permanetly delete \"%L1\"?").arg(m_model->fileName(index)),
                                         QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::No)
                == QMessageBox::Yes)
                {
                    bool wasClassFolder = getClassFolderPath() == m_model->fileInfo(index).canonicalFilePath();
                    QString error;

                    if(!Utils::FilePath::fromString(m_model->fileInfo(index).canonicalFilePath()).removeRecursively(&error))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Dataset Editor"),
                            tr("Error: %L1!").arg(error));
                    }
                    else if(wasClassFolder)
                    {
                        updateLabels();
                    }
                }
            }

            break;
        }
    }

    QTreeView::keyPressEvent(event);
}

void OpenMVDatasetEditor::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    emit snapshotEnable((!getClassFolderPath().isEmpty()) && (!m_pixmap.isNull()));

//  Utils::MimeDatabase db;

    foreach(const QModelIndex &index, selectedIndexes())
    {
        QString file = m_model->fileInfo(index).filePath();
        QPixmap pixmap(file);

//      if(pixmap.isNull())
//      {
//          Utils::MimeType mt = db.mimeTypeForFile(file);
//
//          if(mt.isValid() && (mt.name().startsWith(QStringLiteral("text"))))
//          {
//              QPixmap textPixmap(400, 400);
//              textPixmap.fill();
//
//              QPainter painter;
//
//              if(painter.begin(&textPixmap))
//              {
//                  QFile textFile(file);
//
//                  if(textFile.open(QIODevice::ReadOnly))
//                  {
//                      QTextDocument document(QString::fromUtf8(textFile.readAll()));
//                      document.setDefaultFont(TextEditor::TextEditorSettings::fontSettings().defaultFixedFontFamily());
//                      document.drawContents(&painter);
//
//                      if(painter.end())
//                      {
//                          pixmap = textPixmap;
//                      }
//                  }
//              }
//          }
//      }

        emit pixmapUpdate(pixmap);
    }

    QTreeView::selectionChanged(selected, deselected);
}

void OpenMVDatasetEditor::hideEvent(QHideEvent *event)
{
    emit visibilityChanged(false);
    QTreeView::hideEvent(event);
}

void OpenMVDatasetEditor::showEvent(QShowEvent *event)
{
    emit visibilityChanged(true);
    QTreeView::showEvent(event);
}

QString OpenMVDatasetEditor::getClassFolderPath()
{
    QModelIndexList list = selectedIndexes();

    if(!list.isEmpty())
    {
        QFileInfo indexInfo = m_model->fileInfo(list.last());
        QString path = indexInfo.isFile() ? indexInfo.canonicalPath() : indexInfo.canonicalFilePath();

        if(QDir(path).canonicalPath() != m_model->rootDirectory().canonicalPath())
        {
            forever
            {
                QString temp = QFileInfo(path).dir().canonicalPath();

                if(temp == m_model->rootDirectory().canonicalPath())
                {
                    break;
                }

                path = temp;
            }

            if(m_classFolderRegex.match(QDir(path).dirName()).hasMatch())
            {
                return path;
            }
        }
    }

    return QString();
}

void OpenMVDatasetEditor::updateLabels()
{
    Utils::FileSaver file(Utils::FilePath::fromString(m_model->rootPath()).pathAppended(QStringLiteral("labels.txt")));

    if(!file.hasError())
    {
        bool write_ok = true;

        foreach(const QString &string, classFolderList())
        {
            QRegularExpressionMatch match = m_classFolderRegex.match(string);

            if(match.hasMatch())
            {
                if(!file.write(QString(match.captured(1) + QLatin1Char('\n')).toUtf8())) write_ok = false;
            }
        }

        if((!write_ok) || (!file.finalize()))
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                tr("Dataset Editor"),
                tr("Error: %L1!").arg(file.errorString()));
        }
    }
    else
    {
        QMessageBox::critical(Core::ICore::dialogParent(),
            tr("Dataset Editor"),
            tr("Error: %L1!").arg(file.errorString()));
    }
}
