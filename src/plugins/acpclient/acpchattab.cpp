// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acpchattab.h"
#include "acpchatcontroller.h"
#include "acpclienttr.h"
#include "acppermissionhandler.h"
#include "acpsettings.h"
#include "chatinputedit.h"
#include "chatpanel.h"
#include "sessionpickerwidget.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectmanager.h>

#include <texteditor/texteditor.h>

#include <utils/async.h>
#include <utils/utilsicons.h>
#include <utils/fileutils.h>
#include <utils/infolabel.h>
#include <utils/progressindicator.h>
#include <utils/qtcwidgets.h>

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout> // OPENMV-DIFF
#include <QRegularExpression> // OPENMV-DIFF
#include <QScrollArea> // OPENMV-DIFF
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer> // OPENMV-DIFF
#include <QVBoxLayout>

using namespace Acp;
using namespace Core;
using namespace Utils;
using namespace ProjectExplorer;

namespace AcpClient::Internal {

AcpChatTab::AcpChatTab(QWidget *parent)
    : QWidget(parent)
    , m_title(Tr::tr("New Chat"))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget;

    // --- Page 0: Configuration (shown when disconnected) ---
    {
        auto *configPage = new QWidget;
        auto *configOuter = new QVBoxLayout(configPage);
        // OPENMV-DIFF //
        // configOuter->addStretch();
        //
        // Anchored at the top (not centred) so opening a group grows the page
        // downwards instead of shifting the rows above it.
        configOuter->addSpacing(24);
        // OPENMV-DIFF //

        m_configStack = new QStackedWidget;
        // OPENMV-DIFF //
        // m_configStack->setMaximumWidth(480);
        // A fixed width (room for the three-column agent grid) so opening or
        // closing a group never changes the page's size.
        m_configStack->setFixedWidth(640);
        // OPENMV-DIFF //
        m_configStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        // Config stack page 0: no servers configured
        {
            auto *noServerPage = new QWidget;
            auto *noServerLayout = new QVBoxLayout(noServerPage);
            noServerLayout->setSpacing(12);
            noServerLayout->addStretch();

            // OPENMV-DIFF //
            // m_noServerLabel = new InfoLabel(
            //     Tr::tr("No ACP servers configured. Add a server in the settings to get started."),
            //     InfoLabel::Information);
            // OPENMV-DIFF //
            // Only shown when the agent registry couldn't be fetched and no agent has
            // been used yet (the agent settings page is hidden).
            m_noServerLabel = new InfoLabel(
                Tr::tr("Couldn't load the list of AI agents. Check your internet connection, "
                       "then try again."),
                InfoLabel::Information);
            // OPENMV-DIFF //
            m_noServerLabel->setElideMode(Qt::ElideNone);
            m_noServerLabel->setWordWrap(true);
            noServerLayout->addWidget(m_noServerLabel);

            // OPENMV-DIFF //
            // auto *manageButton = new QtcButton(Tr::tr("Manage Agents..."),
            //                                    QtcButton::MediumSecondary);
            // manageButton->setToolTip(Tr::tr("Open ACP server settings."));
            // manageButton->setPixmap(Utils::Icons::SETTINGS.pixmap());
            // connect(manageButton, &QAbstractButton::clicked, this, [] {
            //     Core::ICore::showSettings("AI.ACPSERVERS");
            // });
            // OPENMV-DIFF //
            // Fetch the registry again; on success the settings emit serversChanged
            // and the picker repopulates.
            auto *manageButton = new QtcButton(Tr::tr("Try Again"), QtcButton::MediumSecondary);
            manageButton->setToolTip(Tr::tr("Load the list of AI agents again."));
            connect(manageButton, &QAbstractButton::clicked, this, [] { prefetchAcpRegistry(); });
            // OPENMV-DIFF //
            auto *manageRow = new QHBoxLayout;
            manageRow->addStretch();
            manageRow->addWidget(manageButton);
            manageRow->addStretch();
            noServerLayout->addLayout(manageRow);
            noServerLayout->addStretch();

            m_configStack->addWidget(noServerPage); // index 0
        }

        // Config stack page 1: list of agent buttons
        {
            auto *connectPage = new QWidget;
            auto *connectLayout = new QVBoxLayout(connectPage);
            connectLayout->setSpacing(12);
            connectLayout->addStretch();

            auto *titleLabel = new QLabel(Tr::tr("Choose AI Agent"));
            QFont titleFont = titleLabel->font();
            titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
            titleFont.setBold(true);
            titleLabel->setFont(titleFont);
            titleLabel->setAlignment(Qt::AlignHCenter);
            connectLayout->addWidget(titleLabel);
            connectLayout->addWidget(Layouting::createHr());

            // OPENMV-DIFF //
            // m_serverButtonsLayout = new QVBoxLayout;
            // m_serverButtonsLayout->setSpacing(6);
            // connectLayout->addLayout(m_serverButtonsLayout);
            //
            // A grid of every registry agent, most used first, plus any custom
            // servers; picking one that is not configured yet adds it as a
            // server (saved like one made in settings) and connects.
            // A few popular agents always shown, the rest of the registry in
            // collapsible groups; picking one that is not configured yet adds
            // it as a server (saved like one made in settings) and connects.
            m_serverGroupsLayout = new QVBoxLayout;
            m_serverGroupsLayout->setSpacing(6);
            connectLayout->addLayout(m_serverGroupsLayout);
            // OPENMV-DIFF //

            m_connectionErrorLabel = new QLabel;
            m_connectionErrorLabel->setWordWrap(true);
            m_connectionErrorLabel->setTextFormat(Qt::RichText);
            m_connectionErrorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            m_connectionErrorLabel->hide();
            QPalette errorPal = m_connectionErrorLabel->palette();
            errorPal.setColor(QPalette::WindowText, creatorColor(Theme::TextColorError));
            m_connectionErrorLabel->setPalette(errorPal);
            connectLayout->addWidget(m_connectionErrorLabel);

            // OPENMV-DIFF //
            // No "Manage Agents..." here: the grid picks the agent, and the
            // server list stays reachable from Preferences > AI.
            //
            // auto *manageButton = new QtcButton(Tr::tr("Manage Agents..."),
            //                                    QtcButton::MediumGhost);
            // manageButton->setToolTip(Tr::tr("Open ACP server settings."));
            // manageButton->setPixmap(Utils::Icons::SETTINGS.pixmap());
            // connect(manageButton, &QAbstractButton::clicked, this, [] {
            //     Core::ICore::showSettings("AI.ACPSERVERS");
            // });
            // connectLayout->addWidget(Layouting::createHr());
            // connectLayout->addWidget(manageButton);
            // OPENMV-DIFF //

            connectLayout->addStretch();

            m_configStack->addWidget(connectPage); // index 1
        }

        auto *configCenter = new QHBoxLayout;
        configCenter->addStretch();
        configCenter->addWidget(m_configStack);
        configCenter->addStretch();
        configOuter->addLayout(configCenter);
        configOuter->addStretch();

        // OPENMV-DIFF //
        // m_stack->addWidget(configPage);  // index 0
        //
        // The agent groups can be taller than the chat area, so the page
        // scrolls instead of squeezing its buttons.
        auto *configScroll = new QScrollArea;
        configScroll->setWidgetResizable(true);
        configScroll->setFrameShape(QFrame::NoFrame);
        configScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        configScroll->setWidget(configPage);
        m_stack->addWidget(configScroll);  // index 0
        // OPENMV-DIFF //
    }

