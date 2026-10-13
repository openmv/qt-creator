// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputpanemanager.h"

#include "actionmanager/actioncontainer.h"
#include "actionmanager/actionmanager.h"
#include "actionmanager/command.h"
#include "coreplugintr.h"
#include "editormanager/editormanager.h"
#include "editormanager/ieditor.h"
#include "find/optionspopup.h"
#include "findplaceholder.h"
#include "icore.h"
#include "ioutputpane.h"
#include "modemanager.h"
#include "outputpane.h"
#include "statusbarmanager.h"

#include <utils/algorithm.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/proxyaction.h>
#include <utils/qtcassert.h>
#include <utils/styledbar.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QFocusEvent>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QPainter>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QTimeLine>
#include <QToolBar>
#include <QToolButton>

using namespace Utils;
using namespace Core::Internal;

namespace Core {
namespace Internal {

Q_GLOBAL_STATIC(QList<Core::OutputPanePlaceHolder *>, sPlaceholders)

class BadgeLabel
{
public:
    BadgeLabel();
    void paint(QPainter *p, int x, int y, bool isChecked);
    void setText(const QString &text);
    QString text() const;
    QSize sizeHint() const;

private:
    void calculateSize();

    QSize m_size;
    QString m_text;
    QFont m_font;
    static const int m_padding = 6;
};

class OutputPaneToggleButton : public QToolButton
{
    Q_OBJECT

public:
    OutputPaneToggleButton(int number, const QString &text, QAction *action);

    QSize sizeHint() const override;
    void paintEvent(QPaintEvent*) override;
    void flash(int count = 3);
    void setIconBadgeNumber(int number);
    bool isPaneVisible() const;

    void contextMenuEvent(QContextMenuEvent *e) override;

signals:
    void contextMenuRequested();

private:
    void updateToolTip();
    void checkStateSet() override;

    QString m_number;
    QString m_text;
    QAction *m_action;
    QTimeLine *m_flashTimer;
    BadgeLabel m_badgeNumberLabel;
};

class OutputPaneManageButton : public QToolButton
{
    Q_OBJECT
public:
    OutputPaneManageButton();
    void paintEvent(QPaintEvent *) override;

    void contextMenuEvent(QContextMenuEvent *e) override;

signals:
    void menuRequested();
};

} // Internal

class OutputPanePlaceHolderPrivate
{
public:
    OutputPanePlaceHolderPrivate(Id mode, QSplitter *parent)
        : m_mode(mode), m_splitter(parent)
    {}

    Id m_mode;
    QSplitter *m_splitter;
    int m_nonMaximizedSize = 0;
    bool m_isMaximized = false;
    bool m_initialized = false;
    static OutputPanePlaceHolder* m_current;
};

OutputPanePlaceHolder *OutputPanePlaceHolderPrivate::m_current = nullptr;

OutputPanePlaceHolder::OutputPanePlaceHolder(Id mode, QSplitter *parent)
   : QWidget(parent), d(new OutputPanePlaceHolderPrivate(mode, parent))
{
    sPlaceholders->append(this);
    setVisible(false);
    setLayout(new QVBoxLayout);
    QSizePolicy sp;
    sp.setHorizontalPolicy(QSizePolicy::Preferred);
    sp.setVerticalPolicy(QSizePolicy::Preferred);
    sp.setHorizontalStretch(0);
    setSizePolicy(sp);
    layout()->setContentsMargins(0, 0, 0, 0);
    connect(ModeManager::instance(), &ModeManager::currentModeChanged,
            this, &OutputPanePlaceHolder::currentModeChanged);
    // if this is part of a lazily created mode widget,
    // we need to check if this is the current placeholder
    currentModeChanged(ModeManager::currentModeId());
}

OutputPanePlaceHolder::~OutputPanePlaceHolder()
{
    if (OutputPanePlaceHolderPrivate::m_current == this) {
        if (Internal::OutputPaneManager *om = Internal::OutputPaneManager::instance()) {
            om->setParent(nullptr);
            om->hide();
        }
        OutputPanePlaceHolderPrivate::m_current = nullptr;
    }
    delete d;
}

void OutputPanePlaceHolder::currentModeChanged(Id mode)
{
    if (OutputPanePlaceHolderPrivate::m_current == this) {
        OutputPanePlaceHolderPrivate::m_current = nullptr;
        if (d->m_initialized)
            Internal::OutputPaneManager::setOutputPaneHeightSetting(d->m_nonMaximizedSize);
        Internal::OutputPaneManager *om = Internal::OutputPaneManager::instance();
        om->hide();
        om->setParent(nullptr);
        om->updateStatusButtons(false);
    }
    if (d->m_mode == mode) {
        if (OutputPanePlaceHolderPrivate::m_current && OutputPanePlaceHolderPrivate::m_current->d->m_initialized)
            Internal::OutputPaneManager::setOutputPaneHeightSetting(OutputPanePlaceHolderPrivate::m_current->d->m_nonMaximizedSize);
        Core::OutputPanePlaceHolderPrivate::m_current = this;
        Internal::OutputPaneManager *om = Internal::OutputPaneManager::instance();
        layout()->addWidget(om);
        om->show();
        om->updateStatusButtons(isVisible());
        Internal::OutputPaneManager::updateMaximizeButton(d->m_isMaximized);
    }
}

void OutputPanePlaceHolder::setMaximized(bool maximize)
{
    if (d->m_isMaximized == maximize)
        return;
    if (!d->m_splitter)
        return;
    int idx = d->m_splitter->indexOf(this);
    if (idx < 0)
        return;

    d->m_isMaximized = maximize;
    if (OutputPanePlaceHolderPrivate::m_current == this)
        Internal::OutputPaneManager::updateMaximizeButton(d->m_isMaximized);
    QList<int> sizes = d->m_splitter->sizes();

    if (maximize) {
        d->m_nonMaximizedSize = sizes[idx];
        int sum = 0;
        for (const int s : std::as_const(sizes))
            sum += s;
        for (int i = 0; i < sizes.count(); ++i) {
            sizes[i] = 32;
        }
        sizes[idx] = sum - (sizes.count()-1) * 32;
    } else {
        int target = d->m_nonMaximizedSize > 0 ? d->m_nonMaximizedSize : sizeHint().height();
        int space = sizes[idx] - target;
        if (space > 0) {
            for (int i = 0; i < sizes.count(); ++i) {
                sizes[i] += space / (sizes.count()-1);
            }
            sizes[idx] = target;
        }
    }

    d->m_splitter->setSizes(sizes);
}

bool OutputPanePlaceHolder::isMaximized() const
{
    return d->m_isMaximized;
}

void OutputPanePlaceHolder::setHeight(int height)
{
    if (height == 0)
        return;
    if (!d->m_splitter)
        return;
    const int idx = d->m_splitter->indexOf(this);
    if (idx < 0)
        return;

    d->m_splitter->refresh();
    QList<int> sizes = d->m_splitter->sizes();
    const int difference = height - sizes.at(idx);
    if (difference == 0)
        return;
    const int adaption = difference / (sizes.count()-1);
    for (int i = 0; i < sizes.count(); ++i) {
        sizes[i] -= adaption;
    }
    sizes[idx] = height;
    d->m_splitter->setSizes(sizes);
}

void OutputPanePlaceHolder::ensureSizeHintAsMinimum()
{
    if (!d->m_splitter)
        return;
    Internal::OutputPaneManager *om = Internal::OutputPaneManager::instance();
    int minimum = (d->m_splitter->orientation() == Qt::Vertical
                   ? om->sizeHint().height() : om->sizeHint().width());
    if (nonMaximizedSize() < minimum && !d->m_isMaximized)
        setHeight(minimum);
}

int OutputPanePlaceHolder::nonMaximizedSize() const
{
    if (!d->m_initialized)
        return Internal::OutputPaneManager::outputPaneHeightSetting();
    return d->m_nonMaximizedSize;
}

Id OutputPanePlaceHolder::mode() const
{
    return d->m_mode;
}

void OutputPanePlaceHolder::resizeEvent(QResizeEvent *event)
{
    if (d->m_isMaximized || event->size().height() == 0)
        return;
    d->m_nonMaximizedSize = event->size().height();
}

void OutputPanePlaceHolder::showEvent(QShowEvent *)
{
    if (!d->m_initialized) {
        d->m_initialized = true;
        setHeight(Internal::OutputPaneManager::outputPaneHeightSetting());
    }
    if (OutputPanePlaceHolderPrivate::m_current == this) {
        Internal::OutputPaneManager *om = Internal::OutputPaneManager::instance();
        om->updateStatusButtons(true);
    }
}

OutputPanePlaceHolder *OutputPanePlaceHolder::getCurrent()
{
    return OutputPanePlaceHolderPrivate::m_current;
}

bool OutputPanePlaceHolder::isCurrentVisible()
{
    return OutputPanePlaceHolderPrivate::m_current && OutputPanePlaceHolderPrivate::m_current->isVisible();
}

bool OutputPanePlaceHolder::modeHasOutputPanePlaceholder(Utils::Id mode)
{
    return Utils::anyOf(*sPlaceholders, Utils::equal(&OutputPanePlaceHolder::mode, mode));
}

class OutputPaneData
{
public:
    OutputPaneData(IOutputPane *pane = nullptr) : pane(pane) {}

