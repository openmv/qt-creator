// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "editmode.h"

#include "coreconstants.h"
#include "coreicons.h"
#include "coreplugintr.h"
#include "editormanager/editormanager.h"
#include "editormanager/ieditor.h"
#include "icore.h"
#include "minisplitter.h"
#include "modemanager.h"
#include "navigationwidget.h"
#include "outputpane.h"
#include "rightpane.h"

// OPENMV-DIFF //
#include <QCoreApplication>
// OPENMV-DIFF //
#include <QVBoxLayout>
#include <QWidget>
#include <QIcon>

namespace Core {
namespace Internal {

EditMode::EditMode() :
    m_splitter(new MiniSplitter),
    m_rightSplitWidgetLayout(new QVBoxLayout)
{
    setObjectName(QLatin1String("EditMode"));
    setDisplayName(Tr::tr("Edit"));
    setIcon(Utils::Icon::modeIcon(Icons::MODE_EDIT_CLASSIC,
                                  Icons::MODE_EDIT_FLAT, Icons::MODE_EDIT_FLAT_ACTIVE));
    setPriority(Constants::P_MODE_EDIT);
    setId(Constants::MODE_EDIT);

    m_rightSplitWidgetLayout->setSpacing(0);
    m_rightSplitWidgetLayout->setContentsMargins(0, 0, 0, 0);
    QWidget *rightSplitWidget = new QWidget;
    rightSplitWidget->setLayout(m_rightSplitWidgetLayout);
    auto editorPlaceHolder = new EditorManagerPlaceHolder;
    // OPENMV-DIFF //
    editorPlaceHolder->setMinimumWidth(160);
    editorPlaceHolder->setMinimumHeight(120);
    // OPENMV-DIFF //
    m_rightSplitWidgetLayout->insertWidget(0, editorPlaceHolder);

    auto rightPaneSplitter = new MiniSplitter;
    rightPaneSplitter->insertWidget(0, rightSplitWidget);
    rightPaneSplitter->insertWidget(1, new RightPanePlaceHolder(Constants::MODE_EDIT));
    rightPaneSplitter->setStretchFactor(0, 1);
    rightPaneSplitter->setStretchFactor(1, 0);

    auto splitter = new MiniSplitter;
    splitter->setOrientation(Qt::Vertical);
    splitter->insertWidget(0, rightPaneSplitter);
    // OPENMV-DIFF //
    // In viewer mode the user can't edit the on-board script, so hide the
    // whole editor area at creation, leaving just the output pane / serial
    // terminal below it. Done here (not from the OpenMV plugin) because the
    // edit mode is built by Core before the plugin's extensionsInitialized
    // runs, so a later findChild() would be too early to catch it.
    if (QCoreApplication::arguments().contains(QLatin1String("-viewer_mode")))
        rightPaneSplitter->hide();
    // OPENMV-DIFF //
    QWidget *outputPane = new OutputPanePlaceHolder(Constants::MODE_EDIT, splitter);
    outputPane->setObjectName(QLatin1String("EditModeOutputPanePlaceHolder"));
    splitter->insertWidget(1, outputPane);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 0);

    m_splitter->insertWidget(0, new NavigationWidgetPlaceHolder(Constants::MODE_EDIT, Side::Left));
    m_splitter->insertWidget(1, splitter);
    m_splitter->insertWidget(2, new NavigationWidgetPlaceHolder(Constants::MODE_EDIT, Side::Right));
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setStretchFactor(2, 0);

    connect(ModeManager::instance(), &ModeManager::currentModeChanged,
            this, &EditMode::grabEditorManager);
    m_splitter->setFocusProxy(editorPlaceHolder);

    auto modeContextObject = new IContext(this);
    modeContextObject->setContext(Context(Constants::C_EDITORMANAGER));
    modeContextObject->setWidget(m_splitter);
    ICore::addContextObject(modeContextObject);

    setWidget(m_splitter);
    setContext(Context(Constants::C_EDIT_MODE,
                       Constants::C_NAVIGATION_PANE));
}

EditMode::~EditMode()
{
    delete m_splitter;
}

void EditMode::grabEditorManager(Utils::Id mode)
{
    if (mode != id())
        return;

    if (EditorManager::currentEditor())
        EditorManager::currentEditor()->widget()->setFocus();
}

} // namespace Internal
} // namespace Core
