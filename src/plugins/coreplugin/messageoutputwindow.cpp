// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "messageoutputwindow.h"

#include "coreconstants.h"
#include "coreplugintr.h"
#include "icontext.h"
#include "outputwindow.h"

#include <utils/utilsicons.h>

#include <QFont>
#include <QToolButton>

// OPENMV-DIFF //
#include <coreplugin/icore.h>
#include <coreplugin/coreicons.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <QActionGroup>
#include <QMenu>
// OPENMV-DIFF //

namespace Core {
namespace Internal {

const char wrapSettingsKey[] = "Core/MessageOutput/WrapText";
const char zoomSettingsKey[] = "Core/MessageOutput/Zoom";
// OPENMV-DIFF //
const char serialDebugLevelKey[] = "Core/MessageOutput/SerialDebugLevel";
// OPENMV-DIFF //

MessageOutputWindow::MessageOutputWindow()
{
    setId("GeneralMessages");
    // OPENMV-DIFF //
    // setDisplayName(Tr::tr("General Messages"));
    // setPriorityInStatusBar(-100);
    // OPENMV-DIFF //
    setDisplayName(Tr::tr("Serial Terminal"));
    setPriorityInStatusBar(1);
    // OPENMV-DIFF //

    m_widget = new OutputWindow(Context(Constants::C_GENERAL_OUTPUT_PANE), zoomSettingsKey);
    m_widget->setReadOnly(true);

    connect(this, &IOutputPane::zoomInRequested, m_widget, &Core::OutputWindow::zoomIn);
    connect(this, &IOutputPane::zoomOutRequested, m_widget, &Core::OutputWindow::zoomOut);
    connect(this, &IOutputPane::resetZoomRequested, m_widget, &Core::OutputWindow::resetZoom);
    connect(this, &IOutputPane::fontChanged, m_widget, &OutputWindow::setBaseFont);
    connect(this, &IOutputPane::wheelZoomEnabledChanged, m_widget, &OutputWindow::setWheelZoomEnabled);

    setupFilterUi("MessageOutputPane.Filter");
    setFilteringEnabled(true);
    setupContext(Constants::C_GENERAL_OUTPUT_PANE, m_widget);

    // OPENMV-DIFF //
    m_widget->setMaximumBlockCount(100000);
    m_widget->setWordWrapEnabled(false);
    m_saveButton = new QToolButton(m_widget);
    m_saveButton->setAutoRaise(true);
    m_saveAction = new QAction(Tr::tr("Save"), this);
    m_saveAction->setIcon(Utils::Icons::SAVEFILE_TOOLBAR.icon());
    Command *cmd = ActionManager::registerAction(m_saveAction, "Core.MessageOutputWindow.Save");
    cmd->setAttribute(Command::CA_UpdateText);
    m_saveButton->setDefaultAction(cmd->action());
    connect(m_saveAction, &QAction::triggered, m_widget, &OutputWindow::save);
    m_wrapButton = new QToolButton(m_widget);
    m_wrapButton->setAutoRaise(true);
    m_wrapAction = new QAction(Tr::tr("Wrap Text"), this);
    m_wrapAction->setCheckable(true);
    m_wrapAction->setIcon(Utils::Icons::WRAP_TOOLBAR.icon());
    cmd = ActionManager::registerAction(m_wrapAction, "Core.MessageOutputWindow.Wrap");
    cmd->setAttribute(Command::CA_UpdateText);
    m_wrapButton->setDefaultAction(cmd->action());
    connect(m_wrapAction, &QAction::toggled, [this] (bool checked) {
        m_widget->setWordWrapEnabled(checked);
        ICore::settings()->setValue(wrapSettingsKey, checked);
    });
    m_wrapAction->setChecked(ICore::settings()->value(wrapSettingsKey).toBool());

    // Serial-protocol debug logging. Off by default; the popup menu picks the
    // verbosity. The openmv plugin connects to OutputWindow::serialDebugLevelChanged
    // (and reads serialDebugLevel() at startup) to drive the protocol's debug flags --
    // coreplugin stays agnostic of the protocol.
    m_debugButton = new QToolButton(m_widget);
    m_debugButton->setAutoRaise(true);
    m_debugButton->setCheckable(true);
    m_debugButton->setIcon(Utils::Icons::DEBUG_TOOLBAR.icon());
    m_debugButton->setToolTip(Tr::tr("Serial Protocol Debug Logging"));
    m_debugButton->setPopupMode(QToolButton::InstantPopup);
    // Hide the menu-indicator arrow Qt draws in the corner for menu buttons; it
    // clutters the small toolbar glyph (the menu still opens on click).
    m_debugButton->setStyleSheet(QStringLiteral("QToolButton::menu-indicator { image: none; }"));

    QMenu *debugMenu = new QMenu(m_debugButton);
    QActionGroup *debugGroup = new QActionGroup(debugMenu);
    const QStringList debugLevelNames = {
        Tr::tr("Off"),
        Tr::tr("Commands"),
        Tr::tr("Commands + Packets"),
        Tr::tr("Commands + Packets + Fragments"),
    };
    for (int i = 0; i < debugLevelNames.size(); ++i) {
        QAction *levelAction = debugMenu->addAction(debugLevelNames.at(i));
        levelAction->setCheckable(true);
        levelAction->setData(i);
        debugGroup->addAction(levelAction);
    }
    m_debugButton->setMenu(debugMenu);

    int savedLevel = qBound(0, ICore::settings()->value(serialDebugLevelKey, 0).toInt(),
                            debugLevelNames.size() - 1);
    debugGroup->actions().at(savedLevel)->setChecked(true);
    m_debugButton->setChecked(savedLevel > 0);
    m_widget->setSerialDebugLevel(savedLevel);

    connect(debugGroup, &QActionGroup::triggered, this, [this] (QAction *levelAction) {
        const int level = levelAction->data().toInt();
        ICore::settings()->setValue(serialDebugLevelKey, level);
        m_debugButton->setChecked(level > 0);
        m_widget->setSerialDebugLevel(level);
    });
    // OPENMV-DIFF //
}

MessageOutputWindow::~MessageOutputWindow()
{
    delete m_widget;
}

bool MessageOutputWindow::hasFocus() const
{
    return m_widget->window()->focusWidget() == m_widget;
}

bool MessageOutputWindow::canFocus() const
{
    return true;
}

void MessageOutputWindow::setFocus()
{
    m_widget->setFocus();
}

void MessageOutputWindow::clearContents()
{
    m_widget->clear();
}

QWidget *MessageOutputWindow::outputWidget(QWidget *parent)
{
    m_widget->setParent(parent);
    return m_widget;
}

void MessageOutputWindow::append(const QString &text)
{
    // OPENMV-DIFF //
    // m_widget->appendMessage(text, Utils::GeneralMessageFormat);
    // OPENMV-DIFF //
    m_widget->appendText(text);
    // OPENMV-DIFF //
}

bool MessageOutputWindow::canNext() const
{
    return false;
}

bool MessageOutputWindow::canPrevious() const
{
    return false;
}

void MessageOutputWindow::goToNext()
{

}

void MessageOutputWindow::goToPrev()
{

}

bool MessageOutputWindow::canNavigate() const
{
    return false;
}

void MessageOutputWindow::updateFilter()
{
    m_widget->updateFilterProperties(filterText(), filterCaseSensitivity(), filterUsesRegexp(),
                                     filterIsInverted());
}

// OPENMV-DIFF //
QList<QWidget*> MessageOutputWindow::toolBarWidgets() const
{
    return QList<QWidget*>() << m_saveButton << m_wrapButton << m_debugButton;
}
// OPENMV-DIFF //

} // namespace Internal
} // namespace Core
