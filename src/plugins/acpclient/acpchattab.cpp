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
        configOuter->addStretch();

        m_configStack = new QStackedWidget;
        // OPENMV-DIFF //
        // m_configStack->setMaximumWidth(480);
        m_configStack->setMaximumWidth(640); // room for the three-column agent grid
        // OPENMV-DIFF //
        m_configStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        // Config stack page 0: no servers configured
        {
            auto *noServerPage = new QWidget;
            auto *noServerLayout = new QVBoxLayout(noServerPage);
            noServerLayout->setSpacing(12);
            noServerLayout->addStretch();

            m_noServerLabel = new InfoLabel(
                Tr::tr("No ACP servers configured. Add a server in the settings to get started."),
                InfoLabel::Information);
            m_noServerLabel->setElideMode(Qt::ElideNone);
            m_noServerLabel->setWordWrap(true);
            noServerLayout->addWidget(m_noServerLabel);

            auto *manageButton = new QtcButton(Tr::tr("Manage Agents..."),
                                               QtcButton::MediumSecondary);
            manageButton->setToolTip(Tr::tr("Open ACP server settings."));
            manageButton->setPixmap(Utils::Icons::SETTINGS.pixmap());
            connect(manageButton, &QAbstractButton::clicked, this, [] {
                Core::ICore::showSettings("AI.ACPSERVERS");
            });
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

            auto *manageButton = new QtcButton(Tr::tr("Manage Agents..."),
                                               QtcButton::MediumGhost);
            manageButton->setToolTip(Tr::tr("Open ACP server settings."));
            manageButton->setPixmap(Utils::Icons::SETTINGS.pixmap());
            connect(manageButton, &QAbstractButton::clicked, this, [] {
                Core::ICore::showSettings("AI.ACPSERVERS");
            });
            connectLayout->addWidget(Layouting::createHr());
            connectLayout->addWidget(manageButton);

            connectLayout->addStretch();

            m_configStack->addWidget(connectPage); // index 1
        }

        auto *configCenter = new QHBoxLayout;
        configCenter->addStretch();
        configCenter->addWidget(m_configStack);
        configCenter->addStretch();
        configOuter->addLayout(configCenter);
        configOuter->addStretch();

        m_stack->addWidget(configPage);  // index 0
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
            m_controller->disconnectFromServer();
            m_stack->setCurrentIndex(0);
        });

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
    connect(m_chatPanel, &ChatPanel::sendRequested, this, [this](const QString &text) {
        if (m_activePicker) {
            QObject::disconnect(m_controller, &AcpChatController::sessionsListed,
                                m_activePicker, nullptr);
            const std::optional<SessionPickerWidget::NewSessionTarget> firstSessionTarget
                = m_activePicker->firstNewSessionTarget();
            m_activePicker->deleteLater();
            m_pendingPrompt = text;
            const Project *project = ProjectManager::startupProject();
            const FilePath sessionPath = firstSessionTarget ? firstSessionTarget->cwd
                                         : project          ? project->projectDirectory()
                                                            : FilePath();
            m_controller->createNewSession(sessionPath);
            return;
        }
        m_chatPanel->addUserMessage(text);
        m_chatPanel->setPrompting(true);
        m_controller->sendPrompt(
            text, m_chatPanel->manualContextFiles(), m_chatPanel->includeCurrentEditorContext());
    });
    connect(m_chatPanel, &ChatPanel::cancelRequested, m_controller, &AcpChatController::cancelPrompt);
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
    connect(m_controller, &AcpChatController::sessionCreated, this, [this]() {
        m_chatPanel->resolveAuthentication();
        m_stack->setCurrentIndex(2);
        m_chatPanel->setSendEnabled(true);
        if (m_pendingPrompt.isEmpty()) {
            m_chatPanel->appendAgentText(
                "Cute Greetings,\n\n your AI Agent is ready and you can start chatting.");
            m_chatPanel->finishAgentMessage();
        } else  {
            const QString text = m_pendingPrompt;
            m_pendingPrompt.clear();
            m_chatPanel->addUserMessage(text);
            m_chatPanel->setPrompting(true);
            m_controller->sendPrompt(
                text, m_chatPanel->manualContextFiles(), m_chatPanel->includeCurrentEditorContext());
        }
    });
    connect(m_controller, &AcpChatController::configOptionsReceived,
            m_chatPanel, &ChatPanel::updateConfigOptions);
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
                if (const auto *textBlock = std::get_if<TextContent>(&chunk->content()))
                    m_chatPanel->addUserMessage(textBlock->text());
            }
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
        showSessionPicker();
    });
    connect(m_controller, &AcpChatController::sessionLoaded, this, [this]() {
        m_stack->setCurrentIndex(2);
        m_chatPanel->setSendEnabled(true);
    });

    connect(m_controller, &AcpChatController::promptFinished, this, [this] {
        m_chatPanel->setPrompting(false);
        m_chatPanel->finishAgentMessage();
    });
    connect(m_controller, &AcpChatController::errorOccurred, this, [this](const QString &msg) {
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
            Utils::onResultReady(
                AcpSettings::iconForUrl(entry.iconUrl), button, [button](const QIcon &icon) {
                    const int size = button->fontMetrics().height();
                    button->setPixmap(icon.pixmap(size, size));
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
    updateTitle();
    m_initializingLabel->setText(Tr::tr("Connecting to %1").arg(serverName));
    m_stack->setCurrentIndex(3);
    m_controller->connectToServer(serverId);
}
// OPENMV-DIFF //

void AcpChatTab::updateTitle()
{
    m_title = m_currentServerName.isEmpty() ? Tr::tr("New Chat") : m_currentServerName;
    emit titleChanged();
}

} // namespace AcpClient::Internal
