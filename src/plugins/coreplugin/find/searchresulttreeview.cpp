// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "searchresulttreeview.h"
#include "searchresulttreeitemroles.h"
#include "searchresulttreemodel.h"
#include "searchresulttreeitemdelegate.h"

#include <utils/qtcassert.h>

#include <QHeaderView>
#include <QKeyEvent>
#include <QVBoxLayout>

// OPENMV-DIFF //
#include <utils/theme/theme.h>
// OPENMV-DIFF //

namespace Core {
namespace Internal {

class FilterWidget : public QWidget
{
public:
    FilterWidget(QWidget *parent, QWidget *content) : QWidget(parent, Qt::Popup)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        const auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(2, 2, 2, 2);
        layout->setSpacing(2);
        layout->addWidget(content);
        setLayout(layout);
        move(parent->mapToGlobal(QPoint(0, -sizeHint().height())));
    }
};

SearchResultTreeView::SearchResultTreeView(QWidget *parent)
    : Utils::TreeView(parent)
    , m_model(new SearchResultFilterModel(this))
    , m_autoExpandResults(false)
{
    setModel(m_model);
    connect(m_model, &SearchResultFilterModel::filterInvalidated,
            this, &SearchResultTreeView::filterInvalidated);

    setItemDelegate(new SearchResultTreeItemDelegate(8, this));
    setIndentation(14);
    setUniformRowHeights(true);
    setExpandsOnDoubleClick(true);
    header()->setSectionResizeMode(QHeaderView::ResizeToContents);
    header()->setStretchLastSection(false);
    header()->hide();

    connect(this, &SearchResultTreeView::activated,
            this, &SearchResultTreeView::emitJumpToSearchResult);

    // OPENMV-DIFF //
    m_styleSheet = QStringLiteral( // https://doc.qt.io/qt-5/stylesheet-examples.html#customizing-qtreeview
#ifndef Q_OS_MAC
    "QTreeView::branch:has-children:!has-siblings:closed,QTreeView::branch:closed:has-children:has-siblings{border-image:none;image:url(:/core/images/branch-closed-%1.png);}"
    "QTreeView::branch:open:has-children:!has-siblings,QTreeView::branch:open:has-children:has-siblings{border-image:none;image:url(:/core/images/branch-open-%1.png);}"
#endif
    ).arg(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface) ? QStringLiteral("dark") : QStringLiteral("light"));

    m_highDPIStyleSheet = QString(m_styleSheet).replace(QStringLiteral(".png"), QStringLiteral("_2x.png"));

    m_devicePixelRatio = 0;
    // OPENMV-DIFF //
}

void SearchResultTreeView::setAutoExpandResults(bool expand)
{
    m_autoExpandResults = expand;
}

void SearchResultTreeView::setTextEditorFont(const QFont &font, const SearchResultColors &colors)
{
    m_model->setTextEditorFont(font, colors);

    QPalette p;
    p.setColor(QPalette::Base, colors.value(SearchResultColor::Style::Default).textBackground);
    setPalette(p);
}

void SearchResultTreeView::clear()
{
    m_model->clear();
}

void SearchResultTreeView::addResults(const QList<SearchResultItem> &items, SearchResult::AddMode mode)
{
    const QList<QModelIndex> addedParents = m_model->addResults(items, mode);
    if (m_autoExpandResults && !addedParents.isEmpty()) {
        for (const QModelIndex &index : addedParents)
            setExpanded(index, true);
    }
}

void SearchResultTreeView::setFilter(SearchResultFilter *filter)
{
    m_filter = filter;
    if (m_filter)
        m_filter->setParent(this);
    m_model->setFilter(filter);
    emit filterChanged();
}

bool SearchResultTreeView::hasFilter() const
{
    return m_filter;
}

void SearchResultTreeView::showFilterWidget(QWidget *parent)
{
    QTC_ASSERT(hasFilter(), return);
    const auto optionsWidget = new FilterWidget(parent, m_filter->createWidget());
    optionsWidget->show();
}

void SearchResultTreeView::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Return
            || event->key() == Qt::Key_Enter)
            && event->modifiers() == 0
            && currentIndex().isValid()
            && state() != QAbstractItemView::EditingState) {
        const SearchResultItem item
            = model()->data(currentIndex(), ItemDataRoles::ResultItemRole).value<SearchResultItem>();
        emit jumpToSearchResult(item);
        return;
    }
    TreeView::keyPressEvent(event);
}

bool SearchResultTreeView::event(QEvent *e)
{
    if (e->type() == QEvent::Resize)
        header()->setMinimumSectionSize(width());
    return TreeView::event(e);
}

void SearchResultTreeView::emitJumpToSearchResult(const QModelIndex &index)
{
    if (model()->data(index, ItemDataRoles::IsGeneratedRole).toBool())
        return;
    SearchResultItem item = model()->data(index, ItemDataRoles::ResultItemRole).value<SearchResultItem>();

    emit jumpToSearchResult(item);
}

void SearchResultTreeView::setTabWidth(int tabWidth)
{
    auto delegate = static_cast<SearchResultTreeItemDelegate *>(itemDelegate());
    delegate->setTabWidth(tabWidth);
    doItemsLayout();
}

SearchResultFilterModel *SearchResultTreeView::model() const
{
    return m_model;
}

// OPENMV-DIFF //
// We have to do this because Qt does not update the icons when switching between
// a non-high dpi screen and a high-dpi screen.
void SearchResultTreeView::paintEvent(QPaintEvent *event)
{
    qreal ratio = devicePixelRatioF();
    if (!qFuzzyCompare(ratio, m_devicePixelRatio))
    {
        m_devicePixelRatio = ratio;
        setStyleSheet(qFuzzyCompare(1.0, ratio) ? m_styleSheet : m_highDPIStyleSheet); // reload icons
    }

    Utils::TreeView::paintEvent(event);
}
// OPENMV-DIFF //

} // namespace Internal
} // namespace Core
