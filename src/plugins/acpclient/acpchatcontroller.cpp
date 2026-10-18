// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acpchatcontroller.h"
#include "acpclientobject.h"
#include "acpclienttr.h"
#include "acpfilesystemhandler.h"
#include "acpinspector.h"
#include "acppermissionhandler.h"
#include "acpsettings.h"
#include "acpstdiotransport.h"
#include "acpterminalhandler.h"

#include <coreplugin/mcp/mcpmanager.h>

#include <utils/algorithm.h>
#include <utils/filepath.h>
// OPENMV-DIFF //
#include <utils/temporarydirectory.h>
// OPENMV-DIFF //

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

// OPENMV-DIFF //
#include <coreplugin/editormanager/documentmodel.h>
#include <coreplugin/editormanager/editormanager.h>
// OPENMV-DIFF //

#include <QLoggingCategory>
// OPENMV-DIFF //
#include <QDir>
#include <QStandardPaths>
// OPENMV-DIFF //
#include <QUuid>
// OPENMV-DIFF //
// OPENMV-DIFF //

static Q_LOGGING_CATEGORY(logController, "qtc.acpclient.controller", QtWarningMsg);

// OPENMV-DIFF //
// OpenMV IDE has no projects, so a session would otherwise start with an empty
// cwd (agents reject that). Use the IDE's scripts folder, else the home directory.
static Utils::FilePath withDefaultWorkingDirectory(const Utils::FilePath &workingDirectory)
{
    if (!workingDirectory.isEmpty())
        return workingDirectory;
    const Utils::FilePath documents = Utils::FilePath::fromString(
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + QStringLiteral("/OpenMV"));
    return documents.isDir() ? documents : Utils::FilePath::fromString(QDir::homePath());
}
// OPENMV-DIFF //

using namespace Acp;
using namespace Utils;