    // --- Page 1: Authentication (shown when auth required) ---
    {
        auto *authPage = new QWidget;
        auto *authOuter = new QVBoxLayout(authPage);
        authOuter->addStretch();

        auto *authForm = new QWidget;
        authForm->setMaximumWidth(480);
        authForm->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *authLayout = new QVBoxLayout(authForm);
        authLayout->setSpacing(12);

        auto *authTitle = new QLabel(Tr::tr("Authentication Required"));
        QFont authFont = authTitle->font();
        authFont.setPointSizeF(authFont.pointSizeF() * 1.3);
        authFont.setBold(true);
        authTitle->setFont(authFont);
        authLayout->addWidget(authTitle);

        auto *authFormLayout = new QFormLayout;
        authFormLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        m_authMethodCombo = new QComboBox;
        m_authMethodCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        authFormLayout->addRow(Tr::tr("Method:"), m_authMethodCombo);
        authLayout->addLayout(authFormLayout);

        m_authDescriptionLabel = new QLabel;
        m_authDescriptionLabel->setWordWrap(true);
        QPalette descPal = m_authDescriptionLabel->palette();
        descPal.setColor(QPalette::WindowText, descPal.color(QPalette::PlaceholderText));
        m_authDescriptionLabel->setPalette(descPal);
        m_authDescriptionLabel->hide();
        authLayout->addWidget(m_authDescriptionLabel);

        m_authErrorLabel = new QLabel;
        m_authErrorLabel->setWordWrap(true);
        QPalette errorPal = m_authErrorLabel->palette();
        errorPal.setColor(QPalette::WindowText, Qt::red);
        m_authErrorLabel->setPalette(errorPal);
        m_authErrorLabel->hide();
        authLayout->addWidget(m_authErrorLabel);

        auto *authButtonRow = new QHBoxLayout;
        auto *authCancelButton = new QPushButton(Tr::tr("Cancel"));
        auto *authButton = new QPushButton(Tr::tr("Authenticate"));
        authButton->setDefault(true);
        authButtonRow->addWidget(authCancelButton);
        authButtonRow->addStretch();
        authButtonRow->addWidget(authButton);
        authLayout->addLayout(authButtonRow);

        auto *authCenter = new QHBoxLayout;
        authCenter->addStretch();
        authCenter->addWidget(authForm);
        authCenter->addStretch();
        authOuter->addLayout(authCenter);
        authOuter->addStretch();

        connect(authButton, &QPushButton::clicked, this, [this] {
            m_authErrorLabel->hide();
            const QString methodId = m_authMethodCombo->currentData().toString();
            if (!methodId.isEmpty())
                m_controller->authenticate(methodId);
        });
        connect(authCancelButton, &QPushButton::clicked, this, [this] {
            m_controller->disconnectFromServer();
        });

        m_stack->addWidget(authPage);  // index 1
    }

    // --- Page 2: Chat panel (shown when connected) ---
    m_chatPanel = new ChatPanel;
    m_stack->addWidget(m_chatPanel); // index 2