    IOutputPane *pane = nullptr;
    Id id;
    OutputPaneToggleButton *button = nullptr;
    QAction *action = nullptr;
};

static QVector<OutputPaneData> g_outputPanes;
static bool g_managerConstructed = false; // For debugging reasons.

// OPENMV-DIFF //
// True when launched as the read-only telemetry viewer (-viewer_mode). Only
// read at startup (manager construction / settings load), so not cached.
// NOTE: upstream 20.0.2 already closed namespace Internal above (line ~114), so
// unlike OpenMV's 14.0.2 base this helper lives in namespace Core and there is
// no extra "} // Internal" close here.
static bool isViewerMode()
{
    return QCoreApplication::arguments().contains(QLatin1String("-viewer_mode"));
}
// OPENMV-DIFF //

// OutputPane

// OPENMV-DIFF //
// IOutputPane::IOutputPane(QObject *parent)
// OPENMV-DIFF //
IOutputPane::IOutputPane(bool hidden, QObject *parent)
// OPENMV-DIFF //
    : QObject(parent)
{
    // We need all pages first. Ignore latecomers and shout.
    QTC_ASSERT(!g_managerConstructed, return);
    // OPENMV-DIFF //
    // g_outputPanes.append(OutputPaneData(this));
    // OPENMV-DIFF //
    if (!hidden) g_outputPanes.append(OutputPaneData(this));
    // OPENMV-DIFF //

    m_zoomInButton = Command::createToolButtonWithShortcutToolTip(Constants::ZOOM_IN);
    m_zoomInButton->setIcon(Utils::Icons::PLUS_TOOLBAR.icon());
    connect(m_zoomInButton, &QToolButton::clicked, this, [this] { emit zoomInRequested(1); });

    m_zoomOutButton = Command::createToolButtonWithShortcutToolTip(Constants::ZOOM_OUT);
    m_zoomOutButton->setIcon(Utils::Icons::MINUS_TOOLBAR.icon());
    connect(m_zoomOutButton, &QToolButton::clicked, this, [this] { emit zoomOutRequested(1); });

    // reinitialize the output pane buttons if a lazy loaded plugin adds a pane
    if (OutputPaneManager::initialized())
        QMetaObject::invokeMethod(this, &OutputPaneManager::setupButtons, Qt::QueuedConnection);
}

IOutputPane::~IOutputPane()
{
    const int i = Utils::indexOf(g_outputPanes, Utils::equal(&OutputPaneData::pane, this));
    // OPENMV-DIFF //
    // QTC_ASSERT(i >= 0, return);
    //
    // g_outputPanes.removeAt(i);
    // OPENMV-DIFF //
    if (i >= 0)
    {
        delete g_outputPanes.at(i).button;
        g_outputPanes.removeAt(i);
    }
    // OPENMV-DIFF //

    delete m_zoomInButton;
    delete m_zoomOutButton;
}

QList<QWidget *> IOutputPane::toolBarWidgets() const
{
    QList<QWidget *> widgets;
    if (m_filterOutputLineEdit)
        widgets << m_filterOutputLineEdit;
    return widgets << m_zoomInButton << m_zoomOutButton;
}

/*!
    Returns the ID of the output pane.
*/
Id IOutputPane::id() const
{
    return m_id;
}

/*!
    Sets the ID of the output pane to \a id.
    This is used for persisting the visibility state.
*/
void IOutputPane::setId(const Utils::Id &id)
{
    m_id = id;
}

/*!
    Returns the translated display name of the output pane.
*/
QString IOutputPane::displayName() const
{
    return m_displayName;
}

/*!
    Determines the position of the output pane on the status bar and the
    default visibility.
    \sa setPriorityInStatusBar()
*/
int IOutputPane::priorityInStatusBar() const
{
    return m_priority;
}

/*!
    Sets the position of the output pane on the status bar and the default
    visibility to \a priority.
    \list
        \li higher numbers are further to the front
        \li >= 0 are shown in status bar by default
        \li < 0 are not shown in status bar by default
    \endlist
*/
void IOutputPane::setPriorityInStatusBar(int priority)
{
    m_priority = priority;
}

/*!
    Sets the translated display name of the output pane to \a name.
*/
void IOutputPane::setDisplayName(const QString &name)
{
    m_displayName = name;
}

void IOutputPane::visibilityChanged(bool /*visible*/)
{
}

bool IOutputPane::hasFilterContext() const
{
    return false;
}

void IOutputPane::setFont(const QFont &font)
{
    emit fontChanged(font);
}

void IOutputPane::setWheelZoomEnabled(bool enabled)
{
    emit wheelZoomEnabledChanged(enabled);
}

void IOutputPane::setupFilterUi(const Key &historyKey, const QString &actionSuffix)
{
    m_filterActionSuffix = actionSuffix;

    ActionBuilder filterRegexpAction(this, filterRegexpActionId());
    filterRegexpAction.setText(Tr::tr("Use Regular Expressions"));
    filterRegexpAction.setCheckable(true);
    filterRegexpAction.addOnToggled(this, &IOutputPane::setRegularExpressions);

    ActionBuilder filterCaseSensitiveAction(this, filterCaseSensitivityActionId());
    filterCaseSensitiveAction.setText(Tr::tr("Case Sensitive"));
    filterCaseSensitiveAction.setCheckable(true);
    filterCaseSensitiveAction.addOnToggled(this, &IOutputPane::setCaseSensitive);

    ActionBuilder invertFilterAction(this, filterInvertedActionId());
    invertFilterAction.setText(Tr::tr("Show Non-matching Lines"));
    invertFilterAction.setCheckable(true);
    invertFilterAction.addOnToggled(this, [this, action=invertFilterAction.contextAction()] {
        m_invertFilter = action->isChecked();
        updateFilter();
    });

    ActionBuilder filterBeforeAction(this, filterBeforeActionId());
    //: The placeholder "{}" is replaced by a spin box for selecting a number.
    filterBeforeAction.setText(Tr::tr("Show {} &preceding lines"));
    QAction *action = filterBeforeAction.contextAction();
    NumericOption::set(action, NumericOption{0, 0, 9});
    NumericOption::set(filterBeforeAction.commandAction(), NumericOption{0, 0, 9});
    connect(action, &QAction::changed, this, [this, action] {
        const std::optional<NumericOption> option = NumericOption::get(action);
        QTC_ASSERT(option, return);
        m_beforeContext = option->currentValue;
        updateFilter();
    });

    ActionBuilder filterAfterAction(this, filterAfterActionId());
    //: The placeholder "{}" is replaced by a spin box for selecting a number.
    filterAfterAction.setText(Tr::tr("Show {} &subsequent lines"));
    action = filterAfterAction.contextAction();
    NumericOption::set(action, NumericOption{0, 0, 9});
    NumericOption::set(filterAfterAction.commandAction(), NumericOption{0, 0, 9});
    connect(action, &QAction::changed, this, [this, action] {
        const std::optional<NumericOption> option = NumericOption::get(action);
        QTC_ASSERT(option, return);
        m_afterContext = option->currentValue;
        updateFilter();
    });

    m_filterOutputLineEdit = new FancyLineEdit;
    m_filterOutputLineEdit->setPlaceholderText(Tr::tr("Filter output..."));
    m_filterOutputLineEdit->setButtonVisible(FancyLineEdit::Left, true);
    m_filterOutputLineEdit->setButtonIcon(FancyLineEdit::Left, Icons::MAGNIFIER.icon());
    m_filterOutputLineEdit->setFiltering(true);
    m_filterOutputLineEdit->setEnabled(false);
    m_filterOutputLineEdit->setHistoryCompleter(historyKey);
    m_filterOutputLineEdit->setAttribute(Qt::WA_MacShowFocusRect, false);
    connect(m_filterOutputLineEdit, &FancyLineEdit::textChanged,
            this, &IOutputPane::updateFilter);
    connect(m_filterOutputLineEdit, &FancyLineEdit::returnPressed,
            this, &IOutputPane::updateFilter);
    connect(m_filterOutputLineEdit, &FancyLineEdit::leftButtonClicked,
            this, &IOutputPane::filterOutputButtonClicked);
}

QString IOutputPane::filterText() const
{
    return m_filterOutputLineEdit->text();
}

void IOutputPane::setFilteringEnabled(bool enable)
{
    m_filterOutputLineEdit->setEnabled(enable);
}

void IOutputPane::setupContext(const Id &context, QWidget *widget)
{
    return setupContext(Context(context), widget);
}

void IOutputPane::setupContext(const Context &context, QWidget *widget)
{
    IContext::attach(widget, context);

    ActionBuilder(this, Constants::ZOOM_IN)
        .setContext(context)
        .addOnTriggered(this, [this] { emit zoomInRequested(1); });

    ActionBuilder(this, Constants::ZOOM_OUT)
        .setContext(context)
        .addOnTriggered(this, [this] { emit zoomOutRequested(1); });

    ActionBuilder(this, Constants::ZOOM_RESET)
        .setContext(context)
        .addOnTriggered(this, &IOutputPane::resetZoomRequested);
}

void IOutputPane::setZoomButtonsEnabled(bool enabled)
{
    m_zoomInButton->setEnabled(enabled);
    m_zoomOutButton->setEnabled(enabled);
}

void IOutputPane::updateFilter()
{
    QTC_ASSERT(false, qDebug() << "updateFilter() needs to get re-implemented");
}

void IOutputPane::filterOutputButtonClicked()
{
    QVector<Utils::Id> commands = {filterRegexpActionId(),
                                   filterCaseSensitivityActionId(),
                                   filterInvertedActionId()};

    if (hasFilterContext()) {
        commands.emplaceBack(filterBeforeActionId());
        commands.emplaceBack(filterAfterActionId());
    }

    auto popup = new Core::OptionsPopup(m_filterOutputLineEdit, commands);
    popup->show();
}

void IOutputPane::setRegularExpressions(bool regularExpressions)
{
    m_filterRegexp = regularExpressions;
    updateFilter();
}

Id IOutputPane::filterRegexpActionId() const
{
    return Id("OutputFilter.RegularExpressions").withSuffix(m_filterActionSuffix);
}

Id IOutputPane::filterCaseSensitivityActionId() const
{
    return Id("OutputFilter.CaseSensitive").withSuffix(m_filterActionSuffix);
}

Id IOutputPane::filterInvertedActionId() const
{
    return Id("OutputFilter.Invert").withSuffix(m_filterActionSuffix);
}

Id IOutputPane::filterBeforeActionId() const
{
    return Id("OutputFilter.BeforeContext").withSuffix(m_filterActionSuffix);
}

Id IOutputPane::filterAfterActionId() const
{
    return Id("OutputFilter.AfterContext").withSuffix(m_filterActionSuffix);
}

void IOutputPane::setCaseSensitive(bool caseSensitive)
{
    m_filterCaseSensitivity = caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
    updateFilter();
}

namespace Internal {

const char outputPaneSettingsKeyC[] = "OutputPaneVisibility";
const char outputPaneIdKeyC[] = "id";
const char outputPaneVisibleKeyC[] = "visible";
const int buttonBorderWidth = 3;

// OPENMV-DIFF //
// static int numberAreaWidth()
// {
//     return creatorTheme()->flag(Theme::FlatToolBars) ? 15 : 19;
// }
// OPENMV-DIFF //

////
// OutputPaneManager
////

static OutputPaneManager *m_instance = nullptr;

void OutputPaneManager::create()
{
   m_instance = new OutputPaneManager;
}

void OutputPaneManager::destroy()
{
    delete m_instance;
    m_instance = nullptr;
}

OutputPaneManager *OutputPaneManager::instance()
{
    return m_instance;
}

void OutputPaneManager::updateStatusButtons(bool visible)
{
    int idx = currentIndex();
    if (idx == -1)
        return;
    QTC_ASSERT(idx < g_outputPanes.size(), return);
    const OutputPaneData &data = g_outputPanes.at(idx);
    QTC_ASSERT(data.button, return);
    data.button->setChecked(visible);
    data.pane->visibilityChanged(visible);
}

void OutputPaneManager::updateMaximizeButton(bool maximized)
{
    if (maximized) {
        static const QIcon icon = Utils::Icons::ARROW_DOWN.icon();
        m_instance->m_minMaxAction->setIcon(icon);
        m_instance->m_minMaxAction->setText(Tr::tr("Minimize"));
    } else {
        static const QIcon icon = Utils::Icons::ARROW_UP.icon();
        m_instance->m_minMaxAction->setIcon(icon);
        m_instance->m_minMaxAction->setText(Tr::tr("Maximize"));
    }
}

// Return shortcut as Alt+<number> or Cmd+<number> if number is a non-zero digit
// OPENMV-DIFF //
// static QKeySequence paneShortCut(int number)
// {
//     if (number < 1 || number > 9)
//         return QKeySequence();
//
//     const int modifier = HostOsInfo::isMacHost() ? Qt::CTRL : Qt::ALT;
//     return QKeySequence(modifier | (Qt::Key_0 + number));
// }
// OPENMV-DIFF //

OutputPaneManager::OutputPaneManager(QWidget *parent) :
    QWidget(parent),
    m_titleLabel(new QLabel),
    m_manageButton(new OutputPaneManageButton),
    m_outputWidgetPane(new QStackedWidget),
    m_opToolBarWidgets(new QStackedWidget)
{
    setWindowTitle(Tr::tr("Output"));

    m_titleLabel->setContentsMargins(5, 0, 5, 0);

    // OPENMV-DIFF //
    // OBSOLETE: OpenMV's 14.0.2 base created m_clearAction/m_nextAction/m_prevAction/
    // m_minMaxAction here as plain QActions (and a second closeButton). Upstream 20.0.2
    // creates them via ActionBuilder(...).bindContextAction(&m_...) below, so the manual
    // creation is dropped (it would also re-declare the closeButton made further down).
    // The OpenMV-only intent here -- disabling prev/next/minMax default shortcuts -- is
    // re-applied on the ActionBuilder chains below.
    // OPENMV-DIFF //
    connect(ICore::instance(), &ICore::saveSettingsRequested, this, &OutputPaneManager::saveSettings);

    auto toolBar = new StyledBar;
    auto clearButton = new QToolButton;
    auto prevToolButton = new QToolButton;
    auto nextToolButton = new QToolButton;
    auto minMaxButton = new QToolButton;
    auto closeButton = new QToolButton;

    m_buttonsWidget = new QWidget;
    m_buttonsWidget->setObjectName("OutputPaneButtons"); // used for UI introduction

    using namespace Layouting;
    Row {
        m_titleLabel,
        new StyledSeparator,
        clearButton,
        // OPENMV-DIFF //
        // prevToolButton,
        // nextToolButton,
        // OPENMV-DIFF //
        m_opToolBarWidgets,
        minMaxButton,
        closeButton,
        spacing(0), noMargin,
    }.attachTo(toolBar);

    Column {
        toolBar,
        m_outputWidgetPane,
        new FindToolBarPlaceHolder(this),
        spacing(0), noMargin,
    }.attachTo(this);

    Row {
        // OPENMV-DIFF //
        // spacing(creatorTheme()->flag(Theme::FlatToolBars) ? 9 : 4), customMargins(5, 0, 0, 0),
        // OPENMV-DIFF //
        spacing(creatorTheme()->flag(Theme::FlatToolBars) ? 9 : 4), customMargins(1, 0, 0, 0),
        // OPENMV-DIFF //
    }.attachTo(m_buttonsWidget);

    StatusBarManager::addStatusBarWidget(m_buttonsWidget, StatusBarManager::Second);

    // OPENMV-DIFF //
    // ActionContainer *mview = ActionManager::actionContainer(Constants::M_VIEW);
    // OPENMV-DIFF //

    // Window->Output Panes
    ActionContainer *mpanes = ActionManager::createMenu(Constants::M_VIEW_PANES);
    // OPENMV-DIFF //
    // mview->addMenu(mpanes, Constants::G_VIEW_PANES);
    // OPENMV-DIFF //
    mpanes->menu()->setTitle(Tr::tr("Out&put"));
    mpanes->appendGroup("Coreplugin.OutputPane.ActionsGroup");
    mpanes->appendGroup("Coreplugin.OutputPane.PanesGroup");

    ActionBuilder clearAction(this, Constants::OUTPUTPANE_CLEAR);
    clearAction.setIcon(Utils::Icons::CLEAN_TOOLBAR.icon())
        .setText(Tr::tr("Clear"))
        .addOnTriggered(this, &OutputPaneManager::clearPage)
        .addToContainer(Constants::M_VIEW_PANES, "Coreplugin.OutputPane.ActionsGroup")
        .bindContextAction(&m_clearAction);
    // OPENMV-DIFF //
    // OpenMV puts the Clear action on the output-pane toolbar with the toolbar icon.
    // Upstream 20.0.2's ActionBuilder sets the icon directly and the tool button uses
    // contextAction() (same as prev/next), replacing the old proxyActionWithIcon wiring.
    clearButton->setDefaultAction(clearAction.contextAction());
    // OPENMV-DIFF //

    ActionBuilder prevAction(this, "Coreplugin.OutputPane.previtem");
    prevAction
        .setIcon(Utils::Icons::ARROW_UP_TOOLBAR.icon())
        .setText(Tr::tr("Previous Item"))
        .addOnTriggered(this, &OutputPaneManager::slotPrev)
        // OPENMV-DIFF //
        // .setDefaultKeySequence(QKeySequence(Tr::tr("Shift+F6")))
        // OPENMV-DIFF //
        .addToContainer(Constants::M_VIEW_PANES, "Coreplugin.OutputPane.ActionsGroup")
        .bindContextAction(&m_prevAction);
    prevToolButton->setDefaultAction(prevAction.contextAction());

    ActionBuilder nextAction(this, "Coreplugin.OutputPane.nextitem");
    nextAction
        .setIcon(Utils::Icons::ARROW_DOWN_TOOLBAR.icon())
        .setText(Tr::tr("Next Item"))
        .addOnTriggered(this, &OutputPaneManager::slotNext)
        // OPENMV-DIFF //
        // .setDefaultKeySequence(QKeySequence(Tr::tr("F6")))
        // OPENMV-DIFF //
        .addToContainer(Constants::M_VIEW_PANES, "Coreplugin.OutputPane.ActionsGroup")
        .bindContextAction(&m_nextAction);
    nextToolButton->setDefaultAction(nextAction.contextAction());

    ActionBuilder minMaxAction(this, "Coreplugin.OutputPane.minmax");
    minMaxAction
        // OPENMV-DIFF //
        // .setDefaultKeySequence(Tr::tr("Ctrl+Shift+9"), Tr::tr("Alt+Shift+9"))
        // OPENMV-DIFF //
        .setCommandAttribute(Command::CA_UpdateText)
        .setCommandAttribute(Command::CA_UpdateIcon)
        .addToContainer(Constants::M_VIEW_PANES, "Coreplugin.OutputPane.ActionsGroup")
        .addOnTriggered(this, &OutputPaneManager::toggleMaximized)
        .bindContextAction(&m_minMaxAction);
    minMaxButton->setDefaultAction(minMaxAction.commandAction());

    ActionBuilder closeAction(this, Constants::OUTPUTPANE_CLOSE);
    closeAction
        .setIcon(Icons::CLOSE_SPLIT_BOTTOM.icon())
        .setText(Tr::tr("Close"))
        .addOnTriggered(this, &OutputPaneManager::slotHide)
        .addToContainer(Constants::M_VIEW_PANES, "Coreplugin.OutputPane.ActionsGroup")
        .bindContextAction(&m_closeAction);
    closeButton->setDefaultAction(closeAction.commandAction());

    mpanes->addSeparator("Coreplugin.OutputPane.ActionsGroup");

    // OPENMV-DIFF //
    // In viewer mode (read-only telemetry viewer) there's nothing to maximize
    // or close in the output pane, so hide the maximize and close buttons. Both
    // are local QToolButtons that nothing re-shows, so hiding the widgets is
    // durable (a standalone QToolButton tracks its action's enabled state but
    // not its visibility, so hiding via the action wouldn't hide the button).
    if (isViewerMode()) {
        minMaxButton->hide();
        closeButton->hide();
    }
    // OPENMV-DIFF //
}

void OutputPaneManager::initialize()
{
    setupButtons();

    const int currentIdx = m_instance->currentIndex();
    if (QTC_GUARD(currentIdx >= 0 && currentIdx < g_outputPanes.size()))
        m_instance->m_titleLabel->setText(g_outputPanes[currentIdx].pane->displayName());
    // OPENMV-DIFF //
    // Upstream 20.0.2 relocated this init tail from setupButtons() into initialize();
    // OpenMV's two customizations are re-applied here:
    //  - the OutputPane "manage" button is not added to the buttons widget.
    // m_instance->m_buttonsWidget->layout()->addWidget(m_instance->m_manageButton);
    // OPENMV-DIFF //
    connect(m_instance->m_manageButton,
            &OutputPaneManageButton::menuRequested,
            m_instance,
            &OutputPaneManager::popupMenu);

    updateMaximizeButton(false); // give it an initial name

    m_instance->readSettings();

    connect(ModeManager::instance(), &ModeManager::currentModeChanged, m_instance, [] {
        const int index = m_instance->currentIndex();
        m_instance->updateActions(index >= 0 ? g_outputPanes.at(index).pane : nullptr);
    });

    // OPENMV-DIFF //
    //  - defer showing the output pane on startup when it was visible last session.
    QTimer::singleShot(0, m_instance, [] () {
        if (m_instance->m_outputPaneVisibleOnStartup)
            m_instance->showPage(m_instance->m_outputWidgetPane->currentIndex(), IOutputPane::ModeSwitch);
    });
    // OPENMV-DIFF //

    m_instance->m_initialized = true;
}

void OutputPaneManager::setupButtons()
{
    for (auto &pane : g_outputPanes)
        delete pane.button;

    QFontMetrics titleFm = m_instance->m_titleLabel->fontMetrics();
    int minTitleWidth = 0;

    Utils::sort(g_outputPanes, [](const OutputPaneData &d1, const OutputPaneData &d2) {
        return d1.pane->priorityInStatusBar() > d2.pane->priorityInStatusBar();
    });
    const int n = g_outputPanes.size();

    int shortcutNumber = 1;
    const Id baseId = "QtCreator.Pane.";
    for (int i = 0; i != n; ++i) {
        OutputPaneData &data = g_outputPanes[i];
        IOutputPane *outPane = data.pane;
        QWidget *widget = outPane->outputWidget(m_instance);
        int idx = m_instance->m_outputWidgetPane->indexOf(widget);
        if (idx < 0) {
            idx = m_instance->m_outputWidgetPane->insertWidget(i, widget);

            connect(outPane, &IOutputPane::showPage, m_instance, [idx](int flags) {
                m_instance->showPage(idx, flags);
            });
            connect(outPane, &IOutputPane::hidePage, m_instance, &OutputPaneManager::slotHide);

            connect(outPane, &IOutputPane::togglePage, m_instance, [idx](int flags) {
                if (OutputPanePlaceHolder::isCurrentVisible() && m_instance->currentIndex() == idx)
                    m_instance->slotHide();
                else
                    m_instance->showPage(idx, flags);
            });

            connect(outPane, &IOutputPane::navigateStateUpdate, m_instance, [idx, outPane] {
                if (m_instance->currentIndex() == idx)
                    m_instance->updateActions(outPane);
            });

            auto *toolBar = new QToolBar(m_instance->m_opToolBarWidgets);
            toolBar->setContentsMargins(0, 0, 0, 0);
            const QList<QWidget *> toolBarWidgets = outPane->toolBarWidgets();
            for (QWidget *toolButton : toolBarWidgets)
                toolBar->addWidget(toolButton);
            auto stretch = new QWidget;
            stretch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
            toolBar->addWidget(stretch);

            m_instance->m_opToolBarWidgets->insertWidget(i, toolBar);
        }
        QTC_CHECK(idx == i);

        minTitleWidth = qMax(minTitleWidth, titleFm.horizontalAdvance(outPane->displayName()));

        data.id = baseId.withSuffix(outPane->id().toString());
        if (data.action) {
            ActionManager::unregisterAction(data.action, data.id);
            delete data.action;
        }

        ActionBuilder paneAction(m_instance, data.id);
        paneAction
            .setText(outPane->displayName())
            .addOnTriggered(m_instance, [i] { m_instance->shortcutTriggered(i); })
            // OPENMV-DIFF //
            // .setDefaultKeySequence(paneShortCut(shortcutNumber))
            // OPENMV-DIFF //
            .addToContainer(Constants::M_VIEW_PANES, "Coreplugin.OutputPane.PanesGroup")
            .bindContextAction(&data.action);
        auto button = new OutputPaneToggleButton(shortcutNumber,
                                                 outPane->displayName(),
                                                 paneAction.commandAction());
        data.button = button;
        connect(button, &OutputPaneToggleButton::contextMenuRequested, m_instance, [] {
            // OPENMV-DIFF //
            // m_instance->popupMenu();
            // OPENMV-DIFF //
        });

        connect(outPane, &IOutputPane::flashButton, button, [button] { button->flash(); });
        connect(outPane,
                &IOutputPane::setBadgeNumber,
                button,
                &OutputPaneToggleButton::setIconBadgeNumber);

        ++shortcutNumber;
        m_instance->m_buttonsWidget->layout()->addWidget(data.button);
        connect(data.button, &QAbstractButton::clicked, m_instance, [i] {
            m_instance->buttonTriggered(i);
        });

        // OPENMV-DIFF //
        // const bool visible = outPane->priorityInStatusBar() >= 0;
        // OPENMV-DIFF //
        bool visible = outPane->priorityInStatusBar() >= 0;
        // In viewer mode (read-only telemetry viewer) the output pane area only
        // ever shows the Serial Terminal, which is forced visible on startup, so
        // hide all of the status-bar pane toggle buttons.
        if (isViewerMode())
            visible = false;
        // OPENMV-DIFF //
        data.button->setVisible(visible);
    }

    m_instance->m_titleLabel->setMinimumWidth(
        minTitleWidth + m_instance->m_titleLabel->contentsMargins().left()
        + m_instance->m_titleLabel->contentsMargins().right());
    // OPENMV-DIFF //
    // Upstream 20.0.2 moved the init tail that used to live here into
    // OutputPaneManager::initialize(); OpenMV's customizations (no manage button,
    // deferred startup showPage) are re-applied there instead of duplicated here.
    // OPENMV-DIFF //
}

OutputPaneManager::~OutputPaneManager() = default;

void OutputPaneManager::shortcutTriggered(int idx)
{
    IOutputPane *outputPane = g_outputPanes.at(idx).pane;
    // Now check the special case, the output window is already visible,
    // we are already on that page but the outputpane doesn't have focus
    // then just give it focus.
    int current = currentIndex();
    if (OutputPanePlaceHolder::isCurrentVisible() && current == idx) {
        if ((!m_outputWidgetPane->isActiveWindow() || !outputPane->hasFocus())
            && outputPane->canFocus()) {
            outputPane->setFocus();
            ICore::raiseWindow(m_outputWidgetPane);
        } else {
            slotHide();
        }
    } else {
        // Else do the same as clicking on the button does.
        buttonTriggered(idx);
    }
}

int OutputPaneManager::outputPaneHeightSetting()
{
    return m_instance->m_outputPaneHeightSetting;
}

void OutputPaneManager::setOutputPaneHeightSetting(int value)
{
    m_instance->m_outputPaneHeightSetting = value;
}

bool OutputPaneManager::initialized()
{
    return m_instance && m_instance->m_initialized;
}

void OutputPaneManager::toggleMaximized()
{
    OutputPanePlaceHolder *ph = OutputPanePlaceHolder::getCurrent();
    QTC_ASSERT(ph, return);

    if (!ph->isVisible()) // easier than disabling/enabling the action
        return;
    ph->setMaximized(!ph->isMaximized());
}

void OutputPaneManager::buttonTriggered(int idx)
{
    QTC_ASSERT(idx >= 0, return);
    if (idx == currentIndex() && OutputPanePlaceHolder::isCurrentVisible()) {
        // we should toggle and the page is already visible and we are actually closeable
        slotHide();
    } else {
        showPage(idx, IOutputPane::ModeSwitch | IOutputPane::WithFocus);
    }
}

void OutputPaneManager::readSettings()
{
    QtcSettings *settings = ICore::settings();
    int num = settings->beginReadArray(outputPaneSettingsKeyC);
    for (int i = 0; i < num; ++i) {
        settings->setArrayIndex(i);
        Id id = Id::fromSetting(settings->value(outputPaneIdKeyC));
        const int idx = Utils::indexOf(g_outputPanes, Utils::equal(&OutputPaneData::id, id));
        if (idx < 0) // happens for e.g. disabled plugins (with outputpanes) that were loaded before
            continue;
        // OPENMV-DIFF //
        // const bool visible = settings->value(outputPaneVisibleKeyC).toBool();
        // OPENMV-DIFF //
        // OpenMV removed every UI affordance for hiding the status-bar pane toggle
        // buttons (the manage button and the per-button context menu are both
        // OPENMV-DIFF'd out), so the persisted "visible" value is vestigial. Worse,
        // viewer mode force-hides every button, which would otherwise be saved and
        // bleed back into the normal IDE (hiding e.g. the Search Results toggle), and
        // the IDE would then re-save the hidden state -- self-perpetuating. Derive
        // visibility from the pane's status-bar priority (as initialize() does) rather
        // than the stored value, and keep all buttons hidden in viewer mode.
        bool visible = g_outputPanes[idx].pane->priorityInStatusBar() >= 0;
        if (isViewerMode())
            visible = false;
        // OPENMV-DIFF //
        g_outputPanes[idx].button->setVisible(visible);
    }
    settings->endArray();

    m_outputPaneHeightSetting
        = settings->value("OutputPanePlaceHolder/Height", 0).toInt();
    // OPENMV-DIFF //
    // const int currentIdx
    //     = settings->value("OutputPanePlaceHolder/CurrentIndex", 0).toInt();
    // OPENMV-DIFF //
    const int currentIdx
        = settings->value("OutputPanePlaceHolder/CurrentIndex", 1).toInt();
    m_outputPaneVisibleOnStartup
        = settings->value("OutputPanePlaceHolder/Visible", true).toBool();
    // OPENMV-DIFF //
    // if (QTC_GUARD(currentIdx >= 0 && currentIdx < g_outputPanes.size()))
    //     setCurrentIndex(currentIdx);
    // OPENMV-DIFF //    
    int startupIdx = currentIdx;
    // In viewer mode the pane toggle buttons are hidden, so always open on the
    // Serial Terminal (GeneralMessages) and force the output pane visible.
    if (isViewerMode()) {
        const int gmIdx = Utils::indexOf(g_outputPanes,
            [](const OutputPaneData &d) { return d.pane->id() == Id("GeneralMessages"); });
        if (gmIdx >= 0)
            startupIdx = gmIdx;
        m_outputPaneVisibleOnStartup = true;
    }
    if (QTC_GUARD(startupIdx >= 0 && startupIdx < g_outputPanes.size()))
        setCurrentIndex(startupIdx);
}

void OutputPaneManager::updateActions(IOutputPane *pane)
{
    const bool enabledForMode = m_buttonsWidget->isVisibleTo(m_buttonsWidget->window())
                                || OutputPanePlaceHolder::modeHasOutputPanePlaceholder(
                                    ModeManager::currentModeId());
    m_clearAction->setEnabled(enabledForMode);
    m_minMaxAction->setEnabled(enabledForMode);
    m_prevAction->setEnabled(enabledForMode && pane && pane->canNavigate() && pane->canPrevious());
    m_nextAction->setEnabled(enabledForMode && pane && pane->canNavigate() && pane->canNext());
    // OPENMV-DIFF //
    // Hide the prev/next actions entirely for panes that cannot navigate. Guarded
    // against a null pane to match the upstream 20.0.2 enabled-checks just above
    // (OpenMV's original dereferenced pane unconditionally).
    m_prevAction->setVisible(pane && pane->canNavigate());
    m_nextAction->setVisible(pane && pane->canNavigate());
    // OPENMV-DIFF //
    for (const OutputPaneData &d : std::as_const(g_outputPanes))
        d.action->setEnabled(enabledForMode);
}

void OutputPaneManager::slotNext()
{
    int idx = currentIndex();
    ensurePageVisible(idx);
    IOutputPane *out = g_outputPanes.at(idx).pane;
    if (out->canNext())
        out->goToNext();
}

void OutputPaneManager::slotPrev()
{
    int idx = currentIndex();
    ensurePageVisible(idx);
    IOutputPane *out = g_outputPanes.at(idx).pane;
    if (out->canPrevious())
        out->goToPrev();
}

void OutputPaneManager::slotHide()
{
    // OPENMV-DIFF //
    // In viewer mode the serial terminal must never hide: the editor area above
    // it is hidden and every affordance for re-showing the pane is removed (the
    // status-bar toggle buttons, the close/minmax buttons, and the unattached
    // Output menu), so a hide would be permanent until restart. This is the
    // choke point for every hide path -- the global Esc shortcut (ReturnToEditor
    // -> doEscapeKeyFocusMoveMagic()), the pane toggle actions, and IOutputPane
    // hidePage/togglePage requests -- so guard it here rather than per-caller.
    if (isViewerMode())
        return;
    // OPENMV-DIFF //
    OutputPanePlaceHolder *ph = OutputPanePlaceHolder::getCurrent();
    if (ph) {
        emit ph->visibilityChangeRequested(false);
        ph->setVisible(false);
        int idx = currentIndex();
        QTC_ASSERT(idx >= 0, return);
        g_outputPanes.at(idx).button->setChecked(false);
        g_outputPanes.at(idx).pane->visibilityChanged(false);
        if (IEditor *editor = EditorManager::currentEditor()) {
            QWidget *w = editor->widget()->focusWidget();
            if (!w)
                w = editor->widget();
            w->setFocus();
        }
    }
}

void OutputPaneManager::ensurePageVisible(int idx)
{
    //int current = currentIndex();
    //if (current != idx)
    //    m_outputWidgetPane->setCurrentIndex(idx);
    setCurrentIndex(idx);
}

void OutputPaneManager::showPage(int idx, int flags)
{
    QTC_ASSERT(idx >= 0, return);
    OutputPanePlaceHolder *ph = OutputPanePlaceHolder::getCurrent();

    if (!ph && flags & IOutputPane::ModeSwitch) {
        // In this mode we don't have a placeholder
        // switch to the output mode and switch the page
        ModeManager::activateMode(Id(Constants::MODE_EDIT));
        ph = OutputPanePlaceHolder::getCurrent();
    }

    if (!ph) {
        g_outputPanes.at(idx).button->flash();
        return;
    }

    emit ph->visibilityChangeRequested(true);
    ph->setVisible(true);

    ensurePageVisible(idx);
    IOutputPane *out = g_outputPanes.at(idx).pane;
    if (flags & IOutputPane::WithFocus) {
        if (out->canFocus())
            out->setFocus();
        ICore::raiseWindow(m_outputWidgetPane);
    }

    if (flags & IOutputPane::EnsureSizeHint)
        ph->ensureSizeHintAsMinimum();
}

void OutputPaneManager::focusInEvent(QFocusEvent *e)
{
    if (QWidget *w = m_outputWidgetPane->currentWidget())
        w->setFocus(e->reason());
}

bool OutputPaneManager::eventFilter(QObject *o, QEvent *e)
{
    if (o == m_buttonsWidget && (e->type() == QEvent::Show || e->type() == QEvent::Hide)) {
        const int index = currentIndex();
        updateActions(index >= 0 ? g_outputPanes.at(index).pane : nullptr);
    }
    return false;
}

void OutputPaneManager::setCurrentIndex(int idx)
{
    static int lastIndex = -1;

    if (lastIndex != -1) {
        g_outputPanes.at(lastIndex).button->setChecked(false);
        g_outputPanes.at(lastIndex).pane->visibilityChanged(false);
    }

    if (idx != -1) {
        m_outputWidgetPane->setCurrentIndex(idx);
        m_opToolBarWidgets->setCurrentIndex(idx);

        OutputPaneData &data = g_outputPanes[idx];
        IOutputPane *pane = data.pane;
        // OPENMV-DIFF //
        // data.button->show();
        // OPENMV-DIFF //
        // In viewer mode the status-bar pane toggle buttons stay hidden, so
        // don't re-show the current pane's button when it becomes active.
        if (!isViewerMode())
            data.button->show();
        // OPENMV-DIFF //
        if (OutputPanePlaceHolder::isCurrentVisible())
            pane->visibilityChanged(true);

        updateActions(pane);
        g_outputPanes.at(idx).button->setChecked(OutputPanePlaceHolder::isCurrentVisible());
        m_titleLabel->setText(pane->displayName());
    }

    lastIndex = idx;
}

void OutputPaneManager::popupMenu()
{
    QMenu menu;
    int idx = 0;
    for (OutputPaneData &data : g_outputPanes) {
        QAction *act = menu.addAction(data.pane->displayName());
        act->setCheckable(true);
        act->setChecked(data.button->isPaneVisible());
        connect(act, &QAction::triggered, this, [this, data, idx] {
            if (data.button->isPaneVisible()) {
                data.pane->visibilityChanged(false);
                data.button->setChecked(false);
                data.button->hide();
            } else {
                showPage(idx, IOutputPane::ModeSwitch);
            }
        });
        ++idx;
    }

    menu.addSeparator();
    QAction *reset = menu.addAction(Tr::tr("Reset to Default"));
    connect(reset, &QAction::triggered, this, [this] {
        for (int i = 0; i < g_outputPanes.size(); ++i) {
            OutputPaneData &data = g_outputPanes[i];
            const bool buttonVisible = data.pane->priorityInStatusBar() >= 0;
            const bool paneVisible = currentIndex() == i
                                     && OutputPanePlaceHolder::isCurrentVisible();
            if (buttonVisible) {
                data.button->setChecked(paneVisible);
                data.button->setVisible(true);
            } else {
                data.button->setChecked(false);
                data.button->hide();
            }
        }
    });

    menu.exec(QCursor::pos());
}

void OutputPaneManager::saveSettings() const
{
    QtcSettings *settings = ICore::settings();
    const int n = g_outputPanes.size();
    settings->beginWriteArray(outputPaneSettingsKeyC, n);
    for (int i = 0; i < n; ++i) {
        const OutputPaneData &data = g_outputPanes.at(i);
        settings->setArrayIndex(i);
        settings->setValue(outputPaneIdKeyC, data.id.toSetting());
        settings->setValue(outputPaneVisibleKeyC, data.button->isPaneVisible());
    }
    settings->endArray();
    int heightSetting = m_outputPaneHeightSetting;
    // update if possible
    if (OutputPanePlaceHolder *curr = OutputPanePlaceHolder::getCurrent())
        heightSetting = curr->nonMaximizedSize();
    settings->setValue("OutputPanePlaceHolder/Height", heightSetting);
    settings->setValue("OutputPanePlaceHolder/CurrentIndex", currentIndex());
    // OPENMV-DIFF //
    settings->setValue("OutputPanePlaceHolder/Visible", OutputPanePlaceHolder::isCurrentVisible());
    // OPENMV-DIFF //
}

void OutputPaneManager::clearPage()
{
    int idx = currentIndex();
    if (idx >= 0)
        g_outputPanes.at(idx).pane->clearContents();
}

int OutputPaneManager::currentIndex() const
{
    return m_outputWidgetPane->currentIndex();
}


///////////////////////////////////////////////////////////////////////
//
// OutputPaneToolButton
//
///////////////////////////////////////////////////////////////////////

OutputPaneToggleButton::OutputPaneToggleButton(int number, const QString &text, QAction *action)
    : m_number(QString::number(number))
    , m_text(text)
    , m_action(action)
    , m_flashTimer(new QTimeLine(1000, this))
{
    setFocusPolicy(Qt::NoFocus);
    setCheckable(true);
    QFont fnt = QApplication::font();
    setFont(fnt);
    if (m_action)
        connect(m_action, &QAction::changed, this, &OutputPaneToggleButton::updateToolTip);

    m_flashTimer->setDirection(QTimeLine::Forward);
    m_flashTimer->setEasingCurve(QEasingCurve::SineCurve);
    m_flashTimer->setFrameRange(0, 92);
    auto updateSlot = QOverload<>::of(&QWidget::update);
    connect(m_flashTimer, &QTimeLine::valueChanged, this, updateSlot);
    connect(m_flashTimer, &QTimeLine::finished, this, updateSlot);
    updateToolTip();
}

void OutputPaneToggleButton::updateToolTip()
{
    QTC_ASSERT(m_action, return);
    setToolTip(m_action->toolTip());
}

QSize OutputPaneToggleButton::sizeHint() const
{
    ensurePolished();

    QSize s = fontMetrics().size(Qt::TextSingleLine, m_text);

    // Expand to account for border image
    // OPENMV-DIFF //
    // s.rwidth() += numberAreaWidth() + 1 + buttonBorderWidth + buttonBorderWidth;
    // OPENMV-DIFF //
    s.rwidth() += 1 + buttonBorderWidth + buttonBorderWidth;
    // OPENMV-DIFF //

    if (!m_badgeNumberLabel.text().isNull())
        s.rwidth() += m_badgeNumberLabel.sizeHint().width() + 1;

    return s;
}

static QRect bgRect(const QRect &widgetRect)
{
    // Removes/compensates the left and right margins of StyleHelper::drawPanelBgRect
    return StyleHelper::toolbarStyle() == StyleHelper::ToolbarStyle::Compact
               ? widgetRect : widgetRect.adjusted(-2, 0, 2, 0);
}

void OutputPaneToggleButton::paintEvent(QPaintEvent*)
{
    const QFontMetrics fm = fontMetrics();
    // OPENMV-DIFF //
    // const int baseLine = (height() - fm.height() + 1) / 2 + fm.ascent();
    // const int numberWidth = fm.horizontalAdvance(m_number);
    // OPENMV-DIFF //
    int baseLine = (height() - fm.height() + 1) / 2 + fm.ascent();
    // OPENMV-DIFF //

    QPainter p(this);

    QStyleOption styleOption;
    styleOption.initFrom(this);
    const bool hovered = !HostOsInfo::isMacHost() && (styleOption.state & QStyle::State_MouseOver);

    if (creatorTheme()->flag(Theme::FlatToolBars)) {
        Theme::Color c = Theme::BackgroundColorDark;

        if (hovered)
            c = Theme::BackgroundColorHover;
        else if (isDown() || isChecked())
            c = Theme::BackgroundColorSelected;

        if (c != Theme::BackgroundColorDark)
            StyleHelper::drawPanelBgRect(&p, bgRect(rect()), creatorColor(c));
    } else {
        const QImage *image = nullptr;
        if (isDown()) {
            static const QImage pressed(
                        StyleHelper::dpiSpecificImageFile(":/utils/images/panel_button_pressed.png"));
            image = &pressed;
        } else if (isChecked()) {
            if (hovered) {
                static const QImage checkedHover(
                            StyleHelper::dpiSpecificImageFile(":/utils/images/panel_button_checked_hover.png"));
                image = &checkedHover;
            } else {
                static const QImage checked(
                            StyleHelper::dpiSpecificImageFile(":/utils/images/panel_button_checked.png"));
                image = &checked;
            }
        } else {
            if (hovered) {
                static const QImage hover(
                            StyleHelper::dpiSpecificImageFile(":/utils/images/panel_button_hover.png"));
                image = &hover;
            } else {
                static const QImage button(
                            StyleHelper::dpiSpecificImageFile(":/utils/images/panel_button.png"));
                image = &button;
            }
        }
        if (image)
            // OPENMV-DIFF //
            // StyleHelper::drawCornerImage(*image, &p, rect(), numberAreaWidth(), buttonBorderWidth, buttonBorderWidth, buttonBorderWidth);
            // OPENMV-DIFF //
            StyleHelper::drawCornerImage(*image, &p, rect(), buttonBorderWidth, buttonBorderWidth, buttonBorderWidth, buttonBorderWidth);
            // OPENMV-DIFF //
    }

    if (m_flashTimer->state() == QTimeLine::Running)
    {
        QColor c = creatorColor(Theme::OutputPaneButtonFlashColor);
        c.setAlpha (m_flashTimer->currentFrame());
        if (creatorTheme()->flag(Theme::FlatToolBars))
            StyleHelper::drawPanelBgRect(&p, bgRect(rect()), c);
        else
            // OPENMV-DIFF //
            // p.fillRect(rect().adjusted(numberAreaWidth(), 1, -1, -1), c);
            // OPENMV-DIFF //
            p.fillRect(rect().adjusted(1, 1, -1, -1), c);
            // OPENMV-DIFF //
    }

    p.setFont(font());
    p.setPen(creatorColor(Theme::OutputPaneToggleButtonTextColorChecked));
    // OPENMV-DIFF //
    // p.drawText((numberAreaWidth() - numberWidth) / 2, baseLine, m_number);
    // OPENMV-DIFF //
    if (!isChecked())
        p.setPen(creatorColor(Theme::OutputPaneToggleButtonTextColorUnchecked));
    // OPENMV-DIFF //
    // int leftPart = numberAreaWidth() + buttonBorderWidth;
    // OPENMV-DIFF //
    int leftPart = 1 + buttonBorderWidth;
    // OPENMV-DIFF //
    int labelWidth = 0;
    if (!m_badgeNumberLabel.text().isEmpty()) {
        const QSize labelSize = m_badgeNumberLabel.sizeHint();
        labelWidth = labelSize.width() + 3;
        m_badgeNumberLabel.paint(&p, width() - labelWidth, (height() - labelSize.height()) / 2, isChecked());
    }
    // OPENMV-DIFF //
#ifndef Q_OS_LINUX
    baseLine = baseLine - 1;
#endif
    // OPENMV-DIFF //
    p.drawText(leftPart, baseLine, fm.elidedText(m_text, Qt::ElideRight, width() - leftPart - 1 - labelWidth));
}

void OutputPaneToggleButton::checkStateSet()
{
    //Stop flashing when button is checked
    QToolButton::checkStateSet();
    m_flashTimer->stop();
}

void OutputPaneToggleButton::flash(int count)
{
    setVisible(true);
    //Start flashing if button is not checked
    if (!isChecked()) {
        m_flashTimer->setLoopCount(count);
        if (m_flashTimer->state() != QTimeLine::Running)
            m_flashTimer->start();
        update();
    }
}

void OutputPaneToggleButton::setIconBadgeNumber(int number)
{
    QString text = (number ? QString::number(number) : QString());
    m_badgeNumberLabel.setText(text);
    updateGeometry();
}

bool OutputPaneToggleButton::isPaneVisible() const
{
    return isVisibleTo(parentWidget());
}

void OutputPaneToggleButton::contextMenuEvent(QContextMenuEvent *)
{
    emit contextMenuRequested();
}

///////////////////////////////////////////////////////////////////////
//
// OutputPaneManageButton
//
///////////////////////////////////////////////////////////////////////

OutputPaneManageButton::OutputPaneManageButton()
{
    setFocusPolicy(Qt::NoFocus);
    setCheckable(true);
    setFixedWidth(StyleHelper::toolbarStyle() == Utils::StyleHelper::ToolbarStyle::Compact ? 17
                                                                                           : 21);
    connect(this, &QToolButton::clicked, this, &OutputPaneManageButton::menuRequested);
}

void OutputPaneManageButton::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    if (!creatorTheme()->flag(Theme::FlatToolBars)) {
        static const QImage button(StyleHelper::dpiSpecificImageFile(QStringLiteral(":/utils/images/panel_manage_button.png")));
        StyleHelper::drawCornerImage(button, &p, rect(), buttonBorderWidth, buttonBorderWidth, buttonBorderWidth, buttonBorderWidth);
    }
    QStyle *s = style();
    QStyleOption arrowOpt;
    arrowOpt.initFrom(this);
    constexpr int arrowSize = 8;
    arrowOpt.rect = QRect(0, 0, arrowSize, arrowSize);
    arrowOpt.rect.moveCenter(rect().center());
    arrowOpt.rect.translate(0, -3);
    s->drawPrimitive(QStyle::PE_IndicatorArrowUp, &arrowOpt, &p, this);
    arrowOpt.rect.translate(0, 6);
    s->drawPrimitive(QStyle::PE_IndicatorArrowDown, &arrowOpt, &p, this);
}

void OutputPaneManageButton::contextMenuEvent(QContextMenuEvent *)
{
    emit menuRequested();
}

BadgeLabel::BadgeLabel()
{
    m_font = QApplication::font();
    m_font.setBold(true);
    m_font.setPixelSize(11);
}

void BadgeLabel::paint(QPainter *p, int x, int y, bool isChecked)
{
    const QRectF rect(QRect(QPoint(x, y), m_size));
    p->save();

    p->setBrush(creatorColor(isChecked? Theme::BadgeLabelBackgroundColorChecked
                                      : Theme::BadgeLabelBackgroundColorUnchecked));
    p->setPen(Qt::NoPen);
    p->setRenderHint(QPainter::Antialiasing, true);
    p->drawRoundedRect(rect, m_padding, m_padding, Qt::AbsoluteSize);

    p->setFont(m_font);
    p->setPen(creatorColor(isChecked ? Theme::BadgeLabelTextColorChecked
                                     : Theme::BadgeLabelTextColorUnchecked));
    p->drawText(rect, Qt::AlignCenter, m_text);

    p->restore();
}

void BadgeLabel::setText(const QString &text)
{
    m_text = text;
    calculateSize();
}

QString BadgeLabel::text() const
{
    return m_text;
}

QSize BadgeLabel::sizeHint() const
{
    return m_size;
}

void BadgeLabel::calculateSize()
{
    const QFontMetrics fm(m_font);
    m_size = fm.size(Qt::TextSingleLine, m_text);
    m_size.setWidth(m_size.width() + m_padding * 1.5);
    m_size.setHeight(2 * m_padding + 1); // Needs to be uneven for pixel perfect vertical centering in the button
}

} // namespace Internal
} // namespace Core

#include "outputpanemanager.moc"
