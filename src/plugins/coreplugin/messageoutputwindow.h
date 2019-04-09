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

#ifndef MESSAGEOUTPUTWINDOW_H
#define MESSAGEOUTPUTWINDOW_H

#include "ioutputpane.h"
//OPENMV-DIFF//
#include <QToolButton>
#include <QAction>
//OPENMV-DIFF//

namespace Core {
class OutputWindow;

namespace Internal {

class MessageOutputWindow : public IOutputPane
{
    Q_OBJECT

public:
    MessageOutputWindow();
    ~MessageOutputWindow();

    QWidget *outputWidget(QWidget *parent);
    //OPENMV-DIFF//
    //QList<QWidget*> toolBarWidgets() const { return QList<QWidget *>(); }
    //OPENMV-DIFF//
    QList<QWidget*> toolBarWidgets() const;
    //OPENMV-DIFF//

    QString displayName() const;
    int priorityInStatusBar() const;
    void clearContents();
    void visibilityChanged(bool visible);

    void append(const QString &text);
    bool canFocus() const;
    bool hasFocus() const;
    void setFocus();

    virtual bool canNext() const;
    virtual bool canPrevious() const;
    virtual void goToNext();
    virtual void goToPrev();
    bool canNavigate() const;

//OPENMV-DIFF//
//private:
//OPENMV-DIFF//
    OutputWindow *m_widget;
    //OPENMV-DIFF//
    QToolButton *m_saveButton;
    QAction *m_saveAction;
    //OPENMV-DIFF//
};

} // namespace Internal
} // namespace Core

#endif // MESSAGEOUTPUTWINDOW_H
