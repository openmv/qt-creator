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
    // The registry agents people actually use, most used first; the rest of the
    // registry follows them in the chat's agent grid.
    static QList<const char *> rankedRegistryAgents()
    {
        return {"claude-acp", "codex-acp", "gemini", "github-copilot-cli", "cursor", "antigravity-acp",
                "qwen-code", "kimi", "glm-acp-agent", "minimax-code", "codebuddy-code", "qoder",
                "grok-build", "mistral-vibe", "opencode", "cline", "kilo", "goose", "amp-acp",
                "devin", "junie", "factory-droid", "auggie", "bob", "cortex-code", "poolside"};
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
