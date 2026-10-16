// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/commandline.h>
#include <utils/environment.h>

#include <QFuture>
#include <QIcon>
#include <QObject>

namespace AcpClient::Internal {

class AcpSettings : public QObject
{
    Q_OBJECT
public:
    struct ServerInfo
    {
        QString id;
        QString name;
        QString iconUrl;
        QString registryId; // OPENMV-DIFF: the registry agent this was made from, if any
        Utils::CommandLine launchCommand;
        Utils::EnvironmentChanges envChanges;
    };

    static QFuture<QIcon> iconForUrl(const QString &url);

    ~AcpSettings() override;

    static AcpSettings &instance();

    static QList<ServerInfo> servers();
    // OPENMV-DIFF //
    struct RegistryAgent
    {
        QString id;
        QString name;
        QString description;
        QString iconUrl;
    };
    static QList<RegistryAgent> registryAgents(); // empty until the registry is fetched
    // The server made from registry agent `id`, creating (and saving) it if
    // there is none; empty when the registry has no such agent.
    static QString ensureRegistryServer(const QString &id);
    // Registry ids of the agents shown first in a new chat, in display order.
    static QList<const char *> defaultRegistryAgents() { return {"claude-acp", "codex-acp", "gemini"}; }
    // The chat's agent picker: a few popular agents always shown, the rest in
    // collapsible groups. Agents missing from every group land in the last one.
    struct AgentGroup
    {
        const char *key;   // settings key
        const char *title; // empty: shown without a header, always open
        QList<const char *> ids;
    };
    static QList<AgentGroup> agentGroups()
    {
        return {{"popular", "", {"claude-acp", "codex-acp", "gemini", "github-copilot-cli", "cursor", "grok-build"}},
                {"providers", QT_TRANSLATE_NOOP("QtC::AcpClient", "More providers"),
                 {"antigravity-acp", "mistral-vibe", "qwen-code", "kimi", "glm-acp-agent", "minimax-code",
                  "codebuddy-code", "qoder", "devin", "junie", "bob", "cortex-code"}},
                {"opensource", QT_TRANSLATE_NOOP("QtC::AcpClient", "Open source"),
                 {"opencode", "cline", "kilo", "goose", "deepagents", "fast-agent", "pi-acp", "vtcode", "stakpak", "sigit"}},
                {"other", QT_TRANSLATE_NOOP("QtC::AcpClient", "Everything else"), {}}};
    }
    // OPENMV-DIFF //

signals:
    void serversChanged();

private:
    AcpSettings();
};

void setupAcpSettings();
void prefetchAcpRegistry();

} // namespace AcpClient::Internal
