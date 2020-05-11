/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#include "fancytabwidget.h"
#include "fancyactionbar.h"

#include <utils/hostosinfo.h>
#include <utils/stylehelper.h>
#include <utils/styledbar.h>
#include <utils/theme/theme.h>

#include <QDebug>

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMouseEvent>
#include <QStyleFactory>
#include <QPainter>
#include <QPixmapCache>
#include <QStackedLayout>
#include <QStatusBar>
#include <QToolTip>

using namespace Core;
using namespace Internal;
using namespace Utils;

const int FancyTabBar::m_rounding = 22;
const int FancyTabBar::m_textPadding = 4;

void FancyTab::fadeIn()
{
    animator.stop();
    animator.setDuration(80);
    animator.setEndValue(1);
    animator.start();
}

void FancyTab::fadeOut()
{
    animator.stop();
    animator.setDuration(160);
    animator.setEndValue(0);
    animator.start();
}

void FancyTab::setFader(float value)
{
    m_fader = value;
    tabbar->update();
}

FancyTabBar::FancyTabBar(QWidget *parent)
    : QWidget(parent)
{
    m_hoverIndex = -1;
    m_currentIndex = -1;
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    setStyle(QStyleFactory::create(QLatin1String("windows")));
    setMinimumWidth(qMax(2 * m_rounding, 40));
    setAttribute(Qt::WA_Hover, true);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true); // Needed for hover events
    m_triggerTimer.setSingleShot(true);

    // We use a zerotimer to keep the sidebar responsive
    connect(&m_triggerTimer, &QTimer::timeout, this, &FancyTabBar::emitCurrentIndex);
}

FancyTabBar::~FancyTabBar()
{
    delete style();
}

QSize FancyTabBar::tabSizeHint(bool minimum) const
{
    QFont boldFont(font());
    boldFont.setPointSizeF(StyleHelper::sidebarFontSize());
    boldFont.setBold(true);
    QFontMetrics fm(boldFont);
    int spacing = 8;
    int width = 60 + spacing + 2;
    int maxLabelwidth = 0;
    for (int tab=0 ; tab<count() ;++tab) {
        int width = fm.width(tabText(tab));
        if (width > maxLabelwidth)
            maxLabelwidth = width;
    }
    int iconHeight = minimum ? 0 : 32;
    return QSize(qMax(width, maxLabelwidth + 4), iconHeight + spacing + fm.height());
}

void FancyTabBar::paintEvent(QPaintEvent *event)
{
    QPainter p(this);
    if (creatorTheme()->widgetStyle() == Theme::StyleFlat) {
        // draw background of upper part of left tab widget
        // (Welcome, ... Help)
        p.fillRect(event->rect(), StyleHelper::baseColor());
    }

    for (int i = 0; i < count(); ++i)
        if (i != currentIndex())
            paintTab(&p, i);

    // paint active tab last, since it overlaps the neighbors
    if (currentIndex() != -1)
        paintTab(&p, currentIndex());
}

// Handle hover events for mouse fade ins
void FancyTabBar::mouseMoveEvent(QMouseEvent *e)
{
    int newHover = -1;
    for (int i = 0; i < count(); ++i) {
        QRect area = tabRect(i);
        if (area.contains(e->pos())) {
            newHover = i;
            break;
        }
    }
    if (newHover == m_hoverIndex)
        return;

    if (validIndex(m_hoverIndex))
        m_tabs[m_hoverIndex]->fadeOut();

    m_hoverIndex = newHover;

    if (validIndex(m_hoverIndex)) {
        m_tabs[m_hoverIndex]->fadeIn();
        m_hoverRect = tabRect(m_hoverIndex);
    }
}

bool FancyTabBar::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip) {
        if (validIndex(m_hoverIndex)) {
            QString tt = tabToolTip(m_hoverIndex);
            if (!tt.isEmpty()) {
                QToolTip::showText(static_cast<QHelpEvent*>(event)->globalPos(), tt, this);
                return true;
            }
        }
    }
    return QWidget::event(event);
}

// Resets hover animation on mouse enter
void FancyTabBar::enterEvent(QEvent *e)
{
    Q_UNUSED(e)
    m_hoverRect = QRect();
    m_hoverIndex = -1;
}

