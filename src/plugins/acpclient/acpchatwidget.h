// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QWidget>

class QTabWidget;
// OPENMV-DIFF //
namespace AcpClient::Internal { class ChatTabs; }
// OPENMV-DIFF //
class QToolButton;

namespace AcpClient::Internal {

class AcpChatTab;
class AcpInspector;

class AcpChatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AcpChatWidget(QWidget *parent = nullptr);
    ~AcpChatWidget() override;

    void setInspector(AcpInspector *inspector);

    void setFocus();

signals:
    void navigateStateUpdate();

private:
    AcpChatTab *addNewTab();
    void closeTab(int index);
    void updateTrailingTab(); // OPENMV-DIFF
    // OPENMV-DIFF //
    void renameTab(int index);  // inline editor on the tab
    void saveOpenChats() const; // reopened at the next start
    bool restoreOpenChats();    // false if there was nothing to reopen
    // OPENMV-DIFF //
    AcpChatTab *currentTab() const;

    // OPENMV-DIFF //
    // QTabWidget *m_tabWidget;
    ChatTabs *m_tabWidget;
    // OPENMV-DIFF //
    AcpInspector *m_inspector = nullptr;
};

} // namespace AcpClient::Internal