namespace AcpClient::Internal {

AcpChatController::AcpChatController(QObject *parent)
    : QObject(parent)
{
}

AcpChatController::~AcpChatController()
{
    disconnectFromServer();
}

void AcpChatController::setInspector(AcpInspector *inspector)
{
    m_inspector = inspector;
}

void AcpChatController::connectToServer(const QString &serverId)
{
    disconnectFromServer();

    if (serverId.isEmpty())
        return;

    const QList<AcpSettings::ServerInfo> servers = AcpSettings::servers();
    const auto it = std::find_if(servers.begin(), servers.end(),
                                  [&serverId](const AcpSettings::ServerInfo &s) {
                                      return s.id == serverId;
                                  });
    // OPENMV-DIFF //
    // if (it == servers.end())
    //     return;
    // OPENMV-DIFF //
    // Say so instead of leaving the caller on "Connecting..." forever.
    if (it == servers.end()) {
        emit errorOccurred(Tr::tr("This agent is no longer available. Choose an agent to "
                                  "start a new chat."));
        return;
    }
    // OPENMV-DIFF //

    const AcpSettings::ServerInfo &serverInfo = *it;
    m_serverName = serverInfo.name;
    m_iconUrl = serverInfo.iconUrl;

    const CommandLine &cmdLine = serverInfo.launchCommand;
    auto *transport = new AcpStdioTransport(this);
    const FilePath command = cmdLine.executable();
    transport->setCommandLine(CommandLine(command, cmdLine.arguments(), CommandLine::Raw));
    if (serverInfo.envChanges.hasItems()) {
        Environment env = command.deviceEnvironment();
        serverInfo.envChanges.modifyEnvironment(env, nullptr);
        transport->setEnvironment(env);
    }
    m_transport = transport;

    m_client = new AcpClientObject(m_transport, this);
    if (m_inspector)
        m_client->setInspector(m_inspector, m_serverName);
    m_terminalHandler = new AcpTerminalHandler(m_client, this);
    m_filesystemHandler = new AcpFilesystemHandler(m_client, this);
    // OPENMV-DIFF //
    // The agent's own edits to an open script needn't be sent back to it.
    connect(m_filesystemHandler, &AcpFilesystemHandler::editorTextWritten,
            this, [this](const FilePath &filePath, const QString &text) {
        m_sentEditorTexts.insert(filePath, qHash(text));
    });
    // OPENMV-DIFF //
    m_permissionHandler = new AcpPermissionHandler(m_client, this);

    connect(m_permissionHandler, &AcpPermissionHandler::permissionRequested,
            this, &AcpChatController::permissionRequested);

    connect(m_client, &AcpClientObject::stateChanged,
            this, &AcpChatController::connectionStateChanged);
    connect(m_client, &AcpClientObject::sessionUpdate,
            this, &AcpChatController::sessionUpdate);
    connect(m_client, &AcpClientObject::usageUpdated,
            this, &AcpChatController::usageUpdated); // OPENMV-DIFF
    connect(m_client, &AcpClientObject::initializeResult,
            this, &AcpChatController::onInitializeResult);
    connect(m_client, &AcpClientObject::errorOccurred,
            this, &AcpChatController::errorOccurred);

    connect(m_transport, &AcpTransport::started, this, [this] {
        InitializeRequest initReq;
        initReq.protocolVersion(1);
        Implementation clientInfoImpl;
        clientInfoImpl.name(QStringLiteral("QtCreator"));
        clientInfoImpl.version(QStringLiteral("1.0"));
        initReq.clientInfo(clientInfoImpl);

        ClientCapabilities caps;
        caps.terminal(true);
        FileSystemCapabilities fsCaps;
        fsCaps.readTextFile(true);
        fsCaps.writeTextFile(true);
        caps.fs(fsCaps);
        initReq.clientCapabilities(caps);

        m_client->initialize(initReq);
    });

    m_transport->start();
}

void AcpChatController::disconnectFromServer()
{
    const bool wasConnected
       = m_client && m_client->state() != AcpClientObject::State::Disconnected;

    if (m_transport)
        m_transport->stop();

    if (m_permissionHandler) {
        m_permissionHandler->deleteLater();
        m_permissionHandler = nullptr;
    }
    if (m_filesystemHandler) {
        m_filesystemHandler->deleteLater();
        m_filesystemHandler = nullptr;
    }
    if (m_terminalHandler) {
        m_terminalHandler->deleteLater();
        m_terminalHandler = nullptr;
    }
    if (m_client) {
        m_client->deleteLater();
        m_client = nullptr;
    }
    if (m_transport) {
        m_transport->deleteLater();
        m_transport = nullptr;
    }

    m_workingDirectory.clear();
    m_sessionId.clear();
    forgetSentEditorState(); // OPENMV-DIFF
    m_agentName.clear();
    m_agentVersion.clear();
    m_serverName.clear();
    m_authMethods.clear();
    m_agentCapabilities.reset();
    m_initialized = false;

    if (wasConnected)
        emit connectionStateChanged(AcpClientObject::State::Disconnected);
}

void AcpChatController::createNewSession(const FilePath &workingDirectory)
{
    if (!m_client || !m_initialized)
        return;

    m_workingDirectory = withDefaultWorkingDirectory(workingDirectory); // OPENMV-DIFF

    m_client->newSession(buildNewSessionRequest(),
                         [this](const QJsonObject &result, const std::optional<Error> &error) {
        if (error) {
            if (error->code() == ErrorCode::Authentication_required && !m_authMethods.isEmpty()) {
                emit authenticationRequired(m_authMethods);
                return;
            }
            emit errorOccurred(Tr::tr("Session error: %1").arg(error->message()));
            return;
        }
        auto response = fromJson<NewSessionResponse>(QJsonValue(result));
        if (!response) {
            emit errorOccurred(Tr::tr("Session error: invalid response."));
            return;
        }

        m_sessionId = response->sessionId();
        forgetSentEditorState(); // OPENMV-DIFF
        emit sessionCreated(m_sessionId);

        if (const auto &opts = response->configOptions(); opts.has_value() && !opts->isEmpty()) {
            qCDebug(logController) << "Session configOptions raw array size:" << opts->size();
            QList<SessionConfigOption> configOptions;
            for (const QJsonValue &item : *opts) {
                auto opt = fromJson<SessionConfigOption>(item);
                if (opt)
                    configOptions.append(*opt);
            }
            emit configOptionsReceived(configOptions);
        } else {
            qCDebug(logController) << "Session response has no configOptions";
        }

        qCDebug(logController) << "Session created:" << m_sessionId;
    });
}

static bool supportsEmbeddedPromptResources(std::optional<Acp::AgentCapabilities> agentCapabilities)
{
    if (!agentCapabilities)
        return false;
    if (auto promptCapabilities = agentCapabilities->promptCapabilities())
        return promptCapabilities->embeddedContext().value_or(false);
    return false;
}

// OPENMV-DIFF //
void AcpChatController::forgetSentEditorState()
{
    m_sentEditorTexts.clear();
    m_sentCurrentScript.clear();
}

static bool supportsPromptImages(std::optional<Acp::AgentCapabilities> agentCapabilities)
{
    if (!agentCapabilities)
        return false;
    if (auto promptCapabilities = agentCapabilities->promptCapabilities())
        return promptCapabilities->image().value_or(false);
    return false;
}
// OPENMV-DIFF //

// OPENMV-DIFF //
// void AcpChatController::sendPrompt(const QString &text,
//                                    const QList<Utils::FilePath> &additionalFiles,
//                                    bool includeCurrentEditor)
// {
void AcpChatController::sendPrompt(const QString &text,
                                   const QList<Utils::FilePath> &additionalFiles,
                                   bool includeCurrentEditor,
                                   const QList<PromptImage> &images)
{
    sendPrompt(QList<OutgoingMessage>{{text, additionalFiles, images}}, includeCurrentEditor);
}

void AcpChatController::sendPrompt(const QList<OutgoingMessage> &messages,
                                   bool includeCurrentEditor)
{
    const bool isEmpty = Utils::allOf(messages, [](const OutgoingMessage &message) {
        return message.text.isEmpty() && message.files.isEmpty() && message.images.isEmpty();
    });
    // OPENMV-DIFF //
    using namespace TextEditor;
    // OPENMV-DIFF //
    // if (text.isEmpty() || !m_client || m_sessionId.isEmpty())
    // A message may be only attachments (a screenshot, say).
    if (isEmpty || !m_client || m_sessionId.isEmpty())
    // OPENMV-DIFF //
        return;

    PromptRequest request;
    request.sessionId(m_sessionId);

    // OPENMV-DIFF //
    // TextContent textContent;
    // textContent.text(text);
    // QList<ContentBlock> content = {textContent};
    // OPENMV-DIFF //
    // OPENMV-DIFF //
    // TextEditorWidget *currentEditorWidget = TextEditorWidget::currentTextEditorWidget();
    // if (includeCurrentEditor && currentEditorWidget) {
    //     const FilePath filePath = currentEditorWidget->textDocument()->filePath();
    //     if (!filePath.isEmpty()) {
    //         const QString uri = filePath.toUrl().toString();
    //         content << ResourceLink()
    //                        .name(filePath.fileName())
    //                        .description("Qt Creators current main Text Editor file.")
    //                        .uri(uri);
    //
    //         if (supportsEmbeddedPromptResources(m_agentCapabilities)) {
    //             TextResourceContents editorState;
    //             editorState.uri(uri);
    //             QString stateString
    //                 = "This is the state of the current Text Editor in Qt Creator\n";
    //             QTextCursor tc = currentEditorWidget->textCursor();
    //             const QString cursorString
    //                 = "Cursor %1: %2, Line(0-based): %3, Column(0-based): %4\n";
    //             stateString += cursorString.arg("Position")
    //                                .arg(tc.position())
    //                                .arg(tc.blockNumber())
    //                                .arg(tc.positionInBlock());
    //             tc.setPosition(tc.anchor());
    //             stateString += cursorString.arg("Anchor")
    //                                .arg(tc.position())
    //                                .arg(tc.blockNumber())
    //                                .arg(tc.positionInBlock());
    //             stateString += "First Visible Line: "
    //                            + QString::number(currentEditorWidget->firstVisibleBlockNumber())
    //                            + "\n";
    //             stateString += "Last Visible Line: "
    //                            + QString::number(currentEditorWidget->lastVisibleBlockNumber())
    //                            + "\n";
    //             content << EmbeddedResource().resource(
    //                 TextResourceContents().text(stateString).uri(uri));
    //         }
    //     }
    // }
    // OPENMV-DIFF //

    QList<ContentBlock> content;
    for (const OutgoingMessage &message : messages) {
        // Agents run consecutive text blocks together ("2", "3" read as "23"): end
        // each queued message but the last with a blank line.
        if (!message.text.isEmpty()) {
            const bool last = &message == &messages.constLast();
            content << TextContent().text(last ? message.text : message.text + "\n\n");
        }

        for (const Utils::FilePath &file : message.files) {
            const QString uri = file.toUrl().toString();
            // OPENMV-DIFF //
            // content << ResourceLink()
            //                .name(file.fileName())
            //                .description("Manually added context file.")
            //                .uri(uri);
            // if (supportsEmbeddedPromptResources(m_agentCapabilities)) {
            // A folder is sent as a reference only (the agent can look inside); so is a
            // big file, rather than its whole contents.
            content << ResourceLink()
                           .name(file.fileName())
                           .description(file.isDir() ? "Folder attached by the user."
                                                     : "Manually added context file.")
                           .uri(uri);

            if (file.isFile() && file.fileSize() <= 512 * 1024
                && supportsEmbeddedPromptResources(m_agentCapabilities)) {
            // OPENMV-DIFF //
                const auto fileContents = file.fileContents();
                if (fileContents) {
                    content << EmbeddedResource().resource(
                        TextResourceContents()
                            .text(QString::fromUtf8(*fileContents))
                            .uri(uri));
                }
            }
        }

        // OPENMV-DIFF //
        // Images go as image content when the agent takes it. Otherwise they go as a
        // link to the image file (a pasted image is saved to a temporary file first),
        // which agents that read files can still open.
        for (const PromptImage &image : message.images) {
            if (supportsPromptImages(m_agentCapabilities)) {
                content << ImageContent()
                               .data(QString::fromLatin1(image.data.toBase64()))
                               .mimeType(image.mimeType);
                continue;
            }
            FilePath file = image.source;
            if (file.isEmpty()) {
                const QString suffix = image.mimeType == "image/jpeg" ? "jpg" : "png";
                file = TemporaryDirectory::masterDirectoryFilePath()
                       / QString("chat-image-%1.%2")
                             .arg(QUuid::createUuid().toString(QUuid::Id128).left(12), suffix);
                if (!file.writeFileContents(image.data))
                    continue;
            }
            content << ResourceLink()
                           .name(file.fileName())
                           .description("Image attached by the user.")
                           .uri(file.toUrl().toString());
        }
        // OPENMV-DIFF //
    }
    // OPENMV-DIFF //

    // The open scripts, sent silently. The AI and Edit modes are separate, so the
    // user doesn't point at a script while chatting; instead the agent gets:
    // - the script last active in the editor, as a reference, when that changes;
    // - each open script whose editor text differs from its file (unsaved edits,
    //   an edited untitled script), with that text: each version once per chat,
    //   so the history only grows when the user changed something. Saved scripts
    //   the agent reads from disk itself.
    // A slash command runs only as the whole message ("/compact"), so for one the
    // open scripts wait for the next message.
    const bool isCommand = messages.size() == 1 && messages.first().text.startsWith('/')
                           && messages.first().files.isEmpty() && messages.first().images.isEmpty();
    if (includeCurrentEditor && !isCommand) {
        Core::IDocument *current = Core::EditorManager::currentDocument();
        if (current && !current->filePath().isEmpty()
            && current->filePath() != m_sentCurrentScript) {
            m_sentCurrentScript = current->filePath();
            // Marked text rather than a resource link: agents replay a link as an
            // ordinary "[@name](uri)" line, which would look like a user attachment.
            content << TextContent().text(
                ideContextMarker()
                + QString(" The script the user has open in the IDE editor is \"%1\" (%2).")
                      .arg(current->displayName(), current->filePath().toUserOutput()));
        }
    }
    for (Core::IDocument *document : isCommand ? QList<Core::IDocument *>()
                                               : Core::DocumentModel::openedDocuments()) {
        auto *textDocument = qobject_cast<TextDocument *>(document);
        if (!textDocument || textDocument->filePath().isEmpty() || !textDocument->isModified())
            continue;
        const QString editorText = textDocument->plainText();
        if (editorText.size() > 100 * 1024)
            continue;
        const FilePath path = textDocument->filePath();
        const size_t hash = qHash(editorText);
        if (m_sentEditorTexts.contains(path) && m_sentEditorTexts.value(path) == hash)
            continue;
        m_sentEditorTexts.insert(path, hash);

        const QString note
            = ideContextMarker()
              + QString(" \"%1\" has unsaved changes in the user's IDE editor, so the file "
                        "%2 on disk is older. Its current text in the editor follows.")
                    .arg(textDocument->displayName(), path.toUserOutput());
        // One marked text block (not an embedded resource, which agents replay as
        // an attachment), so a reopened chat can leave it out.
        content << TextContent().text(note + "\n\n```\n" + editorText + "\n```");
    }
    // OPENMV-DIFF //

    request.prompt(content);

    m_client->prompt(request, [this](const QJsonObject &result, const std::optional<Error> &error) {
        if (error) {
            emit errorOccurred(Tr::tr("Prompt error: %1").arg(error->message()));
        } else {
            auto response = fromJson<PromptResponse>(QJsonValue(result));
            if (response)
                qCDebug(logController) << "Prompt completed. Stop reason:" << toString(response->stopReason());
        }
        emit promptFinished();
    });
}

void AcpChatController::cancelPrompt()
{
    if (!m_client || m_sessionId.isEmpty())
        return;

    CancelNotification cancel;
    cancel.sessionId(m_sessionId);
    m_client->cancelSession(cancel);
}

void AcpChatController::authenticate(const QString &methodId)
{
    if (!m_client || !m_initialized)
        return;

    AuthenticateRequest req;
    req.methodId(methodId);
    m_client->authenticate(req, [this](const QJsonObject &result, const std::optional<Error> &error) {
        Q_UNUSED(result)
        if (error) {
            QString errorMsg = error->message();
            if (const std::optional<QJsonValue> data = error->data(); data->isString()) {
                if (auto dataString = data->toString(); !dataString.isEmpty())
                    errorMsg.append("\n" + dataString);
            }
            emit authenticationFailed(errorMsg);
            return;
        }
        createNewSession();
    });
}

void AcpChatController::setConfigOption(const QString &configId, const QString &value)
{
    if (m_sessionId.isEmpty() || !m_client)
        return;

    SetSessionConfigOptionRequest req;
    req.sessionId(m_sessionId);
    req.configId(configId);
    req.value(value);
    m_client->setSessionConfigOption(req, [this](const QJsonObject &result, const std::optional<Error> &error) {
        if (error) {
            emit errorOccurred(Tr::tr("Failed to set config option: %1").arg(error->message()));
            return;
        }
        auto resp = fromJson<SetSessionConfigOptionResponse>(QJsonValue(result));
        if (resp)
            emit configOptionsReceived(resp->configOptions());
    });
}

void AcpChatController::sendPermissionResponse(const QJsonValue &id, const QString &optionId)
{
    if (m_permissionHandler)
        m_permissionHandler->sendPermissionResponse(id, optionId);
}

void AcpChatController::sendPermissionCancelled(const QJsonValue &id)
{
    if (m_permissionHandler)
        m_permissionHandler->sendPermissionCancelled(id);
}

void AcpChatController::onInitializeResult(const InitializeResponse &response)
{
    if (const auto info = response.agentInfo()) {
        m_agentName = info->title().value_or(info->name());
        m_agentVersion = info->version();
    }

    m_initialized = true;

    m_authMethods = response.authMethods().value_or(QList<AuthMethod>{});
    m_agentCapabilities = response.agentCapabilities();

    emit agentInfoReceived(m_agentName, m_agentVersion, m_iconUrl);

    emit sessionSelectionRequired();
}

static QList<McpServer> buildMcpServers()
{
    auto generateHeaders = [](const QStringList &headers) {
        return Utils::transform(headers, [](const QString &h) -> HttpHeader {
            HttpHeader header;
            int split = h.indexOf(':');
            header.name(h.left(split).trimmed());
            header.value(h.mid(split + 1).trimmed());
            return header;
        });
    };

    QList<McpServer> mcpServers;
    for (const Core::McpManager::ServerInfo &info : Core::McpManager::mcpServers()) {
        switch (info.connectionType) {
        case Core::McpManager::Stdio: {
            QTC_ASSERT(std::holds_alternative<CommandLine>(info.launchInfo), continue);
            const CommandLine commandLine = std::get<CommandLine>(info.launchInfo);
            const QString command = commandLine.executable().toUserOutput();
            const QStringList args = ProcessArgs::splitArgs(
                commandLine.arguments(), HostOsInfo::hostOs());
            auto stdioServer = McpServerStdio().name(info.name).command(command).args(args);
            if (info.envChanges.hasItems()) {
                QList<EnvVariable> envVars;
                for (const EnvironmentItem &item : info.envChanges.itemsFromUser()) {
                    if (item.operation == EnvironmentItem::SetEnabled)
                        envVars.append(EnvVariable().name(item.name).value(item.value));
                }
                stdioServer.env(envVars);
            }
            mcpServers.append(stdioServer);
            break;
        }
        case Core::McpManager::Sse: {
            QTC_ASSERT(std::holds_alternative<QUrl>(info.launchInfo), continue);
            const QString url = std::get<QUrl>(info.launchInfo).toString();
            const QList<HttpHeader> headers = generateHeaders(info.httpHeaders);
            mcpServers.append(McpServerSse().name(info.name).url(url).headers(headers));
            break;
        }
        case Core::McpManager::Streamable_Http: {
            QTC_ASSERT(std::holds_alternative<QUrl>(info.launchInfo), continue);
            const QString url = std::get<QUrl>(info.launchInfo).toString();
            const QList<HttpHeader> headers = generateHeaders(info.httpHeaders);
            mcpServers.append(McpServerHttp().name(info.name).url(url).headers(headers));
            break;
        }
        }
    }
    return mcpServers;
}

NewSessionRequest AcpChatController::buildNewSessionRequest() const
{
    NewSessionRequest req;
    req.cwd(m_workingDirectory.toFSPathString());
    req.mcpServers(buildMcpServers());
    return req;
}

// OPENMV-DIFF //
bool AcpChatController::supportsLoadSession() const
{
    return m_agentCapabilities && m_agentCapabilities->loadSession().value_or(false);
}
// OPENMV-DIFF //

bool AcpChatController::supportsSessionList() const
{
    if (!m_agentCapabilities)
        return false;
    const auto &sessionCaps = m_agentCapabilities->sessionCapabilities();
    return sessionCaps.has_value() && sessionCaps->list().has_value();
}

void AcpChatController::listSessions(const std::optional<QString> &cursor)
{
    if (!m_client || !m_initialized)
        return;

    ListSessionsRequest req;
    req.cursor(cursor);

    m_client->listSessions(req, [this](const QJsonObject &result, const std::optional<Error> &error) {
        if (error) {
            emit errorOccurred(Tr::tr("Failed to list sessions: %1").arg(error->message()));
            return;
        }
        auto resp = fromJson<ListSessionsResponse>(QJsonValue(result));
        if (!resp) {
            emit errorOccurred(Tr::tr("Failed to list sessions: invalid response."));
            return;
        }
        emit sessionsListed(resp->sessions(), resp->nextCursor());
    });
}

void AcpChatController::loadSession(const QString &sessionId, const FilePath &workingDirectory)
{
    if (!m_client || !m_initialized)
        return;

    m_workingDirectory = withDefaultWorkingDirectory(workingDirectory); // OPENMV-DIFF

    LoadSessionRequest req;
    req.sessionId(sessionId);
    req.cwd(m_workingDirectory.toFSPathString());
    req.mcpServers(buildMcpServers());

    m_client->loadSession(req, [this, sessionId](const QJsonObject &result,
                                                  const std::optional<Error> &error) {
        if (error) {
            emit errorOccurred(Tr::tr("Failed to load session: %1").arg(error->message()));
            return;
        }
        m_sessionId = sessionId;
        forgetSentEditorState(); // OPENMV-DIFF
        emit sessionLoaded(m_sessionId);

        auto resp = fromJson<LoadSessionResponse>(QJsonValue(result));
        if (resp) {
            if (const auto &opts = resp->configOptions(); opts.has_value() && !opts->isEmpty()) {
                QList<SessionConfigOption> configOptions;
                for (const QJsonValue &item : *opts) {
                    auto opt = fromJson<SessionConfigOption>(item);
                    if (opt)
                        configOptions.append(*opt);
                }
                emit configOptionsReceived(configOptions);
            }
        }
    });
}

} // namespace AcpClient::Internal