// Resets hover animation on mouse enter
void FancyTabBar::leaveEvent(QEvent *e)
{
    Q_UNUSED(e)
    m_hoverIndex = -1;
    m_hoverRect = QRect();
    for (int i = 0 ; i < m_tabs.count() ; ++i) {
        m_tabs[i]->fadeOut();
    }
}

QSize FancyTabBar::sizeHint() const
{
    QSize sh = tabSizeHint();
    return QSize(sh.width(), sh.height() * m_tabs.count());
}

QSize FancyTabBar::minimumSizeHint() const
{
    QSize sh = tabSizeHint(true);
    return QSize(sh.width(), sh.height() * m_tabs.count());
}

QRect FancyTabBar::tabRect(int index) const
{
    QSize sh = tabSizeHint();

    if (sh.height() * m_tabs.count() > height())
        sh.setHeight(height() / m_tabs.count());

    return QRect(0, index * sh.height(), sh.width(), sh.height());

}

// This keeps the sidebar responsive since
// we get a repaint before loading the
// mode itself
void FancyTabBar::emitCurrentIndex()
{
    emit currentChanged(m_currentIndex);
}

void FancyTabBar::mousePressEvent(QMouseEvent *e)
{
    e->accept();
    for (int index = 0; index < m_tabs.count(); ++index) {
        if (tabRect(index).contains(e->pos())) {

            if (isTabEnabled(index)) {
                m_currentIndex = index;
                update();
                m_triggerTimer.start(0);
            }
            break;
        }
    }
}

static void paintSelectedTabBackground(QPainter *painter, const QRect &spanRect)
{
    const int verticalOverlap = 2; // Grows up and down for the overlaps
    const int dpr = painter->device()->devicePixelRatio();
    const QString cacheKey = QLatin1String(Q_FUNC_INFO) + QString::number(spanRect.width())
            + QLatin1Char('x') + QString::number(spanRect.height())
            + QLatin1Char('@') + QString::number(dpr);
    QPixmap selection;
    if (!QPixmapCache::find(cacheKey, &selection)) {
        selection = QPixmap(QSize(spanRect.width(), spanRect.height() + 2 * verticalOverlap) * dpr);
        selection.fill(Qt::transparent);
        selection.setDevicePixelRatio(dpr);
        QPainter p(&selection);
        p.translate(QPoint(0, verticalOverlap));

        const QRect rect(QPoint(), spanRect.size());
        const QRectF borderRect = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);

        //background
        p.save();
        QLinearGradient grad(rect.topLeft(), rect.topRight());
        grad.setColorAt(0, QColor(255, 255, 255, 140));
        grad.setColorAt(1, QColor(255, 255, 255, 210));
        p.fillRect(rect, grad);
        p.restore();

        //shadows
        p.setPen(QColor(0, 0, 0, 110));
        p.drawLine(borderRect.topLeft() + QPointF(1, -1), borderRect.topRight() - QPointF(0, 1));
        p.drawLine(borderRect.bottomLeft(), borderRect.bottomRight());
        p.setPen(QColor(0, 0, 0, 40));
        p.drawLine(borderRect.topLeft(), borderRect.bottomLeft());

        //highlights
        p.setPen(QColor(255, 255, 255, 50));
        p.drawLine(borderRect.topLeft() + QPointF(0, -2), borderRect.topRight() - QPointF(0, 2));
        p.drawLine(borderRect.bottomLeft() + QPointF(0, 1), borderRect.bottomRight() + QPointF(0, 1));
        p.setPen(QColor(255, 255, 255, 40));
        p.drawLine(borderRect.topLeft() + QPointF(0, 0), borderRect.topRight());
        p.drawLine(borderRect.topRight() + QPointF(0, 1), borderRect.bottomRight() - QPointF(0, 1));
        p.drawLine(borderRect.bottomLeft() + QPointF(0, -1), borderRect.bottomRight() - QPointF(0, 1));

        QPixmapCache::insert(cacheKey, selection);
    }
    painter->drawPixmap(spanRect.topLeft() + QPoint(0, -verticalOverlap), selection);
}

