// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "acpauthchoice.h" // OPENMV-DIFF
#include "acpclientobject.h"

#include <acp/acp.h>

#include <QJsonValue>
#include <QObject>
// OPENMV-DIFF //
#include <QHash>
#include <QMap>
// OPENMV-DIFF //

#include <functional>
// OPENMV-DIFF //
#include <QImage>
// OPENMV-DIFF //

#include <utils/filepath.h>

namespace Utils { class Process; }

namespace AcpClient::Internal {

// OPENMV-DIFF //
// An image attached to a prompt (pasted or dropped into the chat input).
struct PromptImage
{
    QByteArray data;        // the encoded image that is sent
    QString mimeType;       // of data: "image/png" or "image/jpeg"
    Utils::FilePath source; // the file it came from, if any
    QImage preview;         // small copy for the UI
};

// A user message with its attachments.
struct OutgoingMessage
{
    QString text;
    QList<Utils::FilePath> files;
    QList<PromptImage> images;
};
// OPENMV-DIFF //

class AcpClientObject;
class AcpFilesystemHandler;
class AcpInspector;
class AcpPermissionHandler;
class AcpTerminalHandler;
class AcpTransport;

class AcpChatController : public QObject
{
    Q_OBJECT

public:
    explicit AcpChatController(QObject *parent = nullptr);
    ~AcpChatController() override;

    void setInspector(AcpInspector *inspector);

    void connectToServer(const QString &serverId);
    void disconnectFromServer();

    void createNewSession(const Utils::FilePath &workingDirectory = {});
    void listSessions(const std::optional<QString> &cursor = {});
    void loadSession(const QString &sessionId, const Utils::FilePath &workingDirectory);
    // OPENMV-DIFF //
    // void sendPrompt(const QString &text, const QList<Utils::FilePath> &additionalFiles = {},
    //                 bool includeCurrentEditor = true);
    void sendPrompt(const QString &text, const QList<Utils::FilePath> &additionalFiles = {},
                    bool includeCurrentEditor = true, const QList<PromptImage> &images = {});
    // Several messages as one prompt (queued ones): each its own text block, with
    // its attachments after it, so the agent's history keeps them apart.
    void sendPrompt(const QList<OutgoingMessage> &messages, bool includeCurrentEditor = true);
    // OPENMV-DIFF //
    void cancelPrompt();
    // OPENMV-DIFF //
    // void authenticate(const QString &methodId);
    //
    // Sign in with one of the agent's methods (see AuthChoice). `values` holds
    // what the user typed for the method's fields, keyed by AuthField::key.
    void authenticate(const QString &methodId, const QMap<QString, QString> &values = {});
    // Ask for sign-in now (the chat's "Sign In..." action), not only after a
    // request fails with "authentication required".
    void requestSignIn();
    bool canSignIn() const { return !m_authChoices.isEmpty(); }
    // OPENMV-DIFF //
    void setConfigOption(const QString &configId, const QString &value);
    void sendPermissionResponse(const QJsonValue &id, const QString &optionId);
    void sendPermissionCancelled(const QJsonValue &id);

    // OPENMV-DIFF //
    // Text the IDE adds to a prompt (unsaved scripts) starts with this, so a
    // replayed chat can leave it out.
    static QString ideContextMarker() { return QStringLiteral("[IDE context]"); }
    // OPENMV-DIFF //

