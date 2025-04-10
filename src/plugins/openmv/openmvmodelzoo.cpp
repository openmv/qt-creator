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

#include "openmvplugin.h"
#include "openmvtr.h"
#include "openmvpluginconnect.h"

#include "openmvmodelzoo.h"

#define LAST_MODEL_ZOO_DIALOG_GEOMETRY "OpenMVModelZooDialogGeometry"
#define LAST_MODEL_ZOO_DIALOG_SPLITTER_STATE "OpenMVModelZooDialogSplitterState"
#define LAST_MODEL_ZOO_DIALOG_EXPANDED_STATE "OpenMVModelZooDialogExpandedState"
#define LAST_MODEL_ZOO_DIALOG_SELECTED_INDEX "OpenMVModelZooDialogSelectedIndex"

namespace OpenMV {
namespace Internal {

OpenMVModelZooBrowser::OpenMVModelZooBrowser(Utils::QtcSettings *settings, QWidget *parent, bool saveDialog) :
    QDialog(parent), m_settings(settings), m_model(new QFileSystemModel(this))
{
    setWindowFlags(windowFlags() | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint));
    setWindowTitle(Tr::tr("Model Zoo"));
    setMinimumSize(QSize(480, 480));

    m_splitter = new Core::MiniSplitter(Qt::Horizontal, this);

    m_filter = new OpenMVModelZooBrowserFilter(this);
    m_filter->setSourceModel(m_model);

    m_treeView = new OpenMVModelZooBrowserTreeView(this);
    Utils::FilePath path = Core::ICore::userResourcePath(QStringLiteral("models"));
    m_model->setRootPath(path.toString());
    m_treeView->setModel(m_filter);
    m_treeView->setRootIndex(m_filter->mapFromSource(m_model->index(path.toString())));
    m_treeView->setContextMenuPolicy(Qt::DefaultContextMenu);
    m_treeView->setHeaderHidden(true);
    m_treeView->setColumnHidden(1, true); // Size
    m_treeView->setColumnHidden(2, true); // Type
    m_treeView->setColumnHidden(3, true); // DateModified
    m_splitter->addWidget(m_treeView);

    QTextBrowser *textBrowser = new QTextBrowser(this);
    textBrowser->setOpenExternalLinks(true);
    m_splitter->addWidget(textBrowser);

    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);

    QVBoxLayout *vlayout = new QVBoxLayout(this);
    vlayout->addWidget(m_splitter);

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *ok = new QPushButton(saveDialog ? Tr::tr("Copy") : Tr::tr("OK"));
    box->addButton(ok, QDialogButtonBox::AcceptRole);
    ok->setEnabled(false);
    connect(box, &QDialogButtonBox::accepted, this, &OpenMVModelZooBrowser::accept);
    connect(box, &QDialogButtonBox::rejected, this, &OpenMVModelZooBrowser::reject);
    vlayout->addWidget(box);

    if(m_settings->contains(LAST_MODEL_ZOO_DIALOG_GEOMETRY))
    {
        restoreGeometry(m_settings->value(LAST_MODEL_ZOO_DIALOG_GEOMETRY).toByteArray());
        m_splitter->restoreState(m_settings->value(LAST_MODEL_ZOO_DIALOG_SPLITTER_STATE).toByteArray());

        m_listToExpand = m_settings->value(LAST_MODEL_ZOO_DIALOG_EXPANDED_STATE).toStringList();

        connect(m_model, &QFileSystemModel::directoryLoaded, this, [this] () {
            if (!m_listToExpand.isEmpty())
            {
                restoreExpandedState(QString(), m_treeView->rootIndex());
            }
        });

        connect(m_treeView, &OpenMVModelZooBrowserTreeView::paintEventSignal, this, [this] () {
            if (m_listToExpand.isEmpty() && (!m_initialized) && m_settings->contains(LAST_MODEL_ZOO_DIALOG_SELECTED_INDEX))
            {
                QTimer::singleShot(1, this, [this] () {
                    QModelIndex index = m_filter->mapFromSource(m_model->index(m_settings->value(LAST_MODEL_ZOO_DIALOG_SELECTED_INDEX).toString()));
                    m_treeView->setCurrentIndex(index);
                    m_treeView->scrollTo(index, QTreeView::PositionAtCenter);
                });

                m_initialized = true;
            }
        });
    }
    else
    {
        resize(800, 600);
        m_splitter->setSizes(QList<int>() << 320 << 480);
    }

    m_selectedModel = QString();
    m_initialized = false;

#ifndef Q_OS_MAC
    m_styleSheet = QStringLiteral( // https://doc.qt.io/qt-5/stylesheet-examples.html#customizing-qtreeview
    "QTreeView::branch:has-children:!has-siblings:closed,QTreeView::branch:closed:has-children:has-siblings{border-image:none;image:url(:/core/images/branch-closed-%1.png);}"
    "QTreeView::branch:open:has-children:!has-siblings,QTreeView::branch:open:has-children:has-siblings{border-image:none;image:url(:/core/images/branch-open-%1.png);}"
    ).arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("dark") : QStringLiteral("light"));
