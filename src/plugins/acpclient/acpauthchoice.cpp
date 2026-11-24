// Copyright (C) 2026 OpenMV, LLC.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acpauthchoice.h"
#include "acpclienttr.h"

#include <QJsonObject>

namespace AcpClient::Internal {

QList<AuthChoice> AuthChoice::fromJson(const QJsonArray &methods)
{
    QList<AuthChoice> result;
    for (const QJsonValue &value : methods) {
        const QJsonObject method = value.toObject();
        AuthChoice choice;
        choice.id = method.value("id").toString();
        choice.name = method.value("name").toString();
        choice.description = method.value("description").toString();
        choice.link = method.value("link").toString();
        if (choice.id.isEmpty())
            continue;
        if (choice.name.isEmpty())
            choice.name = choice.id;

        const QString type = method.value("type").toString();
        const QJsonObject meta = method.value("_meta").toObject();
        if (type.isEmpty() || type == "agent") {
            choice.kind = Kind::Agent;
            // Gemini: the key travels in authenticate's _meta["api-key"].
            if (meta.contains("api-key")) {
                choice.metaKey = "api-key";
                choice.fields.append({"api-key", Tr::tr("API key:"), true, false});
            } else if (meta.contains("gateway")) {
                choice.metaKey = "gateway";
                choice.fields.append({"baseUrl", Tr::tr("Gateway URL:"), false, false});
            }
        } else if (type == "terminal") {
            choice.kind = Kind::Terminal;
            for (const QJsonValue &arg : method.value("args").toArray())
                choice.args.append(arg.toString());
            const QJsonObject env = method.value("env").toObject();
            for (auto it = env.begin(); it != env.end(); ++it)
                choice.env.insert(it.key(), it.value().toString());
        } else if (type == "env_var") {
            choice.kind = Kind::EnvVar;
            for (const QJsonValue &var : method.value("vars").toArray()) {
                const QJsonObject v = var.toObject();
                const QString name = v.value("name").toString();
                if (name.isEmpty())
                    continue;
                const QString label = v.value("label").toString();
                choice.fields.append({name,
                                      (label.isEmpty() ? name : label) + ':',
                                      v.value("secret").toBool(true),
                                      v.value("optional").toBool(false)});
            }
        } else {
            continue; // a future or vendor-specific type ("_...")
        }
        result.append(choice);
    }
    return result;
}

} // namespace AcpClient::Internal
