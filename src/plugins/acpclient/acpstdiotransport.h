// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "acptransport.h"

#include <utils/commandline.h>
#include <utils/environment.h>
#include <utils/filepath.h>

#include <optional>

namespace Utils { class Process; }

namespace AcpClient::Internal {

// OPENMV-DIFF //
// The bin directory of the Node.js OpenMV IDE ships, or empty: used when the
// machine has no npx/node/npm of its own.
Utils::FilePath bundledNodeBinDir();

// How an agent's configured command actually launches: npx/node/npm through
// the bundled Node.js when the machine has none, and the command's own
// directory on PATH. `environment` is set only when it had to change.
struct AgentLaunch
{
    Utils::CommandLine command;
    std::optional<Utils::Environment> environment;
};
AgentLaunch resolveAgentLaunch(const Utils::CommandLine &command,
                               const std::optional<Utils::Environment> &environment);
// OPENMV-DIFF //

class AcpStdioTransport : public AcpTransport
{
    Q_OBJECT

public:
    explicit AcpStdioTransport(QObject *parent = nullptr);
    ~AcpStdioTransport() override;

    void setCommandLine(const Utils::CommandLine &cmd);
    void setWorkingDirectory(const Utils::FilePath &workingDirectory);
    void setEnvironment(const Utils::Environment &environment);

    void start() override;
    void stop() override;

protected:
    void sendData(const QByteArray &data) override;

private:
    void readOutput();
    void readError();

    Utils::CommandLine m_cmd;
    Utils::FilePath m_workingDirectory;
    std::optional<Utils::Environment> m_env;
    Utils::Process *m_process = nullptr;
    bool m_expectStop = false;
    QString m_stderrBuffer;
};

} // namespace AcpClient::Internal
