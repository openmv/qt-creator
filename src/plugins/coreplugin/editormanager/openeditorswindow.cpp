// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "openeditorswindow.h"

#include "documentmodel.h"
#include "editormanager.h"
#include "editormanager_p.h"
#include "editorview.h"
#include "../coreplugintr.h"
#include "../idocument.h"

#include <utils/algorithm.h>
#include <utils/basetreeview.h>
#include <utils/fsengine/fileiconprovider.h>
#include <utils/hostosinfo.h>
#include <utils/qtcassert.h>
#include <utils/treemodel.h>
#include <utils/utilsicons.h>

#include <QFocusEvent>
#include <QHeaderView>
#include <QLoggingCategory>
#include <QScrollBar>
// OPENMV-DIFF //
#include <QTimer>
// OPENMV-DIFF //
#include <QVBoxLayout>

using namespace Utils;

Q_LOGGING_CATEGORY(openEditorsLog, "qtc.core.openeditorswindow", QtWarningMsg);

namespace Core::Internal {

class OpenEditorsItem : public TreeItem
{
public:
    QVariant data(int column, int role) const override
    {
        if (column != 0)
            return {};

        if (!entry)
            return {};

        if (role == Qt::DecorationRole) {
            return !entry->filePath().isEmpty() && entry->document->isFileReadOnly()
                  ? DocumentModel::lockedIcon() : FileIconProvider::icon(entry->filePath());
        }

        if (role == Qt::DisplayRole)  {
            QString title = entry->displayName();
            if (entry->document->isModified())
                title += Tr::tr("*");
            return title;
        }

        if (role == Qt::ToolTipRole)
            return entry->filePath().toUserOutput();

        return {};
    }

    DocumentModel::Entry *entry = nullptr;
    EditorView *view = nullptr;
};

static void selectEditor(OpenEditorsItem *item)
{
    if (!item)
        return;
    if (!EditorManagerPrivate::activateEditorForEntry(item->view, item->entry))
        delete item;
}

class OpenEditorsView : public QTreeView
{
public:
    OpenEditorsView()
    {
        m_model.setHeader({{}});
        setModel(&m_model);

        header()->hide();
        setIndentation(0);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setTextElideMode(Qt::ElideMiddle);
        if (Utils::HostOsInfo::isMacHost())
            setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
        setUniformRowHeights(true);
    }

    QSize sizeHint() const override
    {
        return QSize(sizeHintForColumn(0) + verticalScrollBar()->width() + frameWidth() * 2,
                     viewportSizeHint().height() + frameWidth() * 2);
    }

    OpenEditorsItem *currentItem() const
    {
        // OPENMV-DIFF //
        // Selection writes are deferred while the popup is hidden (macOS
        // Cocoa a11y crash class); honor the pending index so the quick
        // Ctrl+Tab path (select-and-hide without ever showing) still works.
        if (m_pendingCurrent.isValid())
            return m_model.itemForIndexAtLevel<1>(QModelIndex(m_pendingCurrent));
        // OPENMV-DIFF //
        QModelIndexList indexes = selectedIndexes();
        return indexes.size() == 1 ? m_model.itemForIndexAtLevel<1>(indexes.front()) : nullptr;
    }

    int currentRow() const
    {
        // OPENMV-DIFF //
        if (m_pendingCurrent.isValid())
            return m_pendingCurrent.row();
        // OPENMV-DIFF //
        QModelIndexList indexes = selectedIndexes();
        return indexes.size() == 1 ? indexes.front().row() : -1;
    }

    // OPENMV-DIFF //
    // Selecting on the hidden popup view trips the macOS Cocoa a11y bridge
    // (NSRangeException); stash the index while hidden and replay it after
    // the popup is shown (see OpenEditorsWindow::setVisible).
    void setCurrentIndexGuarded(const QModelIndex &idx)
    {
        if (isVisible()) {
            m_pendingCurrent = QPersistentModelIndex();
            setCurrentIndex(idx);
            scrollTo(idx, QAbstractItemView::PositionAtCenter);
        } else {
            m_pendingCurrent = QPersistentModelIndex(idx);
        }
    }

