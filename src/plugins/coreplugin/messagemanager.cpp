// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "messagemanager.h"

#include "coreconstants.h"
#include "coreplugintr.h"
#include "icontext.h"
#include "ioutputpane.h"
#include "outputwindow.h"

#include <utils/qtcassert.h>
#include <utils/shutdownguard.h>
#include <utils/utilsicons.h>

#include <QFont>
#include <QPointer>

// OPENMV-DIFF //
#include "icore.h"
#include "coreicons.h"
#include "actionmanager/actionmanager.h"
#include "actionmanager/command.h"

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QToolButton>
// OPENMV-DIFF //

/*!
    \namespace Core::MessageManager
    \inheaderfile coreplugin/messagemanager.h
    \ingroup mainclasses
    \inmodule QtCreator

    \brief The MessageManager namespace is used to post messages in the
    \uicontrol{General Messages} pane.
*/

using namespace Core::Internal;

namespace Core::MessageManager {

const char zoomSettingsKey[] = "Core/MessageOutput/Zoom";
// OPENMV-DIFF //
const char wrapSettingsKey[] = "Core/MessageOutput/WrapText";
const char serialDebugLevelKey[] = "Core/MessageOutput/SerialDebugLevel";
// OPENMV-DIFF //

class MessageOutputWindow final : public IOutputPane
{
public:
    explicit MessageOutputWindow(QObject *parent)
        // OPENMV-DIFF //
        // OpenMV's IOutputPane takes (bool hidden, QObject *parent); passing the
        // parent pointer alone binds it to `hidden` (pointer->bool == true), which
        // silently hides the Serial Terminal pane. Pass hidden=false explicitly.
        // : IOutputPane(parent)
        : IOutputPane(false, parent)
        // OPENMV-DIFF //
    {
        setId("GeneralMessages");
        // OPENMV-DIFF //
        // setDisplayName(Tr::tr("General Messages"));
        // setPriorityInStatusBar(-100);
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

        setupFilterUi("MessageOutputPane.Filter", "Core::Internal::MessageOutputWindow");
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
        connect(m_wrapAction, &QAction::toggled, this, [this] (bool checked) {
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

    ~MessageOutputWindow() final
    {
        delete m_widget;
    }

    void append(const QString &text)
    {
        // OPENMV-DIFF //
        // m_widget->appendMessage(text, Utils::GeneralMessageFormat);
        m_widget->appendText(text);
        // OPENMV-DIFF //
    }

    void clearLinesPrefixedWith(const QString &prefix, bool deleteTrailingLineBreak)
    {
        m_widget->clearLinesPrefixedWith(prefix, deleteTrailingLineBreak);
    }

    // OPENMV-DIFF //
    OutputWindow *widget() const { return m_widget; }

    QList<QWidget *> toolBarWidgets() const final
    {
        return QList<QWidget *>() << m_saveButton << m_wrapButton << m_debugButton;
    }
    // OPENMV-DIFF //

private:
    QWidget *outputWidget(QWidget *parent) final
    {
        m_widget->setParent(parent);
        return m_widget;
    }

    void clearContents() final { m_widget->clear(); }

    bool canFocus() const final { return true; }
    bool hasFocus() const final { return m_widget->window()->focusWidget() == m_widget; }
    void setFocus() final { m_widget->setFocus(); }

    bool canNext() const final { return false; }
    bool canPrevious() const final { return false; }
    void goToNext() final {}
    void goToPrev() final {}
    bool canNavigate() const final { return false; }

    bool hasFilterContext() const final { return true; }

    void updateFilter() final
    {
        m_widget->updateFilterProperties(filterText(), filterCaseSensitivity(), filterUsesRegexp(),
                                         filterIsInverted(), beforeContext(), afterContext());
    }

    OutputWindow *m_widget = nullptr;
    // OPENMV-DIFF //
    QToolButton *m_saveButton = nullptr;
    QToolButton *m_wrapButton = nullptr;
    QToolButton *m_debugButton = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_wrapAction = nullptr;
    // OPENMV-DIFF //
};

static MessageOutputWindow *messageOutputWindow()
{
    static QPointer<MessageOutputWindow> theMessageOutputWindow
            = new MessageOutputWindow(Utils::shutdownGuard());
    return theMessageOutputWindow.get();
}

enum class Flag { Silent, Flash, Disrupt };

static void showOutputPane(Flag flags)
{
    QTC_ASSERT(messageOutputWindow(), return);
    switch (flags) {
    case Flag::Silent:
        break;
    case Flag::Flash:
        messageOutputWindow()->flash();
        break;
    case Flag::Disrupt:
        messageOutputWindow()->popup(IOutputPane::ModeSwitch | IOutputPane::WithFocus);
        break;
    }
}

static void writeImpl(const QString &text, Flag flags)
{
    // Make sure this end up in the GUI thread.
    QMetaObject::invokeMethod(Utils::shutdownGuard(), [text, flags] {
        QTC_ASSERT(messageOutputWindow(), return);
        showOutputPane(flags);
        messageOutputWindow()->append(text + '\n');
    });
}

/*!
    \internal
*/
void setFont(const QFont &font)
{
    QTC_ASSERT(messageOutputWindow(), return);
    messageOutputWindow()->setFont(font);
}

/*!
    \internal
*/
void setWheelZoomEnabled(bool enabled)
{
    QTC_ASSERT(messageOutputWindow(), return);
    messageOutputWindow()->setWheelZoomEnabled(enabled);
}

void clearLinesPrefixedWith(const QString &prefix, bool deleteTrailingLineBreak)
{
    // Make sure this end up in the GUI thread.
    QMetaObject::invokeMethod(Utils::shutdownGuard(), [prefix, deleteTrailingLineBreak] {
        QTC_ASSERT(messageOutputWindow(), return);
        messageOutputWindow()->clearLinesPrefixedWith(prefix, deleteTrailingLineBreak);
    });
}

/*!
    Writes the \a message to the \uicontrol{General Messages} pane without
    any further action.

    This is the preferred method of posting messages, since it does not
    interrupt the user.

    \sa writeFlashing()
    \sa writeDisrupting()
*/
void writeSilently(const QString &message)
{
    writeImpl(message, Flag::Silent);
}

/*!
    Writes the \a message to the \uicontrol{General Messages} pane and flashes
    the output pane button.

    This notifies the user that something important has happened that might
    require the user's attention. Use sparingly, since continually flashing the
    button is annoying, especially if the condition is something the user might
    not be able to fix.

    \sa writeSilently()
    \sa writeDisrupting()
*/
void writeFlashing(const QString &message)
{
    writeImpl(message, Flag::Flash);
}

/*!
    Writes the \a message to the \uicontrol{General Messages} pane and brings
    the pane to the front.

    This might interrupt a user's workflow, so only use this as a direct
    response to something a user did, like explicitly running a tool.

    \sa writeSilently()
    \sa writeFlashing()
*/
void writeDisrupting(const QString &message)
{
    writeImpl(message, Flag::Disrupt);
}

/*!
    \overload writeSilently()
*/
void writeSilently(const QStringList &messages)
{
    writeSilently(messages.join('\n'));
}

/*!
    \overload writeFlashing()
*/
void writeFlashing(const QStringList &messages)
{
    writeFlashing(messages.join('\n'));
}

/*!
    \overload writeDisrupting()
*/
void writeDisrupting(const QStringList &messages)
{
    writeDisrupting(messages.join('\n'));
}

void popup()
{
    messageOutputWindow()->popup(IOutputPane::NoModeSwitch);
}

// OPENMV-DIFF //
// Adapted to upstream 20.0.2: the global s_messageOutputWindow pointer is gone;
// the pane is now reached through the messageOutputWindow() accessor, and
// m_widget via the widget() accessor added to the class above.
OutputWindow *outputWindow()
{
    QTC_ASSERT(messageOutputWindow(), return nullptr);
    return messageOutputWindow()->widget();
}

void printData(const QByteArray &data)
{
    QTC_ASSERT(messageOutputWindow(), return);
    messageOutputWindow()->append(QString::fromUtf8(data));
}

void grayOutOldContent()
{
    QTC_ASSERT(messageOutputWindow(), return);
    messageOutputWindow()->widget()->grayOutOldContent();
}

void flash()
{
    QTC_ASSERT(messageOutputWindow(), return);
    messageOutputWindow()->flash();
}
// OPENMV-DIFF //

} // namespace Core::MessageManager
