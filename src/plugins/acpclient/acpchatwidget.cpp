// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acpchatwidget.h"
#include "acpchattab.h"
#include "acpclienttr.h"

#include <utils/documenttabbar.h>
#include <utils/styledbar.h>
#include <utils/utilsicons.h>

#include <QHBoxLayout>
// OPENMV-DIFF //
#include <QAbstractButton>
#include <QPainter>
// OPENMV-DIFF //
#include <QTabWidget>
// OPENMV-DIFF //
#include <QApplication>
#include <QStackedWidget>
#include <utils/theme/theme.h>
// OPENMV-DIFF //
#include <QToolButton>
#include <QVBoxLayout>

#include <coreplugin/rightpane.h>

namespace AcpClient::Internal {

// OPENMV-DIFF //
// class TabWidget : public QTabWidget
// {
// public:
//     TabWidget(QWidget *parent = nullptr)
//         : QTabWidget(parent)
//     {
//         setTabBar(new Utils::DocumentTabBar(this));
//     }
// };

// OpenMV IDE hosts the chat in its AI mode. The tabs take the editor tab
// bar's look (its stylesheet is applied by the TabbedEditor plugin by object
// name) and span the full width; the trailing "New Chat" tab is the new-tab
// control, so it has no close button and there is no toolbar.
class ChatTabBar : public Utils::DocumentTabBar
{
public:
    ChatTabBar(QWidget *parent = nullptr)
        : Utils::DocumentTabBar(parent)
    {
        setObjectName(QLatin1String("OpenMVStyledTabBar"));
        setDocumentMode(true);
        setTabsClosable(false); // close buttons are ours, per tab
        setMovable(true);
        setExpanding(false);
        setDrawBase(false);
        setUsesScrollButtons(false);
        setElideMode(Qt::ElideNone); // like the editor tabs: never truncate the name
        const QString tabStyle = qApp->property("openmv.tabBarStyleSheet").toString();
        if (!tabStyle.isEmpty())
            setStyleSheet(tabStyle);
    }

    QSize minimumSizeHint() const override
    {
        // Never let the tabs decide the chat's minimum width.
        return QSize(0, QTabBar::minimumSizeHint().height());
    }

    // Where the current tab is drawn. While a tab is dragged (and while it slides
    // back into place afterwards) QTabBar draws it with a temporary child widget
    // that it positions during its own paintEvent; that is the only child that is
    // not a button, so follow it. Otherwise it is the current tab's slot.
    QRect currentTabRect() const
    {
        for (QObject *child : children()) {
            QWidget *widget = qobject_cast<QWidget *>(child);
            if (widget && widget->isVisible() && !qobject_cast<QAbstractButton *>(widget))
                return widget->geometry();
        }
        return currentIndex() >= 0 ? tabRect(currentIndex()) : QRect();
    }

    // Like the editor tabs: a line along the bottom of the tab row, except
    // under the current tab, which opens straight into the chat below it.
    void paintEvent(QPaintEvent *event) override
    {
        Utils::DocumentTabBar::paintEvent(event);

        QPainter painter(this);
        painter.setPen(Utils::creatorColor(Utils::Theme::SplitterColor));
        const int y = height() - 1;
        const QRect current = currentTabRect();
        if (current.isNull()) {
            painter.drawLine(0, y, width() - 1, y);
        } else {
            if (current.left() > 0)
                painter.drawLine(0, y, current.left() - 1, y);
            if (current.right() < width() - 1)
                painter.drawLine(current.right() + 1, y, width() - 1, y);
        }
    }