    void applyPendingSelection()
    {
        if (m_pendingCurrent.isValid()) {
            const QModelIndex idx = m_pendingCurrent;
            m_pendingCurrent = QPersistentModelIndex();
            setCurrentIndex(idx);
            scrollTo(idx, QAbstractItemView::PositionAtCenter);
        }
    }
    // OPENMV-DIFF //

    void mouseReleaseEvent(QMouseEvent *ev) override
    {
        QPoint pos = ev->pos();
        QModelIndex idx = indexAt(pos);
        if (OpenEditorsItem *item = m_model.itemForIndexAtLevel<1>(idx))
            selectEditor(item);
        setFocus();
    }

    void addHistoryItems(const QList<EditLocation> &history, EditorView *view,
                         QSet<const DocumentModel::Entry *> &entriesDone);

    void addRemainingItems(EditorView *view,
                           QSet<const DocumentModel::Entry *> &entriesDone);

    void addItem(DocumentModel::Entry *entry, QSet<const DocumentModel::Entry *> &entriesDone,
                 EditorView *view);

    void selectUpDown(bool up);

    TreeModel<TreeItem, OpenEditorsItem> m_model;
    // OPENMV-DIFF //
    QPersistentModelIndex m_pendingCurrent;
    // OPENMV-DIFF //
};

OpenEditorsWindow::OpenEditorsWindow(QWidget *parent)
    : QFrame(parent, Qt::Popup)
{
    m_editorView = new OpenEditorsView;

    setMinimumSize(300, 200);
    m_editorView->installEventFilter(this);

    // We disable the frame on this list view and use a QFrame around it instead.
    // This improves the look with QGTKStyle.
    if (!Utils::HostOsInfo::isMacHost())
        setFrameStyle(m_editorView->frameStyle());
    m_editorView->setFrameStyle(QFrame::NoFrame);

    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_editorView);
}

void OpenEditorsWindow::selectAndHide()
{
    setVisible(false);
    selectEditor(m_editorView->currentItem());
}

void OpenEditorsWindow::setVisible(bool visible)
{
    QWidget::setVisible(visible);
    // OPENMV-DIFF //
    // if (visible)
    //     setFocus();
    // Deferred: focusing the view mid-popup-activation auto-assigns a
    // current index and trips the macOS Cocoa a11y bridge. Replay the
    // deferred selection first so the focus-in finds a valid current index.
    if (visible) {
        QTimer::singleShot(0, this, [this] {
            if (!isVisible())
                return;
            m_editorView->applyPendingSelection();
            setFocus();
        });
    }
    // OPENMV-DIFF //
}

bool OpenEditorsWindow::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_editorView) {
        if (e->type() == QEvent::ShortcutOverride) {
            auto ke = static_cast<QKeyEvent*>(e);
            switch (ke->key()) {
            case Qt::Key_Up:
            case Qt::Key_P:
                e->accept();
                return true;
            case Qt::Key_Down:
            case Qt::Key_N:
                e->accept();
                return true;
            }
        }
        if (e->type() == QEvent::KeyPress) {
            auto ke = static_cast<QKeyEvent*>(e);
            switch (ke->key()) {
            case Qt::Key_Up:
            case Qt::Key_P:
                selectNextEditor();
                return true;
            case Qt::Key_Down:
            case Qt::Key_N:
                selectPreviousEditor();
                return true;
            case Qt::Key_Escape:
                setVisible(false);
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                selectEditor(m_editorView->currentItem());
                return true;
            }

        } else if (e->type() == QEvent::KeyRelease) {
            auto ke = static_cast<QKeyEvent*>(e);
            qCDebug(openEditorsLog()) << ke;
            if (ke->modifiers() == 0
                /* On some platforms, the key event can claim both that Ctrl is released and that
                  Ctrl is still pressed, see QTCREATORBUG-31228 */
                || (ke->modifiers() == Qt::ControlModifier && ke->key() == Qt::Key_Control)
                /*HACK this is to overcome some event inconsistencies between platforms*/
                || (ke->modifiers() == Qt::AltModifier
                    && (ke->key() == Qt::Key_Alt || ke->key() == -1))) {
                selectAndHide();
            }
        }
    }
    return QWidget::eventFilter(obj, e);
}