void FancyTabBar::paintTab(QPainter *painter, int tabIndex) const
{
    if (!validIndex(tabIndex)) {
        qWarning("invalid index");
        return;
    }
    painter->save();

    QRect rect = tabRect(tabIndex);
    bool selected = (tabIndex == m_currentIndex);
    bool enabled = isTabEnabled(tabIndex);

    if (selected) {
        if (creatorTheme()->widgetStyle() == Theme::StyleFlat) {
          // background color of a fancy tab that is active
          painter->fillRect(rect, creatorTheme()->color(Theme::FancyToolButtonSelectedColor));
        } else {
            paintSelectedTabBackground(painter, rect);
        }
    }

    QString tabText(this->tabText(tabIndex));
    QRect tabTextRect(rect);
    const bool drawIcon = rect.height() > 36;
    QRect tabIconRect(tabTextRect);
    tabTextRect.translate(0, drawIcon ? -2 : 1);
    QFont boldFont(painter->font());
    boldFont.setPointSizeF(StyleHelper::sidebarFontSize());
    boldFont.setBold(true);
    painter->setFont(boldFont);
    painter->setPen(selected ? QColor(255, 255, 255, 160) : QColor(0, 0, 0, 110));
    const int textFlags = Qt::AlignCenter | (drawIcon ? Qt::AlignBottom : Qt::AlignVCenter) | Qt::TextWordWrap;

    const float fader = m_tabs[tabIndex]->fader();
    if (fader > 0 && !HostOsInfo::isMacHost() && !selected && enabled) {
        painter->save();
        painter->setOpacity(fader);
        if (creatorTheme()->widgetStyle() == Theme::StyleFlat)
            painter->fillRect(rect, creatorTheme()->color(Theme::FancyToolButtonHoverColor));
        else
            FancyToolButton::hoverOverlay(painter, rect);
        painter->restore();
    }

    if (!enabled && creatorTheme()->widgetStyle() == Theme::StyleDefault)
        painter->setOpacity(0.7);

    if (drawIcon) {
        int textHeight = painter->fontMetrics().boundingRect(QRect(0, 0, width(), height()), Qt::TextWordWrap, tabText).height();
        tabIconRect.adjust(0, 4, 0, -textHeight);
        const QIcon::Mode iconMode = enabled ? (selected ? QIcon::Active : QIcon::Normal)
                                             : QIcon::Disabled;
        StyleHelper::drawIconWithShadow(tabIcon(tabIndex), tabIconRect, painter, iconMode);
    }

    painter->setOpacity(1.0); //FIXME: was 0.7 before?
    if (enabled) {
        painter->setPen(selected
          ? creatorTheme()->color(Theme::FancyTabWidgetEnabledSelectedTextColor)
          : creatorTheme()->color(Theme::FancyTabWidgetEnabledUnselectedTextColor));
    } else {
        painter->setPen(selected
          ? creatorTheme()->color(Theme::FancyTabWidgetDisabledSelectedTextColor)
          : creatorTheme()->color(Theme::FancyTabWidgetDisabledUnselectedTextColor));
    }
    painter->translate(0, -1);
    painter->drawText(tabTextRect, textFlags, tabText);

    painter->restore();
}

void FancyTabBar::setCurrentIndex(int index) {
    if (isTabEnabled(index)) {
        m_currentIndex = index;
        update();
        emit currentChanged(m_currentIndex);
    }
}

void FancyTabBar::setTabEnabled(int index, bool enable)
{
    Q_ASSERT(index < m_tabs.size());
    Q_ASSERT(index >= 0);

    if (index < m_tabs.size() && index >= 0) {
        m_tabs[index]->enabled = enable;
        update(tabRect(index));
    }
}

bool FancyTabBar::isTabEnabled(int index) const
{
    Q_ASSERT(index < m_tabs.size());
    Q_ASSERT(index >= 0);

    if (index < m_tabs.size() && index >= 0)
        return m_tabs[index]->enabled;

    return false;
}


//////
// FancyColorButton
//////

class FancyColorButton : public QWidget
{
    Q_OBJECT

public:
    explicit FancyColorButton(QWidget *parent = 0)
      : QWidget(parent)
    {
        setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    }

    void mousePressEvent(QMouseEvent *ev)
    {
        emit clicked(ev->button(), ev->modifiers());
    }