    // The editor tab bar's close icon and hover tint, as a plain tool button.
    QToolButton *makeCloseButton()
    {
        auto *button = new QToolButton(this);
        const bool dark = Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface);
        button->setIcon(QIcon(dark ? QLatin1String(":/icons/close_button_light_grey.png")
                                   : QLatin1String(":/icons/close_button_dark.png")));
        button->setToolTip(Tr::tr("Close"));
        button->setAutoRaise(true);
        button->setCursor(Qt::ArrowCursor);
        // Same size as the editor tabs' close button: 20x20, the 16px icon unscaled.
        button->setFixedSize(20, 20);
        button->setIconSize(QSize(16, 16));
        // rgba(), not name(): the hover color is translucent, and name() drops the alpha.
        const QColor hover = Utils::creatorColor(Utils::Theme::BackgroundColorHover);
        button->setStyleSheet(QString::fromLatin1(
            "QToolButton { background: transparent; border: none; padding: 0px; }"
            "QToolButton:hover { background-color: rgba(%1, %2, %3, %4); }")
            .arg(hover.red()).arg(hover.green()).arg(hover.blue()).arg(hover.alpha()));
        connect(button, &QToolButton::clicked, this, [this, button] {
            for (int i = 0; i < count(); ++i) {
                if (tabButton(i, QTabBar::RightSide) == button) {
                    emit tabCloseRequested(i);
                    return;
                }
            }
        });
        return button;
    }
};

// Stands in for the QTabWidget: the bar above, the pages below, so the bar
// spans the full width like the editor's.
class ChatTabs : public QWidget
{
    Q_OBJECT
public:
    ChatTabs(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        m_bar = new ChatTabBar(this);
        m_stack = new QStackedWidget(this);
        // A blank bar above the tabs, like the editor's toolbar above its tabs
        // (and the bar above Preferences), so it lines up with the Frame Buffer bar.
        layout->addWidget(new Utils::StyledBar(this));
        layout->addWidget(m_bar);
        layout->addWidget(m_stack, 1);

        connect(m_bar, &QTabBar::currentChanged, m_stack, &QStackedWidget::setCurrentIndex);
        connect(m_bar, &QTabBar::tabCloseRequested, this, &ChatTabs::tabCloseRequested);
        connect(m_bar, &QTabBar::tabMoved, this, [this](int from, int to) {
            QWidget *w = m_stack->widget(from);
            m_stack->removeWidget(w);
            m_stack->insertWidget(to, w);
            m_stack->setCurrentIndex(m_bar->currentIndex());
        });
    }

    int addTab(QWidget *page, const QString &text)
    {
        m_stack->addWidget(page);
        return m_bar->addTab(text);
    }

    void removeTab(int index)
    {
        if (QWidget *w = m_stack->widget(index))
            m_stack->removeWidget(w);
        m_bar->removeTab(index);
    }

    void setTabClosable(int index, bool closable)
    {
        QWidget *current = m_bar->tabButton(index, QTabBar::RightSide);
        if (closable == (current != nullptr))
            return;
        m_bar->setTabButton(index, QTabBar::RightSide, closable ? m_bar->makeCloseButton() : nullptr);
        delete current;
    }

    int count() const { return m_bar->count(); }
    int indexOf(QWidget *page) const { return m_stack->indexOf(page); }
    QWidget *widget(int index) const { return m_stack->widget(index); }
    QWidget *currentWidget() const { return m_stack->currentWidget(); }
    void setCurrentIndex(int index) { m_bar->setCurrentIndex(index); }
    void setTabText(int index, const QString &text) { m_bar->setTabText(index, text); }

signals:
    void tabCloseRequested(int index);

private:
    ChatTabBar *m_bar = nullptr;
    QStackedWidget *m_stack = nullptr;
};
// OPENMV-DIFF //

