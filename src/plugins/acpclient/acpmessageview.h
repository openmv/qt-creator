// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <acp/acp.h>

#include <QHash>
// OPENMV-DIFF //
#include <QPointer>
// OPENMV-DIFF //
// OPENMV-DIFF //
#include <functional>
// OPENMV-DIFF //
// OPENMV-DIFF //
#include <utils/filepath.h>
// OPENMV-DIFF //
// OPENMV-DIFF //
#include <QImage>
// OPENMV-DIFF //
#include <QJsonValue>
#include <QScrollArea>

QT_BEGIN_NAMESPACE
class QElapsedTimer;
class QLabel;
class QTimer;
class QVBoxLayout;
QT_END_NAMESPACE

namespace Utils { class ProgressIndicator; }

namespace AcpClient::Internal {

class AgentMessageWidget;
class AuthenticationWidget;
class SessionPickerWidget;
class ThoughtWidget;
class UserMessageWidget; // OPENMV-DIFF
class ToolCallDetailWidget;
class ToolCallGroupWidget;

class AcpMessageView : public QScrollArea
{
    Q_OBJECT

public:
    explicit AcpMessageView(QWidget *parent = nullptr);

    void setAgentIconUrl(const QString &iconUrl);
    void setThoughtsVisible(bool visible);
    bool thoughtsVisible() const { return m_thoughtsVisible; }
    bool hasThoughts() const { return !m_thoughtWidgets.isEmpty(); } // OPENMV-DIFF

    void setPrompting(bool prompting);

    void clear();
    // OPENMV-DIFF //
    // void addUserMessage(const QString &text);
    void addUserMessage(const QString &text, const QList<QImage> &images = {},
                        const QList<Utils::FilePath> &files = {});
    void addUserImage(const QImage &image); // to the message just added, if any
    void addUserFile(const Utils::FilePath &file); // likewise
    // OPENMV-DIFF //
    void appendAgentText(const QString &text);
    void appendAgentThought(const QString &text);
    void addToolCall(const Acp::ToolCall &toolCall);
    void updateToolCall(const Acp::ToolCallUpdate &update);
    void addPlan(const Acp::Plan &plan);
    void addStatusMessage(const QString &text);
    void addErrorMessage(const QString &text);
    void finishAgentMessage();

    void addPermissionRequest(const QJsonValue &id,
                              const Acp::RequestPermissionRequest &request);

    void addAuthenticationRequest(const QList<Acp::AuthMethod> &methods);
    void showAuthenticationError(const QString &error);
    void resolveAuthentication();

    SessionPickerWidget *addSessionPicker();

signals:
    void permissionOptionSelected(const QJsonValue &id, const QString &optionId);
    void permissionCancelled(const QJsonValue &id);
    void authenticateRequested(const QString &methodId);

protected:
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override; // OPENMV-DIFF

private:
    void updateElapsedTimeLabel();
    void scrollToBottom();
    // OPENMV-DIFF //
    // QWidget *wrapWithSpacer(QWidget *widget, Qt::Alignment side);
    QWidget *wrapWithSpacer(QWidget *widget, Qt::Alignment side,
                            const std::function<QString()> &copyText = {});
    // OPENMV-DIFF //
    ToolCallGroupWidget *ensureToolCallGroup();
    void finishToolCallGroup();
    void addWidget(QWidget *widget);
    int contentMaxWidth() const;

    QWidget *m_container = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QLabel *m_elapsedLabel = nullptr;
    Utils::ProgressIndicator *m_progressIndicator = nullptr;
    QElapsedTimer *m_elapsedTimer = nullptr;
    QTimer *m_progressUpdateTimer = nullptr;
    AgentMessageWidget *m_currentAgentWidget = nullptr;
    ThoughtWidget *m_currentThoughtWidget = nullptr;
    QList<ThoughtWidget *> m_thoughtWidgets;
    bool m_thoughtsVisible = true;
    ToolCallGroupWidget *m_currentToolCallGroup = nullptr;
    QHash<QString, ToolCallDetailWidget *> m_toolCallDetailWidgets;
    QHash<QString, ToolCallGroupWidget *> m_toolCallGroups; // toolCallId -> owning group
    AuthenticationWidget *m_currentAuthWidget = nullptr;
    QString m_agentIconUrl;
    bool m_autoScroll = true;
    // OPENMV-DIFF //
    // Scrolled up, the view keeps the message at its top where it is while the
    // messages above it change height (re-wrapping on a resize, say).
    QPointer<QWidget> m_anchor;
    int m_anchorOffset = 0; // its top, relative to the view's top
    double m_anchorFraction = -1; // or, scrolled partway into it, how far (0..1)
    bool m_restoringAnchor = false;
    void captureAnchor();
    void restoreAnchor();
    // OPENMV-DIFF //
    UserMessageWidget *m_lastUserMessage = nullptr; // OPENMV-DIFF: while it's the last widget
};

} // namespace AcpClient::Internal
