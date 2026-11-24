// Copyright (C) 2026 OpenMV, LLC.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include <QJsonArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace AcpClient::Internal {

// A value the user types in to sign in: an API key, a gateway URL, or one of
// the environment variables an env_var method lists.
struct AuthField
{
    QString key;   // environment variable name, or the _meta key ("api-key", "baseUrl")
    QString label; // shown next to the input
    bool secret = true;
    bool optional = false;
};

// One way to sign in to an agent, read from the raw `authMethods` of the
// initialize response. The generated protocol library keeps only the id, name
// and description of each method, which is not enough to run any of these:
//
//   Agent     the agent signs in itself when sent `authenticate` (a browser
//             OAuth flow, usually); some want an API key or gateway URL in the
//             request's _meta (Gemini).
//   Terminal  the client runs the agent's own command with extra arguments in
//             an interactive terminal; exit status 0 means signed in (Claude).
//   EnvVar    the client starts the agent with the listed environment
//             variables set, then sends `authenticate` (Codex API keys).
struct AuthChoice
{
    enum class Kind { Agent, Terminal, EnvVar };

    Kind kind = Kind::Agent;
    QString id;
    QString name;
    QString description;
    QString link;                // where to get a key, if the agent says
    QStringList args;            // Terminal: appended to the agent's command
    QMap<QString, QString> env;  // Terminal: extra environment for that command
    QList<AuthField> fields;     // values the user is asked for
    QString metaKey;             // Agent: _meta key the values go under ("api-key", "gateway")

    // Methods of a type this client doesn't know are left out.
    static QList<AuthChoice> fromJson(const QJsonArray &methods);
};

} // namespace AcpClient::Internal
