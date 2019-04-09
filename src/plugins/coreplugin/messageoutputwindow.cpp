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

#include "messageoutputwindow.h"
#include "outputwindow.h"
#include "icontext.h"
#include "coreconstants.h"
#include "find/basetextfind.h"

#include <aggregation/aggregate.h>
//OPENMV-DIFF//
#include <coreplugin/coreicons.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
//OPENMV-DIFF//

namespace Core {
namespace Internal {

MessageOutputWindow::MessageOutputWindow()
{
    m_widget = new OutputWindow(Context(Constants::C_GENERAL_OUTPUT_PANE));
    m_widget->setReadOnly(true);
    // Let selected text be colored as if the text edit was editable,
    // otherwise the highlight for searching is too light
    QPalette p = m_widget->palette();
    QColor activeHighlight = p.color(QPalette::Active, QPalette::Highlight);
    p.setColor(QPalette::Highlight, activeHighlight);
    QColor activeHighlightedText = p.color(QPalette::Active, QPalette::HighlightedText);
    p.setColor(QPalette::HighlightedText, activeHighlightedText);
    m_widget->setPalette(p);
    Aggregation::Aggregate *agg = new Aggregation::Aggregate;
    agg->add(m_widget);
    agg->add(new BaseTextFind(m_widget));
    //OPENMV-DIFF//
    p = m_widget->palette();
    p.setColor(QPalette::Base, QColor(QStringLiteral("#1E1E27")));
    p.setColor(QPalette::Text, QColor(QStringLiteral("#EEEEF7")));
    m_widget->setPalette(p);
    m_widget->setMaxLineCount(100000);
    m_widget->setWordWrapEnabled(false);
    m_saveButton = new QToolButton(m_widget);
    m_saveButton->setAutoRaise(true);
    m_saveAction = new QAction(tr("Save"), this);
    m_saveAction->setIcon(Core::Icons::SAVEFILE_TOOLBAR.icon());
    Command *cmd = ActionManager::registerAction(m_saveAction, "Core.MessageOutputWindow.Save");
    cmd->setAttribute(Command::CA_UpdateText);
    m_saveButton->setDefaultAction(cmd->action());
    connect(m_saveAction, &QAction::triggered, m_widget, &OutputWindow::save);
    //OPENMV-DIFF//
}

//OPENMV-DIFF//
QList<QWidget*> MessageOutputWindow::toolBarWidgets() const
{
    return QList<QWidget*>() << m_saveButton;
}
//OPENMV-DIFF//

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
    //OPENMV-DIFF//
    //return tr("General Messages");
    //OPENMV-DIFF//
    return tr("Serial Terminal");
}

void MessageOutputWindow::visibilityChanged(bool /*b*/)
{
}

void MessageOutputWindow::append(const QString &text)
{
    m_widget->appendText(text);
}

int MessageOutputWindow::priorityInStatusBar() const
{
    //OPENMV-DIFF//
    //return -1;
    //OPENMV-DIFF//
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

} // namespace Internal
} // namespace Core
