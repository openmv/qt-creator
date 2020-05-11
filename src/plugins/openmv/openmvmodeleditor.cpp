#include "openmvmodeleditor.h"

OpenMVModelEditorModel::OpenMVModelEditorModel(QObject *parent) : QFileSystemModel(parent)
{

}

int OpenMVModelEditorModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent)
    return 1;
}

OpenMVModelEditor::OpenMVModelEditor(QWidget *parent) : QTreeView(parent)
{
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setFrameStyle(QFrame::NoFrame);
    setStyleSheet(QStringLiteral("QAbstractScrollArea{background-color:#1E1E27;color:#FFFFFF;}" // https://doc.qt.io/qt-5/stylesheet-examples.html#customizing-qtreeview
                                 "QTreeView::branch:has-children:!has-siblings:closed,QTreeView::branch:closed:has-children:has-siblings{border-image:none;image:url(:/openmv/images/branch-closed.png);}"
                                 "QTreeView::branch:open:has-children:!has-siblings,QTreeView::branch:open:has-children:has-siblings{border-image:none;image:url(:/openmv/images/branch-open.png);}"));
    setHeaderHidden(true);
    setUniformRowHeights(true);
    m_model = new OpenMVModelEditorModel(this);
    m_model->setReadOnly(false);
    setModel(m_model);
}

void OpenMVModelEditor::setRootPath(const QString &path)
{
    setRootIndex(m_model->setRootPath(path));
    emit rootPathSet();
}

void OpenMVModelEditor::frameBufferData(const QPixmap &data)
{
    m_pixmap = data;
    emit snapshotEnable((!getClassFolderPath().isEmpty()) && (!m_pixmap.isNull()));
}