    // --- Page 3: Initializing (shown after agent button click, before session/chat) ---
    {
        auto *initPage = new QWidget;
        auto *initOuter = new QVBoxLayout(initPage);
        initOuter->addStretch();

        auto *initForm = new QWidget;
        initForm->setMaximumWidth(480);
        initForm->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        auto *initLayout = new QVBoxLayout(initForm);
        initLayout->setSpacing(12);

        auto *spinner = new Utils::ProgressIndicator(Utils::ProgressIndicatorSize::Large);
        auto *spinnerRow = new QHBoxLayout;
        spinnerRow->addStretch();
        spinnerRow->addWidget(spinner);
        spinnerRow->addStretch();
        initLayout->addLayout(spinnerRow);

        m_initializingLabel = new QLabel;
        m_initializingLabel->setAlignment(Qt::AlignHCenter);
        initLayout->addWidget(m_initializingLabel);

        auto *cancelButton = new QtcButton(Tr::tr("Cancel"), QtcButton::MediumSecondary);
        auto *cancelRow = new QHBoxLayout;
        cancelRow->addStretch();
        cancelRow->addWidget(cancelButton);
        cancelRow->addStretch();
        initLayout->addLayout(cancelRow);
        connect(cancelButton, &QAbstractButton::clicked, this, [this] {
            // OPENMV-DIFF //
            // Also gives up on a session that was loading (nothing left to report).
            m_loadingSession = false;
            m_loadingRestoredSession = false;
            // OPENMV-DIFF //
            m_controller->disconnectFromServer();
            m_stack->setCurrentIndex(0);
        });
        m_initializingCancel = cancelButton; // OPENMV-DIFF: hidden while a chat loads

        auto *initCenter = new QHBoxLayout;
        initCenter->addStretch();
        initCenter->addWidget(initForm);
        initCenter->addStretch();
        initOuter->addLayout(initCenter);
        initOuter->addStretch();

        m_stack->addWidget(initPage); // index 3
    }

    layout->addWidget(m_stack);

    // Controller
    m_controller = new AcpChatController(this);

    populateServerButtons();

    // --- Connections: Settings ---
    connect(&AcpSettings::instance(), &AcpSettings::serversChanged,
            this, &AcpChatTab::populateServerButtons);

    // --- Connections: ChatPanel -> Controller ---
    // OPENMV-DIFF //
    const auto previews = [](const QList<PromptImage> &images) {
        QList<QImage> result;
        for (const PromptImage &image : images)
            result.append(image.preview);
        return result;
    };
    // OPENMV-DIFF //
    // The files to show on the user's message: images show as thumbnails instead.
    const auto attachedFiles = [](const QList<FilePath> &files, const QList<PromptImage> &) {
        return files;
    };
    connect(m_chatPanel, &ChatPanel::sendRequested, this,
            [this, previews, attachedFiles](const QString &text) {
        // OPENMV-DIFF //
        // Attachments go with this message only.
        const QList<FilePath> files = m_chatPanel->takeContextFiles();
        const QList<PromptImage> images = m_chatPanel->takeImages();
        // OPENMV-DIFF //
        if (m_activePicker) {
            QObject::disconnect(m_controller, &AcpChatController::sessionsListed,
                                m_activePicker, nullptr);
            const std::optional<SessionPickerWidget::NewSessionTarget> firstSessionTarget
                = m_activePicker->firstNewSessionTarget();
            m_activePicker->deleteLater();
            m_pendingPrompt = text;
            // OPENMV-DIFF //
            m_pendingFiles = files;
            m_pendingImages = images;
            if (m_firstPrompt.isEmpty()) {
                m_firstPrompt = text;
                updateTitle();
            }
            // OPENMV-DIFF //
            const Project *project = ProjectManager::startupProject();
            const FilePath sessionPath = firstSessionTarget ? firstSessionTarget->cwd
                                         : project          ? project->projectDirectory()
                                                            : FilePath();
            m_controller->createNewSession(sessionPath);
            return;
        }
        // OPENMV-DIFF //
        if (m_firstPrompt.isEmpty()) {
            m_firstPrompt = text;
            updateTitle();
        }
        // OPENMV-DIFF //
        // OPENMV-DIFF //
        // m_chatPanel->addUserMessage(text);
        // m_chatPanel->setPrompting(true);
        // m_controller->sendPrompt(
        //     text, m_chatPanel->manualContextFiles(), m_chatPanel->includeCurrentEditorContext());
        // Queued messages go as one prompt but show as the messages they were.
        const QList<OutgoingMessage> parts = m_chatPanel->takeSentParts();
        if (parts.isEmpty()) {
            m_chatPanel->addUserMessage(text, previews(images), attachedFiles(files, images));
        } else {
            for (const OutgoingMessage &part : parts)
                m_chatPanel->addUserMessage(part.text, previews(part.images), part.files);
        }
        m_chatPanel->setPrompting(true);
        m_controller->sendPrompt(text, files, m_chatPanel->includeCurrentEditorContext(), images);
        // OPENMV-DIFF //
    });
    // OPENMV-DIFF //
    // connect(m_chatPanel, &ChatPanel::cancelRequested, m_controller, &AcpChatController::cancelPrompt);
    connect(m_chatPanel, &ChatPanel::cancelRequested, this, [this] {
        m_controller->cancelPrompt();
        m_chatPanel->addStatusMessage(Tr::tr("Request interrupted by user"));
    });
    // OPENMV-DIFF //
    connect(m_chatPanel, &ChatPanel::configOptionChanged,
            m_controller, &AcpChatController::setConfigOption);

