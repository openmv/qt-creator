// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

// OPENMV-DIFF //
#include <QIcon>
#include <QVariantMap>
// OPENMV-DIFF //
#include <QWidget>

// OPENMV-DIFF //
#include "acpchatcontroller.h" // PromptImage
// OPENMV-DIFF //

class QComboBox;
class QLabel;
class QStackedWidget;
class QVBoxLayout;
class QGridLayout; // OPENMV-DIFF

namespace Utils {
class FilePath;
class InfoLabel;
}

namespace AcpClient::Internal {

class AcpChatController;
class AcpInspector;
class ChatPanel;
class SessionPickerWidget;

class AcpChatTab : public QWidget
{
    Q_OBJECT

public:
    explicit AcpChatTab(QWidget *parent = nullptr);
    ~AcpChatTab() override;

    void setInspector(AcpInspector *inspector);

    bool hasFocus() const;
    void setFocus();

    QString title() const;
    // OPENMV-DIFF //
    QWidget *sessionToolBar() const; // shown in the AI mode's bar above the tabs
    QIcon icon() const { return m_icon; }     // the agent's logo
    QString toolTip() const;                  // agent name + full chat title

    // A name the user gave the chat; empty = automatic. Kept per session id.
    void setCustomTitle(const QString &title);
    QString customTitle() const { return m_customTitle; }

    // Reopening chats across restarts: the state to save (empty when there is
    // nothing to reopen), and restoring it. A restored chat connects to its agent
    // and loads its session the first time the tab is shown.
    QVariantMap saveState() const;
    void restoreState(const QVariantMap &state);
    // OPENMV-DIFF //
    bool isInUse() const { return !m_currentServerName.isEmpty(); } // OPENMV-DIFF: an agent was picked

signals:
    void titleChanged();

// OPENMV-DIFF //
protected:
    void showEvent(QShowEvent *event) override;
// OPENMV-DIFF //

private:
    void populateServerButtons();
    void connectToConfiguredServer(const QString &serverId, const QString &serverName); // OPENMV-DIFF
    void updateTitle();
    void showSessionPicker();

    // Config page
    QStackedWidget *m_stack;
    QStackedWidget *m_configStack;
    // OPENMV-DIFF //
    // QVBoxLayout *m_serverButtonsLayout = nullptr;
    QVBoxLayout *m_serverGroupsLayout = nullptr;
    // OPENMV-DIFF //
    Utils::InfoLabel *m_noServerLabel;
    QLabel *m_connectionErrorLabel = nullptr;
    QString m_currentServerName;
    // OPENMV-DIFF //
    // The tab is named after the chat: the agent's title for it, else the start of
    // the first message; the agent's name until then.
    QString m_agentTitle;
    QString m_firstPrompt;
    QIcon m_icon;
    QString m_customTitle;
    QString m_currentServerId;
    // A restored chat: connect when first shown, then load this session.
    bool m_restorePending = false;
    QString m_restoreSessionId;
    QString m_restoreWorkingDirectory;
    bool m_loadingRestoredSession = false;
    void fetchIcon(const QString &serverId);
    void rememberCustomTitle();
    // OPENMV-DIFF //

    // Initializing page
    QLabel *m_initializingLabel = nullptr;
    // OPENMV-DIFF //
    QWidget *m_initializingCancel = nullptr;
    bool m_loadingSession = false;
    int m_loadingGeneration = 0; // so a late timer from an earlier load does nothing
    void showLoading(); // the spinner page, "Loading chat...", until the history is in
    void revealCancelLater(); // Cancel hidden, shown if the spinner page is still up in 5 s
    // OPENMV-DIFF //

    // Auth page
    QComboBox *m_authMethodCombo = nullptr;
    QLabel *m_authDescriptionLabel = nullptr;
    QLabel *m_authErrorLabel = nullptr;

    // Chat page
    ChatPanel *m_chatPanel;

    AcpChatController *m_controller;
    SessionPickerWidget *m_activePicker = nullptr;
    QString m_pendingPrompt;
    // OPENMV-DIFF //
    QList<Utils::FilePath> m_pendingFiles;
    QList<PromptImage> m_pendingImages;
    // OPENMV-DIFF //
    QString m_title;
};

} // namespace AcpClient::Internal