void OpenEditorsWindow::focusInEvent(QFocusEvent *)
{
    m_editorView->setFocus();
}

void OpenEditorsView::selectUpDown(bool up)
{
    int itemCount = m_model.rootItem()->childCount();
    if (itemCount < 2)
        return;
    int index = currentRow();
    if (index < 0)
        return;
    TreeItem *editor = nullptr;
    int count = 0;
    while (!editor && count < itemCount) {
        if (up) {
            index--;
            if (index < 0)
                index = itemCount-1;
        } else {
            index++;
            if (index >= itemCount)
                index = 0;
        }
        editor = m_model.rootItem()->childAt(index);
        count++;
    }
    if (editor) {
        // OPENMV-DIFF //
        // setCurrentIndex(m_model.index(index, 0));
        // scrollTo(currentIndex(), QAbstractItemView::PositionAtCenter);
        setCurrentIndexGuarded(m_model.index(index, 0));
        // OPENMV-DIFF //
    }
}

void OpenEditorsWindow::selectPreviousEditor()
{
    m_editorView->selectUpDown(false);
}

void OpenEditorsWindow::selectNextEditor()
{
    m_editorView->selectUpDown(true);
}

QSize OpenEditorsWindow::sizeHint() const
{
    return m_editorView->sizeHint() + QSize(frameWidth() * 2, frameWidth() * 2);
}

void OpenEditorsWindow::setEditors(const QList<EditLocation> &globalHistory, EditorView *view)
{
    m_editorView->m_model.clear();

    QSet<const DocumentModel::Entry *> entriesDone;
    m_editorView->addHistoryItems(view->editorHistory(), view, entriesDone);

    // add missing editors from the global history
    m_editorView->addHistoryItems(globalHistory, view, entriesDone);

    // add purely suspended editors which are not initialised yet
    m_editorView->addRemainingItems(view, entriesDone);
}

static DocumentModel::Entry *entryForEditLocation(const EditLocation &item)
{
    if (!item.document.isNull())
        return DocumentModel::entryForDocument(item.document);
    return DocumentModel::entryForFilePath(item.filePath);
}

void OpenEditorsView::addHistoryItems(const QList<EditLocation> &history, EditorView *view,
                                        QSet<const DocumentModel::Entry *> &entriesDone)
{
    for (const EditLocation &hi : history) {
        if (DocumentModel::Entry *entry = entryForEditLocation(hi))
            addItem(entry, entriesDone, view);
    }
}

void OpenEditorsView::addRemainingItems(EditorView *view,
                                        QSet<const DocumentModel::Entry *> &entriesDone)
{
    const QList<DocumentModel::Entry *> entries = DocumentModel::entries();
    for (DocumentModel::Entry *entry : entries)
        addItem(entry, entriesDone, view);
}

void OpenEditorsView::addItem(DocumentModel::Entry *entry,
                                QSet<const DocumentModel::Entry *> &entriesDone,
                                EditorView *view)
{
    if (!Utils::insert(entriesDone, entry))
        return;

    auto item = new OpenEditorsItem;
    item->entry = entry;
    item->view = view;
    m_model.rootItem()->appendChild(item);

    if (m_model.rootItem()->childCount() == 1) {
        // OPENMV-DIFF //
        // setCurrentIndex(m_model.index(0, 0));
        setCurrentIndexGuarded(m_model.index(0, 0));
        // OPENMV-DIFF //
    }
}

} // Core::Internal