    // --- Connections: Controller -> UI ---
    connect(m_controller, &AcpChatController::connectionStateChanged, this, [this](AcpClientObject::State state) {
        const bool disconnected = state == AcpClientObject::State::Disconnected;
        if (disconnected) {
            m_stack->setCurrentIndex(0);
            m_chatPanel->clear();
            m_chatPanel->setSendEnabled(false);
            m_chatPanel->setPrompting(false);
            m_chatPanel->clearConfigOptions();
            m_currentServerName.clear();
            // OPENMV-DIFF //
            m_agentTitle.clear();
            m_firstPrompt.clear();
            m_icon = {};
            m_customTitle.clear();
            m_currentServerId.clear();
            // OPENMV-DIFF //
            updateTitle();
        }
    });
    connect(m_controller, &AcpChatController::agentInfoReceived, this,
            [this](const QString &name, const QString &version, const QString &iconUrl) {
        m_chatPanel->setAgentInfo(name, version, iconUrl);
        updateTitle();
    });
    connect(m_controller, &AcpChatController::authenticationRequired,
            this, [this](const QList<Acp::AuthMethod> &methods) {
        // Show auth inline in chat instead of a separate page
        m_stack->setCurrentIndex(2);
        m_chatPanel->addAuthenticationRequest(methods);
    });
    connect(m_chatPanel, &ChatPanel::authenticateRequested,
            m_controller, &AcpChatController::authenticate);
    connect(m_controller, &AcpChatController::authenticationFailed, this, [this](const QString &error) {
        m_chatPanel->showAuthenticationError(
            Tr::tr("Authentication failed: %1").arg(error));
    });
    // OPENMV-DIFF //
    // A rename elsewhere (the session picker, or another tab on the same session).
    connect(&AcpSettings::instance(), &AcpSettings::chatTitleChanged,
            this, [this](const QString &sessionId, const QString &title) {
        const QString mine = m_restorePending ? m_restoreSessionId : m_controller->sessionId();
        if (sessionId != mine || title == m_customTitle)
            return;
        m_customTitle = title;
        updateTitle();
    });
    // OPENMV-DIFF //
    connect(m_controller, &AcpChatController::sessionCreated, this,
            [this, previews, attachedFiles]() {
        rememberCustomTitle(); // OPENMV-DIFF: a name given before the session existed
        m_chatPanel->resolveAuthentication();
        m_stack->setCurrentIndex(2);
        m_chatPanel->setSendEnabled(true);
        // OPENMV-DIFF //
        // if (m_pendingPrompt.isEmpty()) {
        if (m_pendingPrompt.isEmpty() && m_pendingFiles.isEmpty() && m_pendingImages.isEmpty()) {
        // OPENMV-DIFF //
            m_chatPanel->appendAgentText(
                "Cute Greetings,\n\n your AI Agent is ready and you can start chatting.");
            m_chatPanel->finishAgentMessage();
        } else  {
            const QString text = m_pendingPrompt;
            m_pendingPrompt.clear();
            // OPENMV-DIFF //
            // m_chatPanel->addUserMessage(text);
            // m_chatPanel->setPrompting(true);
            // m_controller->sendPrompt(
            //     text, m_chatPanel->manualContextFiles(), m_chatPanel->includeCurrentEditorContext());
            const QList<FilePath> files = std::exchange(m_pendingFiles, {});
            const QList<PromptImage> images = std::exchange(m_pendingImages, {});
            m_chatPanel->addUserMessage(text, previews(images), attachedFiles(files, images));
            m_chatPanel->setPrompting(true);
            m_controller->sendPrompt(text, files, m_chatPanel->includeCurrentEditorContext(), images);
            // OPENMV-DIFF //
        }
    });
    connect(m_controller, &AcpChatController::configOptionsReceived,
            m_chatPanel, &ChatPanel::updateConfigOptions);
    // OPENMV-DIFF //
    connect(m_controller, &AcpChatController::usageUpdated, this,
            [this](const QString &sessionId, qint64 used, qint64 size, double cost,
                   const QString &currency) {
        if (sessionId == m_controller->sessionId())
            m_chatPanel->setUsage(used, size, cost, currency);
    });
    // OPENMV-DIFF //
    connect(m_controller, &AcpChatController::sessionUpdate,
            this, [this](const QString &sessionId, const SessionUpdate &update) {
        Q_UNUSED(sessionId)
        const QString &kind = update.kind();
        if (kind == QLatin1String("agent_message_chunk")) {
            if (const auto *chunk = update.get<ContentChunk>()) {
                if (const auto *textBlock = std::get_if<TextContent>(&chunk->content()))
                    m_chatPanel->appendAgentText(textBlock->text());
            }
        } else if (kind == QLatin1String("agent_thought_chunk")) {
            if (const auto *chunk = update.get<ContentChunk>()) {
                if (const auto *textBlock = std::get_if<TextContent>(&chunk->content()))
                    m_chatPanel->appendAgentThought(textBlock->text());
            }
        } else if (kind == QLatin1String("user_message_chunk")) {
            if (const auto *chunk = update.get<ContentChunk>()) {
                // OPENMV-DIFF //
                // if (const auto *textBlock = std::get_if<TextContent>(&chunk->content())) {
                // Leave out what the IDE added to the user's messages (open scripts).
                // Agents replay attachments as text too: a file as "[@name](uri)"
                // (shown as a file chip) and its embedded contents as a
                // "<context ref=...>" block (left out); a stopped request leaves
                // a "[Request interrupted by user]" note (shown as a status line).
                static const QRegularExpression linkRe(
                    QStringLiteral("^\\[@([^\\]]*)\\]\\((\\S+)\\)$"));
                const auto *textBlock = std::get_if<TextContent>(&chunk->content());
                const QString chunkText = textBlock ? textBlock->text().trimmed() : QString();
                const QRegularExpressionMatch link = linkRe.match(chunkText);
                if (textBlock && textBlock->text().startsWith(AcpChatController::ideContextMarker())) {
                } else if (textBlock && chunkText.startsWith(QLatin1String("<context ref="))) {
                } else if (textBlock && chunkText.startsWith(QLatin1String("[Request interrupted by user"))
                           && chunkText.endsWith(QLatin1Char(']'))) {
                    m_chatPanel->addStatusMessage(chunkText.mid(1, chunkText.size() - 2));
                } else if (textBlock && link.hasMatch()) {
                    const QUrl url(link.captured(2));
                    m_chatPanel->addUserFile(url.isLocalFile()
                                                 ? FilePath::fromUserInput(url.toLocalFile())
                                                 : FilePath::fromUserInput(link.captured(1)));
                } else if (const auto *resourceLink = std::get_if<ResourceLink>(&chunk->content())) {
                    const QUrl url(resourceLink->uri());
                    m_chatPanel->addUserFile(url.isLocalFile()
                                                 ? FilePath::fromUserInput(url.toLocalFile())
                                                 : FilePath::fromUserInput(resourceLink->name()));
                } else if (std::get_if<EmbeddedResource>(&chunk->content())) {
                } else if (textBlock) {
                // OPENMV-DIFF //
                    m_chatPanel->addUserMessage(textBlock->text());
                    // OPENMV-DIFF //
                    // A resumed session replays its history: name it after the first
                    // message, as for a new one.
                    if (m_firstPrompt.isEmpty()) {
                        m_firstPrompt = textBlock->text();
                        updateTitle();
                    }
                    // OPENMV-DIFF //
                }
                // OPENMV-DIFF //
                // An image the user attached, replayed.
                else if (const auto *imageBlock = std::get_if<ImageContent>(&chunk->content())) {
                    QImage image;
                    if (image.loadFromData(QByteArray::fromBase64(imageBlock->data().toLatin1()))) {
                        m_chatPanel->addUserImage(image.scaled(256, 256, Qt::KeepAspectRatio,
                                                               Qt::SmoothTransformation));
                    }
                }
                // OPENMV-DIFF //
            }
        // OPENMV-DIFF //
        } else if (kind == QLatin1String("session_info_update")) {
            if (const auto *info = update.get<SessionInfoUpdate>()) {
                m_agentTitle = info->title().value_or(QString()).trimmed();
                updateTitle();
            }
        // OPENMV-DIFF //
        } else if (kind == QLatin1String("tool_call")) {
            if (const auto *tc = update.get<ToolCall>())
                m_chatPanel->addToolCall(*tc);
        } else if (kind == QLatin1String("tool_call_update")) {
            if (const auto *tcu = update.get<ToolCallUpdate>())
                m_chatPanel->updateToolCall(*tcu);
        } else if (kind == QLatin1String("plan")) {
            if (const auto *p = update.get<Plan>())
                m_chatPanel->addPlan(*p);
        } else if (kind == QLatin1String("config_option_update")) {
            if (const auto *cu = update.get<ConfigOptionUpdate>())
                m_chatPanel->updateConfigOptions(cu->configOptions());
        } else if (kind == QLatin1String("available_commands_update")) {
            if (const auto *acu = update.get<AvailableCommandsUpdate>())
                m_chatPanel->updateAvailableCommands(acu->availableCommands());
        }
    });
    connect(m_controller, &AcpChatController::permissionRequested,
            this, [this](const QJsonValue &id, const Acp::RequestPermissionRequest &request) {
        m_chatPanel->addPermissionRequest(id, request);
    });
    connect(m_chatPanel, &ChatPanel::permissionOptionSelected,
            m_controller, &AcpChatController::sendPermissionResponse);
    connect(m_chatPanel, &ChatPanel::permissionCancelled,
            m_controller, &AcpChatController::sendPermissionCancelled);