    void paintEvent(QPaintEvent *event)
    {
        QWidget::paintEvent(event);

        // Some Themes do not want highlights and shadows in the toolbars.
        // But we definitely want a separator between FancyColorButton and FancyTabBar
        if (!creatorTheme()->flag(Theme::DrawToolBarHighlights)) {
            QPainter p(this);
            p.setPen(StyleHelper::borderColor());
            //OPENMV-DIFF//
            //const QRectF innerRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
            //p.drawLine(innerRect.bottomLeft(), innerRect.bottomRight());
            //OPENMV-DIFF//
        }
    }

signals:
    void clicked(Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
};

//////
// FancyTabWidget
//////

FancyTabWidget::FancyTabWidget(QWidget *parent)
    : QWidget(parent)
{
    m_tabBar = new FancyTabBar(this);
    //OPENMV-DIFF//
    m_tabBar->hide();
    //OPENMV-DIFF//

    m_selectionWidget = new QWidget(this);
    QVBoxLayout *selectionLayout = new QVBoxLayout;
    selectionLayout->setSpacing(0);
    selectionLayout->setMargin(0);

    StyledBar *bar = new StyledBar;
    QHBoxLayout *layout = new QHBoxLayout(bar);
    layout->setMargin(0);
    layout->setSpacing(0);
    auto fancyButton = new FancyColorButton(this);
    //OPENMV-DIFF//
    //connect(fancyButton, &FancyColorButton::clicked, this, &FancyTabWidget::topAreaClicked);
    //OPENMV-DIFF//
    layout->addWidget(fancyButton);
    selectionLayout->addWidget(bar);

    selectionLayout->addWidget(m_tabBar, 1);
    m_selectionWidget->setLayout(selectionLayout);
    m_selectionWidget->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    m_cornerWidgetContainer = new QWidget(this);
    m_cornerWidgetContainer->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    m_cornerWidgetContainer->setAutoFillBackground(false);

    QVBoxLayout *cornerWidgetLayout = new QVBoxLayout;
    cornerWidgetLayout->setSpacing(0);
    cornerWidgetLayout->setMargin(0);
    //OPENMV-DIFF//
    //cornerWidgetLayout->addStretch();
    //OPENMV-DIFF//
    m_cornerWidgetContainer->setLayout(cornerWidgetLayout);

    selectionLayout->addWidget(m_cornerWidgetContainer, 0);

    m_modesStack = new QStackedLayout;
    m_statusBar = new QStatusBar;
    m_statusBar->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);

    QVBoxLayout *vlayout = new QVBoxLayout;
    vlayout->setMargin(0);
    vlayout->setSpacing(0);
    //OPENMV-DIFF//
    //vlayout->addLayout(m_modesStack);
    //OPENMV-DIFF//
    vlayout->addWidget(m_statusBar);

    QHBoxLayout *mainLayout = new QHBoxLayout;
    mainLayout->setMargin(0);
    mainLayout->setSpacing(1);
    mainLayout->addWidget(m_selectionWidget);
    //OPENMV-DIFF//
    m_msplitter = new MiniSplitter;
    m_hsplitter = new MiniSplitter;
    m_vsplitter = new MiniSplitter(Qt::Vertical);

    QWidget *tempWidget = new QWidget;
    tempWidget->setLayout(m_modesStack);
    m_hsplitter->insertWidget(0, tempWidget);

    QWidget *tempWidget2 = new QWidget;
    QVBoxLayout *tempLayout2 = new QVBoxLayout;
    tempLayout2->setMargin(0);
    tempLayout2->setSpacing(0);

