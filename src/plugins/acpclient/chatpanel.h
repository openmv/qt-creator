// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <acp/acp.h>

// OPENMV-DIFF //
#include "acpchatcontroller.h" // PromptImage
// OPENMV-DIFF //

#include <utils/filepath.h>

#include <QJsonValue>
#include <QMap>
#include <QStringList>
#include <QWidget>

namespace Utils {
class ElidingLabel;
class QtcButton;
class StyledBar;
} // namespace Utils

class QComboBox;
class QHBoxLayout;
class QMimeData; // OPENMV-DIFF
class QLabel;
class QLayout;
class QMenu;
class QToolButton;

namespace AcpClient::Internal {

class AcpMessageView;
class ChatInputEdit;
// OPENMV-DIFF //
class ChatActionButton;
class ContextGauge;
// OPENMV-DIFF //
class SendButton;
class SessionPickerWidget;

class ChatPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPanel(QWidget *parent = nullptr);

    AcpMessageView *messageView() const { return m_messageView; }
    ChatInputEdit *inputEdit() const { return m_inputEdit; }
    // OPENMV-DIFF //
    // The agent / Mode / Model / Effort bar. Not in this panel's layout: the AI
    // mode shows the current chat's bar in the bar above the tabs.
    QWidget *sessionToolBar() const { return m_sessionToolBar; }
    // OPENMV-DIFF //

    void setAgentInfo(const QString &name, const QString &version,
                      const QString &iconUrl = {});
    void setPrompting(bool prompting);
    void setSendEnabled(bool enabled);

    void updateConfigOptions(const QList<Acp::SessionConfigOption> &configOptions);
    void clear();
    void clearConfigOptions();

    void updateAvailableCommands(const QList<Acp::AvailableCommand> &commands);
    // OPENMV-DIFF //
    void setUsage(qint64 used, qint64 size, double cost, const QString &currency);
    // OPENMV-DIFF //
    bool includeCurrentEditorContext() const { return m_includeCurrentEditorContext; }
    QList<Utils::FilePath> manualContextFiles() const { return m_manualContextFiles; }
    // OPENMV-DIFF //
    // Attachments go with one message: these hand them over and clear them.
    QList<Utils::FilePath> takeContextFiles();
    QList<PromptImage> takeImages();
    bool hasAttachments() const { return !m_manualContextFiles.isEmpty() || !m_images.isEmpty(); }
    // The messages a sendRequested() merged (queued ones), to show separately;
    // empty for an ordinary message.
    QList<OutgoingMessage> takeSentParts() { return std::exchange(m_sentParts, {}); }
    // OPENMV-DIFF //

    // Delegate to message view
    // OPENMV-DIFF //
    // void addUserMessage(const QString &text);
    void addUserMessage(const QString &text, const QList<QImage> &images = {},
                        const QList<Utils::FilePath> &files = {});
    void addUserImage(const QImage &image);
    void addUserFile(const Utils::FilePath &file);
    void addStatusMessage(const QString &text);
    // OPENMV-DIFF //
    void appendAgentText(const QString &text);
    void appendAgentThought(const QString &text);
    void addToolCall(const Acp::ToolCall &toolCall);
    void updateToolCall(const Acp::ToolCallUpdate &update);
    void addPlan(const Acp::Plan &plan);
    void addErrorMessage(const QString &text);
    void finishAgentMessage();
    void addPermissionRequest(const QJsonValue &id,
                              const Acp::RequestPermissionRequest &request);

    void addAuthenticationRequest(const QList<Acp::AuthMethod> &methods);
    void showAuthenticationError(const QString &error);
    void resolveAuthentication();

    SessionPickerWidget *addSessionPicker();

protected:
    void resizeEvent(QResizeEvent *event) override;
    // OPENMV-DIFF //
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    // OPENMV-DIFF //

signals:
    void sendRequested(const QString &text);
    void cancelRequested();
    void configOptionChanged(const QString &configId, const QString &value);
    void permissionOptionSelected(const QJsonValue &id, const QString &optionId);
    void permissionCancelled(const QJsonValue &id);
    void authenticateRequested(const QString &methodId);

private:
    // Session toolbar
    QLabel *m_agentLabel;
    QWidget *m_sessionToolBar = nullptr; // OPENMV-DIFF
    QWidget *m_configOptionsWidget = nullptr;
    QHBoxLayout *m_configOptionsLayout;
    QToolButton *m_configOverflowButton = nullptr;
    QMap<QString, QComboBox*> m_configCombos; // configId -> combo
    QMap<QString, QLabel*> m_configLabels;    // configId -> label
    QStringList m_configOrder;                // insertion order of configIds

    void updateConfigOverflow();
    void updateComboDividers(); // OPENMV-DIFF

    // Message area
    AcpMessageView *m_messageView;

    // Input
    ChatInputEdit *m_inputEdit;
    // OPENMV-DIFF //
    // Utils::QtcButton *m_sendButton;
    // Utils::QtcButton *m_commandsButton;
    ChatActionButton *m_sendButton;
    ChatActionButton *m_commandsButton;
    ContextGauge *m_contextGauge = nullptr;
    bool m_canCompact = false; // the agent has a "compact" command
    // OPENMV-DIFF //
    QMenu *m_commandsMenu = nullptr;
    QWidget *m_contextBar = nullptr;
    QLayout *m_contextBarLayout = nullptr;
    bool m_includeCurrentEditorContext = true;
    QList<Utils::FilePath> m_manualContextFiles;
    // OPENMV-DIFF //
    QList<PromptImage> m_images;
    // Messages written while the agent works, each with its attachments: sent
    // together as one prompt when it's done (so the agent sees them all at once),
    // and shown as separate messages.
    QList<OutgoingMessage> m_queue;
    QList<OutgoingMessage> m_sentParts;
    QWidget *m_queueBar = nullptr;
    void queueMessage(const QString &text);
    void unqueueMessage(int index); // back into the input, for editing
    void unqueueAll();
    void sendQueuedMessages();
    void updateQueueBar();
    void requestCancel();
    bool attachFromMimeData(const QMimeData *mimeData);
    void attachImage(const QImage &image, const Utils::FilePath &source);
    // OPENMV-DIFF //

    void updateContextBar();

    bool m_prompting = false;
};

} // namespace AcpClient::Internal