    connect(m_controller, &AcpChatController::sessionSelectionRequired, this, [this] {
        m_chatPanel->resolveAuthentication();
        m_stack->setCurrentIndex(2);
        // OPENMV-DIFF //
        // A chat reopened after a restart: load its session (replays the history)
        // instead of offering the picker, if the agent can.
        const QString restoreId = std::exchange(m_restoreSessionId, QString());
        if (!restoreId.isEmpty() && m_controller->supportsLoadSession()) {
            m_loadingRestoredSession = true;
            showLoading();
            m_controller->loadSession(restoreId,
                                      FilePath::fromUserInput(m_restoreWorkingDirectory));
            return;
        }
        // OPENMV-DIFF //
        showSessionPicker();
    });
    connect(m_controller, &AcpChatController::sessionLoaded, this, [this]() {
        m_stack->setCurrentIndex(2);
        m_chatPanel->setSendEnabled(true);
        // OPENMV-DIFF //
        m_loadingRestoredSession = false;
        m_loadingSession = false;
        rememberCustomTitle();
        // OPENMV-DIFF //
    });

    connect(m_controller, &AcpChatController::promptFinished, this, [this] {
        m_chatPanel->setPrompting(false);
        m_chatPanel->finishAgentMessage();
    });
    connect(m_controller, &AcpChatController::errorOccurred, this, [this](const QString &msg) {
        // OPENMV-DIFF //
        // A session that couldn't be loaded: back to the chat page (not the agent
        // list) to show the error; a reopened chat also gets the picker.
        if (std::exchange(m_loadingSession, false))
            m_stack->setCurrentIndex(2);
        if (std::exchange(m_loadingRestoredSession, false))
            showSessionPicker();
        // OPENMV-DIFF //
        if (m_stack->currentIndex() == 3)
            m_stack->setCurrentIndex(0);
        m_chatPanel->addErrorMessage(msg);
        // Also show on the connection page so errors are visible if we switch back
        m_connectionErrorLabel->setText(
            QStringLiteral("<b>\u26A0 Error:</b> %1").arg(msg.toHtmlEscaped()));
        m_connectionErrorLabel->show();
    });
}

AcpChatTab::~AcpChatTab()
{
    // Disconnect all signals from the controller before the blocking
    // disconnectFromServer() call. That call pumps the event loop
    // (waitForFinished), which can deliver signals back to this
    // half-destroyed widget and its already-destroyed siblings.
    m_controller->disconnect(this);
    m_controller->disconnectFromServer();
}

void AcpChatTab::setInspector(AcpInspector *inspector)
{
    m_controller->setInspector(inspector);
}