#endif

    m_highDPIStyleSheet = QString(m_styleSheet).replace(QStringLiteral(".png"), QStringLiteral("_2x.png"));
    m_devicePixelRatio = 0;

    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged, this, [this, ok, textBrowser](const QItemSelection &selected, const QItemSelection &deselected) {
        QModelIndexList indexes = selected.indexes();
        Q_UNUSED(deselected)

        if (indexes.isEmpty())
        {
            return;
        }

        QString path = m_model->filePath(m_filter->mapToSource(indexes.first()));

        if (QFileInfo(path).isDir())
        {
            m_selectedModel = QString();
            ok->setEnabled(false);

            do
            {
                QString indexPath = path + QDir::separator() + QStringLiteral("index.html");

                if (QFileInfo(indexPath).exists())
                {
                    QFile file(indexPath);

                    if(file.open(QIODevice::ReadOnly))
                    {
                        textBrowser->setSearchPaths(QStringList() << QFileInfo(indexPath).path());
                        textBrowser->setHtml(QString::fromUtf8(file.readAll()));
                        file.close();
                    }

                    break;
                }

                path = QFileInfo(path).path();
            }
            while (Utils::FilePath::fromString(path).isChildOf(Core::ICore::userResourcePath(QStringLiteral("models"))));
        }
        else
        {
            m_selectedModel = path;
            ok->setEnabled(true);

            do
            {
                path = QFileInfo(path).path();

                QString indexPath = path + QDir::separator() + QStringLiteral("index.html");

                if (QFileInfo(indexPath).exists())
                {
                    QFile file(indexPath);

                    if(file.open(QIODevice::ReadOnly))
                    {
                        textBrowser->setSearchPaths(QStringList() << QFileInfo(indexPath).path());
                        textBrowser->setHtml(QString::fromUtf8(file.readAll()));
                        file.close();
                    }

                    break;
                }
            }
            while (Utils::FilePath::fromString(path).isChildOf(Core::ICore::userResourcePath(QStringLiteral("models"))));
        }
    });

    QString indexPath = path.toString() + QDir::separator() + QStringLiteral("index.html");

    if (QFileInfo(indexPath).exists())
    {
        QFile file(indexPath);

        if(file.open(QIODevice::ReadOnly))
        {
            textBrowser->setSearchPaths(QStringList() << QFileInfo(indexPath).path());
            textBrowser->setHtml(QString::fromUtf8(file.readAll()));
            file.close();
        }
    }

    connect(m_treeView, &OpenMVModelZooBrowserTreeView::selectionCleared, this, [this, path, textBrowser]() {
        QString indexPath = path.toString() + QDir::separator() + QStringLiteral("index.html");

        if (QFileInfo(indexPath).exists())
        {
            QFile file(indexPath);

            if(file.open(QIODevice::ReadOnly))
            {
                textBrowser->setSearchPaths(QStringList() << QFileInfo(indexPath).path());
                textBrowser->setHtml(QString::fromUtf8(file.readAll()));
                file.close();
            }
        }
    });

    connect(m_treeView, &OpenMVModelZooBrowserTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        QString path = m_model->filePath(m_filter->mapToSource(index));

        if (QFileInfo(path).isFile())
        {
            m_selectedModel = path;
            accept();
        }
    });
}

void OpenMVModelZooBrowser::saveExpandedState(const QString &path, QStringList &list, const QModelIndex &index)
{
    for (int row = 0; row < m_filter->rowCount(index); row++)
    {
        QModelIndex child = m_filter->index(row, 0, index);
        QString childPath = path + QDir::separator() + child.data().toString();

        if (m_treeView->isExpanded(child))
        {
            list.append(childPath);
        }

        saveExpandedState(childPath, list, child);
    }
}

void OpenMVModelZooBrowser::restoreExpandedState(const QString &path, const QModelIndex &index)
{
    for (int row = 0; row < m_filter->rowCount(index); row++)
    {
        QModelIndex child = m_filter->index(row, 0, index);
        QString childPath = path + QDir::separator() + child.data().toString();

        if (m_listToExpand.contains(childPath))
        {
            m_treeView->setExpanded(child, true);
            m_listToExpand.removeOne(childPath);
        }

        restoreExpandedState(childPath, child);
    }
}

OpenMVModelZooBrowser::~OpenMVModelZooBrowser()
{
    m_settings->setValue(LAST_MODEL_ZOO_DIALOG_GEOMETRY, saveGeometry());
    m_settings->setValue(LAST_MODEL_ZOO_DIALOG_SPLITTER_STATE, m_splitter->saveState());

    QStringList list;
    saveExpandedState(QString(), list, m_treeView->rootIndex());
    m_settings->setValue(LAST_MODEL_ZOO_DIALOG_EXPANDED_STATE, list);

    if (m_treeView->selectionModel()->hasSelection())
    {
        m_settings->setValue(LAST_MODEL_ZOO_DIALOG_SELECTED_INDEX, m_model->filePath(m_filter->mapToSource(m_treeView->currentIndex())));
    }
    else
    {
        m_settings->remove(LAST_MODEL_ZOO_DIALOG_SELECTED_INDEX);
    }
}

// We have to do this because Qt does not update the icons when switching between
// a non-high dpi screen and a high-dpi screen.
void OpenMVModelZooBrowser::paintEvent(QPaintEvent *event)
{
    qreal ratio = devicePixelRatioF();
    if (!qFuzzyCompare(ratio, m_devicePixelRatio))
    {
        m_devicePixelRatio = ratio;
        setStyleSheet(qFuzzyCompare(1.0, ratio) ? m_styleSheet : m_highDPIStyleSheet); // reload icons
    }

    QDialog::paintEvent(event);
}

} // namespace Internal
} // namespace OpenMV