    Utils::StyledBar *topBar = new Utils::StyledBar;
    topBar->setSingleRow(true);
    QHBoxLayout *topBarLayout = new QHBoxLayout;
    topBarLayout->setMargin(0);
    topBarLayout->setSpacing(0);
    m_topDrawer = new QToolButton;
    m_topDrawer->setArrowType(Qt::DownArrow);
    m_topDrawer->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred, QSizePolicy::Label));
    topBarLayout->addWidget(m_topDrawer);
    topBar->setLayout(topBarLayout);
    tempLayout2->addWidget(topBar);

    tempLayout2->addWidget(m_vsplitter);

    Utils::StyledBar *bottomBar = new Utils::StyledBar;
    bottomBar->setSingleRow(true);
    QHBoxLayout *bottomBarLayout = new QHBoxLayout;
    bottomBarLayout->setMargin(0);
    bottomBarLayout->setSpacing(0);
    m_bottomDrawer = new QToolButton;
    m_bottomDrawer->setArrowType(Qt::UpArrow);
    m_bottomDrawer->setSizePolicy(QSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred, QSizePolicy::Label));
    bottomBarLayout->addWidget(m_bottomDrawer);
    bottomBar->setLayout(bottomBarLayout);
    tempLayout2->addWidget(bottomBar);

    tempWidget2->setLayout(tempLayout2);
    m_hsplitter->insertWidget(1, tempWidget2);

    m_hsplitter->setStretchFactor(0, 1);
    m_hsplitter->setStretchFactor(1, 0);
    m_hsplitter->setCollapsible(0, true);
    m_hsplitter->setCollapsible(1, true);

    QWidget *tempWidget3 = new QWidget;
    QHBoxLayout *tempLayout3 = new QHBoxLayout;
    tempLayout3->setMargin(0);
    tempLayout3->setSpacing(0);

    Utils::StyledBar *leftBar = new Utils::StyledBar;
    leftBar->setSingleRow(false);
    QVBoxLayout *leftBarLayout = new QVBoxLayout;
    leftBarLayout->setMargin(0);
    leftBarLayout->setSpacing(0);
    m_leftDrawer = new QToolButton;
    m_leftDrawer->setArrowType(Qt::RightArrow);
    m_leftDrawer->setMinimumHeight(160);
    leftBarLayout->addSpacing(22);
    leftBarLayout->addWidget(m_leftDrawer);
    leftBarLayout->addSpacing(160);
    leftBar->setLayout(leftBarLayout);
    tempLayout3->addWidget(leftBar);

    tempLayout3->addWidget(m_hsplitter);

    Utils::StyledBar *rightBar = new Utils::StyledBar;
    rightBar->setSingleRow(false);
    QVBoxLayout *rightBarLayout = new QVBoxLayout;
    rightBarLayout->setMargin(0);
    rightBarLayout->setSpacing(0);
    m_rightDrawer = new QToolButton;
    m_rightDrawer->setArrowType(Qt::LeftArrow);
    m_rightDrawer->setMinimumHeight(160);
    rightBarLayout->addSpacing(22);
    rightBarLayout->addWidget(m_rightDrawer);
    rightBarLayout->addSpacing(160);
    rightBar->setLayout(rightBarLayout);
    tempLayout3->addWidget(rightBar);

    tempWidget3->setLayout(tempLayout3);
    m_msplitter->insertWidget(0, tempWidget3);
    m_msplitter->setStretchFactor(0, 1);
    m_msplitter->setCollapsible(0, false);
    vlayout->insertWidget(0, m_msplitter);

    //OPENMV-DIFF//
    mainLayout->addLayout(vlayout);
    setLayout(mainLayout);

    connect(m_tabBar, &FancyTabBar::currentChanged, this, &FancyTabWidget::showWidget);
    //OPENMV-DIFF//
    setStyleSheet(QStringLiteral("QAbstractScrollArea::corner{background-color:#404244;}"
    "QScrollBar:vertical{margin-top:17px;margin-right:0px;margin-bottom:17px;margin-left:0px;background-color:#404244;}"
    "QScrollBar::sub-line:vertical{subcontrol-origin:margin;subcontrol-position:top;height:17px;background-color:#404244;border-top:1px solid #2E2E2E;}"
    "QScrollBar::add-line:vertical{subcontrol-origin:margin;subcontrol-position:bottom;height:17px;background-color:#404244;}"
    "QScrollBar::up-arrow:vertical{margin-left:1px;margin-right:1px;border-image:url(:/core/images/scroll-arrowup.png);}"
    "QScrollBar::down-arrow:vertical{margin-left:1px;margin-right:1px;border-image:url(:/core/images/scroll-arrowdown.png);}"
    "QScrollBar::sub-page:vertical{margin-left:1px;margin-right:1px;background-color:#404244;}"
    "QScrollBar::add-page:vertical{margin-left:1px;margin-right:1px;background-color:#404244;}"
    "QScrollBar::handle:vertical{margin-left:1px;margin-right:1px;min-height:20px;background-color:#2E2E2E;}"
    "QScrollBar:horizontal{margin-top:0px;margin-right:17px;margin-bottom:0px;margin-left:17px;background-color:#404244;}"
    "QScrollBar::sub-line:horizontal{subcontrol-origin:margin;subcontrol-position:left;width:17px;background-color:#404244;}"
    "QScrollBar::add-line:horizontal{subcontrol-origin:margin;subcontrol-position:right;width:17px;background-color:#404244;}"
    "QScrollBar::left-arrow:horizontal{margin-top:1px;margin-bottom:1px;border-image:url(:/core/images/scroll-arrowleft.png);}"
    "QScrollBar::right-arrow:horizontal{margin-top:1px;margin-bottom:1px;border-image:url(:/core/images/scroll-arrowright.png);}"
    "QScrollBar::sub-page:horizontal{margin-top:1px;margin-bottom:1px;background-color:#404244;}"
    "QScrollBar::add-page:horizontal{margin-top:1px;margin-bottom:1px;background-color:#404244;}"
    "QScrollBar::handle:horizontal{margin-top:1px;margin-bottom:1px;min-width:20px;background-color:#2E2E2E;}"
    "QScrollBar::up-arrow:hover,QScrollBar::right-arrow:hover,QScrollBar::down-arrow:hover,QScrollBar::left-arrow:hover,QScrollBar::handle:hover{background-color:#595b5d;}"
    "QScrollBar::up-arrow:pressed,QScrollBar::right-arrow:pressed,QScrollBar::down-arrow:pressed,QScrollBar::left-arrow:pressed,QScrollBar::handle:pressed{background-color:#262829;}"
    "QPlainTextEdit{background-color:#1E1E27;}"));

    QPalette pal = palette();
    pal.setColor(QPalette::Background, QColor(QStringLiteral("#404244")));
    setAutoFillBackground(true);
    setPalette(pal);
    //OPENMV-DIFF//
}