bool AcpChatTab::hasFocus() const
{
    return m_chatPanel->inputEdit()->hasFocus();
}

void AcpChatTab::setFocus()
{
    m_chatPanel->inputEdit()->setFocus();
}

QString AcpChatTab::title() const
{
    return m_title;
}

// OPENMV-DIFF //
QWidget *AcpChatTab::sessionToolBar() const
{
    return m_chatPanel->sessionToolBar();
}

QString AcpChatTab::toolTip() const
{
    if (m_currentServerName.isEmpty())
        return {};
    QString full = !m_customTitle.isEmpty() ? m_customTitle
                   : !m_agentTitle.isEmpty() ? m_agentTitle
                                             : m_firstPrompt.trimmed();
    // At most 6 lines and 300 characters.
    const QStringList lines = full.split(QLatin1Char('\n'));
    bool cut = lines.size() > 6;
    full = lines.mid(0, 6).join(QLatin1Char('\n'));
    if (full.size() > 300) {
        full = full.left(300);
        cut = true;
    }
    if (cut)
        full = full.trimmed() + QChar(0x2026);
    return full.isEmpty() ? m_currentServerName
                          : QString("%1: %2").arg(m_currentServerName, full);
}
// OPENMV-DIFF //

static IDocument *currentNonTemporaryDocument()
{
    IDocument *currentDocument = EditorManager::currentDocument();
    if (currentDocument && (currentDocument->filePath().isEmpty() || currentDocument->isTemporary()))
        return nullptr;
    return currentDocument;
}

void AcpChatTab::showSessionPicker()
{
    auto *picker = m_chatPanel->addSessionPicker();
    m_activePicker = picker;
    m_chatPanel->setSendEnabled(true);
    connect(picker, &QObject::destroyed, this, [this] { m_activePicker = nullptr; });

    if (const Project *startup = ProjectManager::startupProject())
        picker->setCurrentProjectDir(startup->projectDirectory());

    QList<SessionPickerWidget::NewSessionTarget> targets;
    IDocument *currentDocument = currentNonTemporaryDocument();
    Project *projectForCurrentDocument = currentDocument ? ProjectManager::projectForFile(
                                                               currentDocument->filePath())
                                                         : nullptr;
    if (currentDocument && !projectForCurrentDocument) {
        SessionPickerWidget::NewSessionTarget t;
        t.label = currentDocument->displayName();
        t.cwd = currentDocument->filePath().parentDir();
        t.tooltip = t.cwd.toUserOutput();
        targets.append(t);
    }
    for (const Project *p : ProjectManager::projects()) {
        SessionPickerWidget::NewSessionTarget t;
        t.label = p->displayName();
        t.cwd = p->projectDirectory();
        t.tooltip = p->projectDirectory().toUserOutput();
        if (p == projectForCurrentDocument)
            targets.prepend(t);
        else
            targets.append(t);
    }
    picker->setNewSessionTargets(targets);

    connect(picker, &SessionPickerWidget::sessionSelected, this,
            [this, picker](const QString &sessionId, const FilePath &cwd) {
        picker->setResolved(sessionId);
        showLoading(); // OPENMV-DIFF: not the history arriving piece by piece
        m_controller->loadSession(sessionId, cwd);
    });
    connect(picker, &SessionPickerWidget::newSessionRequested,
            this, [this](const FilePath &cwd) {
        m_controller->createNewSession(cwd);
    });
    connect(picker, &SessionPickerWidget::loadMoreRequested,
            m_controller, &AcpChatController::listSessions);

    if (!m_controller->supportsSessionList())
        return;

    auto isFirstPage = QSharedPointer<bool>::create(true);
    connect(m_controller, &AcpChatController::sessionsListed, picker,
            [picker, isFirstPage](
                const QList<Acp::SessionInfo> &sessions,
                const std::optional<QString> &nextCursor) {
        if (*isFirstPage) {
            *isFirstPage = false;
            picker->setInitialSessions(sessions, nextCursor);
        } else {
            picker->appendSessions(sessions, nextCursor);
        }
    });

    m_controller->listSessions();
}