void OpenMVModelEditor::newClassFolder()
{
    bool ok;
    QString name = QInputDialog::getText(Core::ICore::dialogParent(),
                                         tr("Dataset Editor"), tr("Please enter a class name"),
                                         QLineEdit::Normal, QString(), &ok,
                                         Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                                         (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));

    if(ok)
    {
        QRegularExpression regex = QRegularExpression(QStringLiteral("(\\d+)\\.(\\w+)\\.class"));

        QStringList list = m_model->rootDirectory().entryList(QStringList() << QStringLiteral("*.class"), QDir::Dirs, QDir::Name);
        int number = 0;

        if(!list.isEmpty())
        {
            QRegularExpressionMatch match = regex.match(list.last());

            if(match.hasMatch())
            {
                number = match.captured(1).toInt() + 1;
            }
        }

        QString dir_name = QString(QStringLiteral("%1.%2.class")).arg(number, 5, 10, QLatin1Char('0')).arg(name);

        if(m_model->rootDirectory().mkdir(dir_name))
        {
            Utils::FileSaver file(QDir::cleanPath(QDir::fromNativeSeparators(m_model->rootPath()) + QStringLiteral("/labels.txt")));

            if(!file.hasError())
            {
                bool write_ok = true;

                foreach(const QString &string, list << dir_name)
                {
                    QRegularExpressionMatch match = regex.match(string);

                    if(match.hasMatch())
                    {
                        if(!file.write(QString(match.captured(2) + QLatin1Char('\n')).toUtf8())) write_ok = false;
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

void OpenMVModelEditor::snapshot()
{
    QString path = getClassFolderPath();
    QRegularExpression regex = QRegularExpression(QStringLiteral("(\\d+)\\.(jpg|jpeg)"));
    QStringList list = QDir(path).entryList(QStringList() << QStringLiteral("*.jpg") << QStringLiteral("*.jpeg"), QDir::Files, QDir::Name).filter(regex);
    int number = 0;

    if(!list.isEmpty())
    {
        QRegularExpressionMatch match = regex.match(list.last());

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

void OpenMVModelEditor::keyPressEvent(QKeyEvent *event)
{
    switch(event->key())
    {
        case Qt::Key_Delete:
        {
            foreach(const QModelIndex &index, selectedIndexes())
            {
                if(QMessageBox::question(Core::ICore::dialogParent(),
                                         tr("Dataset Editor"),
                                         tr("Are you sure you want to permanetly delete \"%L1\"?").arg(m_model->fileName(index)),
                                         QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
                == QMessageBox::Yes)
                {
                    QString error;

                    if(!Utils::FileUtils::removeRecursively(Utils::FileName(m_model->fileInfo(index)), &error))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Dataset Editor"),
                            tr("Error: %L1!").arg(error));
                    }
                }
            }

            break;
        }
    }

    QTreeView::keyPressEvent(event);
}

void OpenMVModelEditor::selectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    emit snapshotEnable((!getClassFolderPath().isEmpty()) && (!m_pixmap.isNull()));

    Utils::MimeDatabase db;

    foreach(const QModelIndex &index, selectedIndexes())
    {
        QString file = m_model->fileInfo(index).filePath();
        QPixmap pixmap(file);

        if(pixmap.isNull())
        {
            Utils::MimeType mt = db.mimeTypeForFile(file);

            if(mt.isValid() && (mt.name().startsWith(QStringLiteral("text"))))
            {
                QPixmap textPixmap(400, 400);
                textPixmap.fill();

                QPainter painter;

                if(painter.begin(&textPixmap))
                {
                    QFile textFile(file);

                    if(textFile.open(QIODevice::ReadOnly))
                    {
                        QTextDocument document(QString::fromUtf8(textFile.readAll()));
                        QFont font = TextEditor::TextEditorSettings::fontSettings().defaultFixedFontFamily();
                        font.setBold(true);
                        document.setDefaultFont(font);
                        document.drawContents(&painter);

                        if(painter.end())
                        {
                            pixmap = textPixmap;
                        }
                    }
                }
            }
        }

        emit pixmapUpdate(pixmap);
    }

    QTreeView::selectionChanged(selected, deselected);
}

void OpenMVModelEditor::contextMenuEvent(QContextMenuEvent *event)
{
    QModelIndex index = indexAt(event->pos());

    if(index.isValid())
    {
        QMenu menu;

        connect(menu.addAction(tr("Delete")), &QAction::triggered, this, [this, index] {
            foreach(const QModelIndex &index, selectedIndexes())
            {
                if(QMessageBox::question(Core::ICore::dialogParent(),
                                         tr("Dataset Editor"),
                                         tr("Are you sure you want to permanetly delete \"%L1\"?").arg(m_model->fileName(index)),
                                         QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes)
                == QMessageBox::Yes)
                {
                    QString error;

                    if(!Utils::FileUtils::removeRecursively(Utils::FileName(m_model->fileInfo(index)), &error))
                    {
                        QMessageBox::critical(Core::ICore::dialogParent(),
                            tr("Dataset Editor"),
                            tr("Error: %L1!").arg(error));
                    }
                }
            }
        });

        connect(menu.addAction(tr("Rename")), &QAction::triggered, this, [this, index] {
            edit(index);
        });

        menu.exec(event->globalPos());
    }

    QTreeView::contextMenuEvent(event);
}

void OpenMVModelEditor::hideEvent(QHideEvent *event)
{
    emit visibilityChanged(false);
    QTreeView::hideEvent(event);
}

void OpenMVModelEditor::showEvent(QShowEvent *event)
{
    emit visibilityChanged(true);
    QTreeView::showEvent(event);
}

QString OpenMVModelEditor::getClassFolderPath()
{
    QModelIndexList list = selectedIndexes();
    QString result;

    if(!list.isEmpty())
    {
        QFileInfo indexInfo = m_model->fileInfo(list.last());
        QString path = indexInfo.isFile() ? indexInfo.canonicalPath() : indexInfo.canonicalFilePath();

        if(QDir(path).canonicalPath() != m_model->rootDirectory().canonicalPath())
        {
            while(QFileInfo(path).dir().canonicalPath() != m_model->rootDirectory().canonicalPath())
            {
                path = QFileInfo(path).dir().canonicalPath();
            }

            if(QRegularExpression(QStringLiteral("^(\\d+)\\.(\\w+)\\.class$")).match(QDir(path).dirName()).hasMatch())
            {
                result = path;
            }
        }
    }

    return result;
}
