// Copyright (C) 2026 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "mcpserver_global.h"

#include "../schemas/schema_2025_11_25.h"
#include "mcpserver.h"

namespace Mcp {

class MCPSERVER_EXPORT ToolRegistry : public QObject
{
    Q_OBJECT
public:
    static void registerTool(
        const Generated::Schema::_2025_11_25::Tool &tool,
        const Server::ToolInterfaceCallback &callback);
    static void registerTool(
        const Generated::Schema::_2025_11_25::Tool &tool, const Server::ToolCallback &callback);

    static const ToolRegistry &instance();

    // OPENMV-DIFF //
    // Resources registered the same append-only way as tools, replayed by
    // AutoRegisteringServer whether it exists yet or not.
    static void registerResource(
        const Generated::Schema::_2025_11_25::Resource &resource,
        const Server::ResourceCallback &callback);
    // OPENMV-DIFF //

signals:
    void toolRegistered();
    // OPENMV-DIFF //
    void resourceRegistered();
    // OPENMV-DIFF //
};

class MCPSERVER_EXPORT AutoRegisteringServer : public Server, public QObject
{
public:
    AutoRegisteringServer(Generated::Schema::_2025_11_25::Implementation serverInfo);

private:
    std::size_t m_nTools = 0;
    // OPENMV-DIFF //
    std::size_t m_nResources = 0;
    // OPENMV-DIFF //
};

} // namespace Mcp