void AcpChatTab::populateServerButtons()
{
    // OPENMV-DIFF //
    while (QLayoutItem *item = m_serverGroupsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    // One entry per registry agent plus the servers that were not made from
    // the registry. Configured or not, an entry connects; unconfigured ones
    // are added as servers first.
    struct Entry
    {
        QString serverId;   // empty until configured
        QString registryId; // empty for custom servers
        QString name;
        QString iconUrl;
        QString description;
    };
    const QList<AcpSettings::ServerInfo> servers = AcpSettings::servers();
    const QList<AcpSettings::RegistryAgent> agents = AcpSettings::registryAgents();
    const auto serverFor = [&servers](const QString &registryId) -> const AcpSettings::ServerInfo * {
        for (const AcpSettings::ServerInfo &s : servers) {
            if (!registryId.isEmpty() && s.registryId == registryId)
                return &s;
        }
        return nullptr;
    };
    const auto entryFor = [&](const AcpSettings::RegistryAgent &agent) {
        const AcpSettings::ServerInfo *s = serverFor(agent.id);
        return Entry{s ? s->id : QString(), agent.id, s ? s->name : agent.name, agent.iconUrl, agent.description};
    };

    // Group the registry: listed ids in their group's order, the rest in the
    // last group, custom servers after the popular ones.
    const QList<AcpSettings::AgentGroup> groups = AcpSettings::agentGroups();
    QList<QList<Entry>> grouped(groups.size());
    QStringList placed;
    for (int g = 0; g < groups.size(); ++g) {
        for (const char *id : groups.at(g).ids) {
            for (const AcpSettings::RegistryAgent &agent : agents) {
                if (agent.id == QLatin1String(id)) {
                    grouped[g].push_back(entryFor(agent));
                    placed << agent.id;
                }
            }
        }
    }
    for (const AcpSettings::RegistryAgent &agent : agents) {
        if (!placed.contains(agent.id))
            grouped.last().push_back(entryFor(agent));
    }
    for (const AcpSettings::ServerInfo &s : servers) {
        const bool fromRegistry = std::any_of(agents.begin(), agents.end(),
                                              [&s](const AcpSettings::RegistryAgent &a) { return a.id == s.registryId; });
        if (!fromRegistry)
            grouped.first().push_back({s.id, QString(), s.name, s.iconUrl, QString()});
    }

    const int columns = 3;
    int total = 0;
    for (int g = 0; g < groups.size(); ++g) {
        const QList<Entry> &entries = grouped.at(g);
        if (entries.isEmpty())
            continue;
        total += entries.size();

        auto *grid = new QWidget;
        auto *gridLayout = new QGridLayout(grid);
        gridLayout->setContentsMargins(0, 0, 0, 0);
        gridLayout->setSpacing(6);
        int n = 0;
        for (const Entry &entry : entries) {
            auto *button = new QtcButton(entry.name, QtcButton::MediumTertiary);
            button->setToolTip(entry.description.isEmpty() ? entry.name : entry.description);
            button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            const QString serverId = entry.serverId;
            const QString registryId = entry.registryId;
            const QString serverName = entry.name;
            connect(button, &QAbstractButton::clicked, this, [this, serverId, registryId, serverName] {
                // Adding a server repopulates this page, deleting the button
                // that was clicked, so finish on the event loop.
                QTimer::singleShot(0, this, [this, serverId, registryId, serverName] {
                    QString id = serverId;
                    QString name = serverName;
                    if (id.isEmpty()) {
                        id = AcpSettings::ensureRegistryServer(registryId);
                        for (const AcpSettings::ServerInfo &info : AcpSettings::servers()) {
                            if (info.id == id)
                                name = info.name;
                        }
                    }
                    if (!id.isEmpty())
                        connectToConfiguredServer(id, name);
                });
            });
            // Reserve the icon's space now, so the button does not widen when
            // the icon arrives.
            const int iconSize = button->fontMetrics().height();
            QPixmap blank(iconSize, iconSize);
            blank.fill(Qt::transparent);
            button->setPixmap(blank);
            Utils::onResultReady(
                AcpSettings::iconForUrl(entry.iconUrl), button, [button, iconSize](const QIcon &icon) {
                    button->setPixmap(icon.pixmap(iconSize, iconSize));
                });
            gridLayout->addWidget(button, n / columns, n % columns);
            ++n;
        }
        for (int c = 0; c < columns; ++c)
            gridLayout->setColumnStretch(c, 1);

        const QString title = QString::fromUtf8(groups.at(g).title);
        if (title.isEmpty()) {
            m_serverGroupsLayout->addWidget(grid);
            continue;
        }

        // A disclosure row in the hairline's style: "▸ More providers (12) ────".
        const Utils::Key settingsKey = Utils::keyFromString(QStringLiteral("AcpClient/AgentGroupOpen/") + QLatin1String(groups.at(g).key));
        const bool open = Core::ICore::settings()->value(settingsKey, false).toBool();
        auto *header = new QWidget;
        auto *headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(0, 6, 0, 0);
        headerLayout->setSpacing(6);
        auto *toggle = new QToolButton;
        toggle->setAutoRaise(true);
        toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        toggle->setText(Tr::tr("%1 (%2)").arg(Tr::tr(groups.at(g).title)).arg(entries.size()));
        toggle->setCursor(Qt::PointingHandCursor);
        toggle->setStyleSheet(QString::fromLatin1(
            "QToolButton { background: transparent; border: none; padding: 2px 4px; }"
            "QToolButton:hover { background-color: %1; }")
            .arg(Utils::creatorColor(Utils::Theme::BackgroundColorHover).name()));
        headerLayout->addWidget(toggle);
        headerLayout->addWidget(Layouting::createHr(), 1);
        m_serverGroupsLayout->addWidget(header);
        m_serverGroupsLayout->addWidget(grid);
        grid->setVisible(open);
        connect(toggle, &QToolButton::clicked, grid, [grid, toggle, settingsKey] {
            const bool nowOpen = !grid->isVisible();
            grid->setVisible(nowOpen);
            toggle->setArrowType(nowOpen ? Qt::DownArrow : Qt::RightArrow);
            Core::ICore::settings()->setValue(settingsKey, nowOpen);
        });
    }


    m_configStack->setCurrentIndex(total == 0 ? 0 : 1);
    // OPENMV-DIFF //
}

// OPENMV-DIFF //
void AcpChatTab::connectToConfiguredServer(const QString &serverId, const QString &serverName)
{
    m_connectionErrorLabel->hide();
    m_currentServerName = serverName;
    // OPENMV-DIFF //
    m_currentServerId = serverId;
    fetchIcon(serverId);
    // OPENMV-DIFF //
    updateTitle();
    m_initializingLabel->setText(Tr::tr("Connecting to %1").arg(serverName));
    m_initializingCancel->show(); // OPENMV-DIFF: hidden while a chat loads
    m_stack->setCurrentIndex(3);
    m_controller->connectToServer(serverId);
}
// OPENMV-DIFF //

void AcpChatTab::updateTitle()
{
    // OPENMV-DIFF //
    // m_title = m_currentServerName.isEmpty() ? Tr::tr("New Chat") : m_currentServerName;
    // OPENMV-DIFF //
    // The chat's own title, else the start of the first message (first line, cut
    // to a tab's length), else the agent's name until something is said.
    QString title = !m_customTitle.isEmpty() ? m_customTitle : m_agentTitle;
    if (title.isEmpty() && !m_firstPrompt.isEmpty()) {
        title = m_firstPrompt.section(QLatin1Char('\n'), 0, 0).simplified();
        if (title.size() > 32)
            title = title.left(31).trimmed() + QChar(0x2026); // ...
    }
    if (title.isEmpty())
        title = m_currentServerName;
    m_title = title.isEmpty() ? Tr::tr("New Chat") : title;
    // OPENMV-DIFF //
    emit titleChanged();
}

// OPENMV-DIFF //
void AcpChatTab::fetchIcon(const QString &serverId)
{
    for (const AcpSettings::ServerInfo &info : AcpSettings::servers()) {
        if (info.id == serverId && !info.iconUrl.isEmpty()) {
            Utils::onResultReady(AcpSettings::iconForUrl(info.iconUrl), this,
                                 [this](const QIcon &icon) {
                                     m_icon = icon;
                                     emit titleChanged();
                                 });
            return;
        }
    }
}

void AcpChatTab::setCustomTitle(const QString &title)
{
    m_customTitle = title.simplified();
    // Saved per session (an empty name removes it); a chat without a session yet
    // saves it once the session starts (rememberCustomTitle()).
    AcpSettings::setChatTitle(m_controller->sessionId(), m_customTitle);
    updateTitle();
}

// When a session starts or is resumed: keep a name given before it existed, or
// pick up the one given earlier (in this tab, another one, or the picker).
void AcpChatTab::rememberCustomTitle()
{
    const QString sessionId = m_controller->sessionId();
    if (sessionId.isEmpty())
        return;
    if (!m_customTitle.isEmpty()) {
        AcpSettings::setChatTitle(sessionId, m_customTitle);
        return;
    }
    m_customTitle = AcpSettings::chatTitle(sessionId);
    if (!m_customTitle.isEmpty())
        updateTitle();
}

QVariantMap AcpChatTab::saveState() const
{
    // A reopened chat that was never shown keeps what it was restored with.
    const QString sessionId = m_restorePending ? m_restoreSessionId : m_controller->sessionId();
    if (m_currentServerId.isEmpty() || sessionId.isEmpty())
        return {};
    const QString workingDirectory = m_restorePending
                                         ? m_restoreWorkingDirectory
                                         : m_controller->workingDirectory().toFSPathString();
    QString registryId;
    for (const AcpSettings::ServerInfo &info : AcpSettings::servers()) {
        if (info.id == m_currentServerId)
            registryId = info.registryId;
    }
    return {{"serverId", m_currentServerId},
            {"registryId", registryId},
            {"serverName", m_currentServerName},
            {"sessionId", sessionId},
            {"workingDirectory", workingDirectory},
            {"customTitle", m_customTitle},
            {"agentTitle", m_agentTitle},
            {"firstPrompt", m_firstPrompt}};
}

void AcpChatTab::restoreState(const QVariantMap &state)
{
    QString serverId = state.value("serverId").toString();
    const QString sessionId = state.value("sessionId").toString();
    if (serverId.isEmpty() || sessionId.isEmpty())
        return;
    // The saved id may predate the agent list being saved (ids were recreated on
    // every launch then): find the agent by the registry agent it was made from.
    const QList<AcpSettings::ServerInfo> servers = AcpSettings::servers();
    const bool known = std::any_of(servers.begin(), servers.end(),
                                   [&serverId](const AcpSettings::ServerInfo &info) {
                                       return info.id == serverId;
                                   });
    const QString registryId = state.value("registryId").toString();
    if (!known && !registryId.isEmpty()) {
        for (const AcpSettings::ServerInfo &info : servers) {
            if (info.registryId == registryId)
                serverId = info.id;
        }
    }
    m_currentServerId = serverId;
    m_currentServerName = state.value("serverName").toString();
    m_restoreSessionId = sessionId;
    m_restoreWorkingDirectory = state.value("workingDirectory").toString();
    m_customTitle = state.value("customTitle").toString();
    m_agentTitle = state.value("agentTitle").toString();
    m_firstPrompt = state.value("firstPrompt").toString();
    m_restorePending = true;
    fetchIcon(serverId);
    updateTitle();
}

// A reopened chat connects to its agent the first time it is shown, so startup
// doesn't launch an agent per saved chat.
// While a session loads, the agent replays its history as updates before it
// confirms the load; show the spinner page meanwhile instead of the chat filling
// in piece by piece. sessionLoaded switches to the chat.
void AcpChatTab::showLoading()
{
    m_loadingSession = true;
    m_initializingLabel->setText(Tr::tr("Loading chat..."));
    m_stack->setCurrentIndex(3);
    revealCancelLater();
}

// For steps the user didn't start (loading a chat, a reopened chat connecting):
// no Cancel cluttering the spinner, unless the step is still going after 5 s
// (the agent hangs, a very long history). Connecting to an agent the user just
// picked shows Cancel at once instead.
void AcpChatTab::revealCancelLater()
{
    m_initializingCancel->hide();
    const int generation = ++m_loadingGeneration;
    QTimer::singleShot(5000, this, [this, generation] {
        if ((generation == m_loadingGeneration) && (m_stack->currentIndex() == 3))
            m_initializingCancel->show();
    });
}

void AcpChatTab::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (std::exchange(m_restorePending, false)) {
        connectToConfiguredServer(m_currentServerId, m_currentServerName);
        revealCancelLater(); // the IDE reopened this chat, the user didn't pick it
    }
}
// OPENMV-DIFF //

} // namespace AcpClient::Internal