void FancyTabWidget::setSelectionWidgetVisible(bool visible)
{
    m_selectionWidget->setVisible(visible);
}

bool FancyTabWidget::isSelectionWidgetVisible() const
{
    return m_selectionWidget->isVisible();
}

void FancyTabWidget::insertTab(int index, QWidget *tab, const QIcon &icon, const QString &label)
{
    m_modesStack->insertWidget(index, tab);
    m_tabBar->insertTab(index, icon, label);
}

void FancyTabWidget::removeTab(int index)
{
    m_modesStack->removeWidget(m_modesStack->widget(index));
    m_tabBar->removeTab(index);
}

void FancyTabWidget::setBackgroundBrush(const QBrush &brush)
{
    QPalette pal;
    pal.setBrush(QPalette::Mid, brush);
    m_tabBar->setPalette(pal);
    m_cornerWidgetContainer->setPalette(pal);
}

void FancyTabWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    if (m_selectionWidget->isVisible()) {
        QPainter painter(this);

        QRect rect = m_selectionWidget->rect().adjusted(0, 0, 1, 0);
        rect = style()->visualRect(layoutDirection(), geometry(), rect);
        const QRectF boderRect = QRectF(rect).adjusted(0.5, 0.5, -0.5, -0.5);
        StyleHelper::verticalGradient(&painter, rect, rect);
        painter.setPen(StyleHelper::borderColor());
        painter.drawLine(boderRect.topRight(), boderRect.bottomRight());

        //OPENMV-DIFF//
        //QColor light = StyleHelper::sidebarHighlight();
        //painter.setPen(light);
        //painter.drawLine(boderRect.bottomLeft(), boderRect.bottomRight());
        //OPENMV-DIFF//
    }
}

void FancyTabWidget::insertCornerWidget(int pos, QWidget *widget)
{
    QVBoxLayout *layout = static_cast<QVBoxLayout *>(m_cornerWidgetContainer->layout());
    layout->insertWidget(pos, widget);
}

int FancyTabWidget::cornerWidgetCount() const
{
    return m_cornerWidgetContainer->layout()->count();
}

void FancyTabWidget::addCornerWidget(QWidget *widget)
{
    m_cornerWidgetContainer->layout()->addWidget(widget);
}

int FancyTabWidget::currentIndex() const
{
    return m_tabBar->currentIndex();
}

QStatusBar *FancyTabWidget::statusBar() const
{
    return m_statusBar;
}

void FancyTabWidget::setCurrentIndex(int index)
{
    if (m_tabBar->isTabEnabled(index))
        m_tabBar->setCurrentIndex(index);
}

void FancyTabWidget::showWidget(int index)
{
    emit currentAboutToShow(index);
    m_modesStack->setCurrentIndex(index);
    emit currentChanged(index);
}

void FancyTabWidget::setTabToolTip(int index, const QString &toolTip)
{
    m_tabBar->setTabToolTip(index, toolTip);
}

void FancyTabWidget::setTabEnabled(int index, bool enable)
{
    m_tabBar->setTabEnabled(index, enable);
}

bool FancyTabWidget::isTabEnabled(int index) const
{
    return m_tabBar->isTabEnabled(index);
}

#include "fancytabwidget.moc"
