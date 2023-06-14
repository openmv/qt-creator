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
#include <coreplugin/coreicons.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
// OPENMV-DIFF //

namespace Core {
namespace Internal {

const char zoomSettingsKey[] = "Core/MessageOutput/Zoom";

MessageOutputWindow::MessageOutputWindow()
{
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

QString MessageOutputWindow::displayName() const
{
    // OPENMV-DIFF //
    // return Tr::tr("General Messages");
    // OPENMV-DIFF //
    return Tr::tr("Serial Terminal");
    // OPENMV-DIFF //
}

void MessageOutputWindow::append(const QString &text)
{
    m_widget->appendText(text);
}

int MessageOutputWindow::priorityInStatusBar() const
{
    // OPENMV-DIFF //
    // return -1;
    // OPENMV-DIFF //
    return 1;
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
    return QList<QWidget*>() << m_saveButton;
}
// OPENMV-DIFF //

} // namespace Internal
} // namespace Core