    bool isInitialized() const { return m_initialized; }
    bool supportsSessionList() const;
    const QString &sessionId() const { return m_sessionId; }
    // OPENMV-DIFF //
    const Utils::FilePath &workingDirectory() const { return m_workingDirectory; }
    bool supportsLoadSession() const; // agent supports session/load (resume)
    // OPENMV-DIFF //
    const QString &agentName() const { return m_agentName; }
    const QString &agentVersion() const { return m_agentVersion; }

signals:
    void connectionStateChanged(AcpClientObject::State state);
    void agentInfoReceived(const QString &name, const QString &version, const QString &iconUrl);
    void sessionSelectionRequired();
    void sessionCreated(const QString &sessionId);
    void sessionLoaded(const QString &sessionId);
    void sessionsListed(const QList<Acp::SessionInfo> &sessions,
                        const std::optional<QString> &nextCursor);
    void configOptionsReceived(const QList<Acp::SessionConfigOption> &configOptions);
    void sessionUpdate(const QString &sessionId, const Acp::SessionUpdate &update);
    // OPENMV-DIFF //
    // The agent's report of how full the session's context is (usage_update, newer
    // than the ACP library here); cost is negative when not reported.
    void usageUpdated(const QString &sessionId, qint64 used, qint64 size, double cost,
                      const QString &currency);
    // OPENMV-DIFF //
    // OPENMV-DIFF //
    // void authenticationRequired(const QList<Acp::AuthMethod> &methods);
    void authenticationRequired(const QList<AcpClient::Internal::AuthChoice> &choices);
    // A terminal sign-in is running in the Shell pane.
    void authenticationInProgress(const QString &message);
    void authenticationSucceeded(const QString &message);
    // OPENMV-DIFF //
    void authenticationFailed(const QString &error);
    void permissionRequested(const QJsonValue &id, const Acp::RequestPermissionRequest &request);
    void promptFinished();
    void errorOccurred(const QString &error);

private:
    void onInitializeResult(const Acp::InitializeResponse &response);
    Acp::NewSessionRequest buildNewSessionRequest() const;
    QStringList additionalDirectories() const; // OPENMV-DIFF

    AcpInspector *m_inspector = nullptr;
    AcpTransport *m_transport = nullptr;
    AcpClientObject *m_client = nullptr;
    AcpTerminalHandler *m_terminalHandler = nullptr;
    AcpFilesystemHandler *m_filesystemHandler = nullptr;
    AcpPermissionHandler *m_permissionHandler = nullptr;

    Utils::FilePath m_workingDirectory;
    QString m_sessionId;
    QString m_agentName;
    QString m_agentVersion;
    QString m_serverName;
    QString m_iconUrl;
    QList<Acp::AuthMethod> m_authMethods;
    // OPENMV-DIFF //
    // Sign-in (see authenticate): the methods as the agent sent them; what to
    // redo once signed in (create or reload the session); what to run instead
    // of asking for a session when a relaunched agent has initialized; and a
    // replay to hide while a session is reloaded after a relaunch.
    QList<AuthChoice> m_authChoices;
    QString m_serverId;
    std::function<void()> m_resumeAfterSignIn;
    std::function<void()> m_afterInitialize;
    bool m_relaunching = false;
    QString m_hiddenReplaySessionId;
    Utils::Process *m_signInProcess = nullptr;
    bool handleAuthRequired(const Acp::Error &error, const std::function<void()> &resume);
    void sendAuthenticate(const AuthChoice &choice, const QMap<QString, QString> &values);
    void runTerminalSignIn(const AuthChoice &choice);
    void relaunchAgent(const std::function<void()> &afterInitialize);
    void finishSignIn();
    std::function<void()> resumeCurrentSession();
    // Environment variables given to sign in (env_var methods), per agent, for
    // the rest of this IDE session; every chat with that agent starts with them.
    static QHash<QString, QMap<QString, QString>> &signInEnvironment();
    // OPENMV-DIFF //
    std::optional<Acp::AgentCapabilities> m_agentCapabilities;
    bool m_initialized = false;
    // OPENMV-DIFF //
    // What this chat's agent was last sent about the open scripts (see sendPrompt).
    QHash<Utils::FilePath, size_t> m_sentEditorTexts; // script -> hash of the text sent
    Utils::FilePath m_sentCurrentScript;
    void forgetSentEditorState();
    // OPENMV-DIFF //
};

} // namespace AcpClient::Internal
