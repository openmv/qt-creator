// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QWidget>

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
    bool isInUse() const { return !m_currentServerName.isEmpty(); } // OPENMV-DIFF: an agent was picked

signals:
    void titleChanged();

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

    // Initializing page
    QLabel *m_initializingLabel = nullptr;

    // Auth page
    QComboBox *m_authMethodCombo = nullptr;
    QLabel *m_authDescriptionLabel = nullptr;
    QLabel *m_authErrorLabel = nullptr;

    // Chat page
    ChatPanel *m_chatPanel;

    AcpChatController *m_controller;
    SessionPickerWidget *m_activePicker = nullptr;
    QString m_pendingPrompt;
    QString m_title;
};

} // namespace AcpClient::Internal