AcpChatWidget::AcpChatWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // OPENMV-DIFF //
    // auto *toolBar = new Utils::StyledBar(this);
    // auto *toolBarLayout = new QHBoxLayout(toolBar);
    // toolBarLayout->setContentsMargins(0, 0, 0, 0);
    // toolBarLayout->setSpacing(0);
    // layout->addWidget(toolBar);
    //
    // auto *addButton = new QToolButton(toolBar);
    // addButton->setIcon(Utils::Icons::PLUS_TOOLBAR.icon());
    // addButton->setText(Tr::tr("Add Chat"));
    // addButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    // addButton->setToolTip(addButton->text());
    // connect(addButton, &QToolButton::clicked, this, [this] {
    //     AcpChatTab *tab = addNewTab();
    //     tab->setFocus();
    // });
    // toolBarLayout->addWidget(addButton);
    //
    // toolBarLayout->addStretch();
    //
    // auto *closeButton = new QToolButton(toolBar);
    // closeButton->setIcon(Utils::Icons::CLOSE_SPLIT_RIGHT.icon());
    // closeButton->setToolTip(Tr::tr("Close"));
    // connect(closeButton, &QToolButton::clicked, this, [] {
    //     Core::RightPaneWidget::instance()->setShown(false);
    // });
    // toolBarLayout->addWidget(closeButton);
    //
    // m_tabWidget = new TabWidget;
    // m_tabWidget->setTabBarAutoHide(false);
    // m_tabWidget->setDocumentMode(true);
    // m_tabWidget->setTabsClosable(true);
    // m_tabWidget->tabBar()->setShape(QTabBar::RoundedNorth);
    // m_tabWidget->setMovable(true);
    // layout->addWidget(m_tabWidget);
    //
    // connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, &AcpChatWidget::closeTab);
    m_tabWidget = new ChatTabs;
    layout->addWidget(m_tabWidget);

    connect(m_tabWidget, &ChatTabs::tabCloseRequested, this, &AcpChatWidget::closeTab);
    // OPENMV-DIFF //

    // Auto-create first tab
    addNewTab();
}

AcpChatWidget::~AcpChatWidget() = default;

void AcpChatWidget::setInspector(AcpInspector *inspector)
{
    m_inspector = inspector;
    for (int i = 0; i < m_tabWidget->count(); ++i) {
        if (auto *tab = qobject_cast<AcpChatTab *>(m_tabWidget->widget(i)))
            tab->setInspector(inspector);
    }
}

void AcpChatWidget::closeTab(int index)
{
    QWidget *w = m_tabWidget->widget(index);
    m_tabWidget->removeTab(index);
    delete w;
    emit navigateStateUpdate();

    // OPENMV-DIFF //
    // if (m_tabWidget->count() == 0) {
    //     addNewTab();
    //     Core::RightPaneWidget::instance()->setShown(false);
    // }
    updateTrailingTab();
    // OPENMV-DIFF //
}

void AcpChatWidget::setFocus()
{
    if (auto *tab = currentTab())
        tab->setFocus();
}

// OPENMV-DIFF //
// The last tab is always an unused "New Chat"; it cannot be closed, every
// other tab can. Called whenever a title or the tab set changes.
void AcpChatWidget::updateTrailingTab()
{
    const int n = m_tabWidget->count();
    auto *last = n > 0 ? qobject_cast<AcpChatTab *>(m_tabWidget->widget(n - 1)) : nullptr;
    if (!last || last->isInUse()) {
        // The user started the chat in the trailing tab: give them the next one
        // without switching to it.
        const int current = m_tabWidget->count() > 0 ? m_tabWidget->indexOf(m_tabWidget->currentWidget()) : -1;
        addNewTab();
        if (current >= 0)
            m_tabWidget->setCurrentIndex(current);
    }
    const int count = m_tabWidget->count();
    for (int i = 0; i < count; ++i)
        m_tabWidget->setTabClosable(i, i != count - 1);
}
// OPENMV-DIFF //

AcpChatTab *AcpChatWidget::addNewTab()
{
    auto *tab = new AcpChatTab(m_tabWidget);
    if (m_inspector)
        tab->setInspector(m_inspector);

    const int index = m_tabWidget->addTab(tab, tab->title());
    m_tabWidget->setCurrentIndex(index);

    connect(tab, &AcpChatTab::titleChanged, this, [this, tab] {
        const int i = m_tabWidget->indexOf(tab);
        if (i >= 0)
            m_tabWidget->setTabText(i, tab->title());
        updateTrailingTab(); // OPENMV-DIFF
    });

    emit navigateStateUpdate();
    // OPENMV-DIFF //
    m_tabWidget->setTabClosable(index, false);
    for (int i = 0; i < index; ++i)
        m_tabWidget->setTabClosable(i, true);
    // OPENMV-DIFF //
    return tab;
}

AcpChatTab *AcpChatWidget::currentTab() const
{
    return qobject_cast<AcpChatTab *>(m_tabWidget->currentWidget());
}

} // namespace AcpClient::Internal

// OPENMV-DIFF //
#include "acpchatwidget.moc"
// OPENMV-DIFF //
