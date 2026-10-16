// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acpstdiotransport.h"

#include "acpclienttr.h"

#include <utils/fileutils.h>
#include <utils/qtcprocess.h>

#include <QLoggingCategory>

// OPENMV-DIFF //
#include <coreplugin/icore.h>
#include <utils/environment.h>
#include <utils/hostosinfo.h>

namespace AcpClient::Internal {

// The Node.js OpenMV IDE ships (share/qtcreator/node/<platform>), resolved
// the way the Copilot plugin does; empty when this platform has none.
Utils::FilePath bundledNodeBinDir()
{
    using namespace Utils;
    FilePath dir;
    if (HostOsInfo::isWindowsHost()) {
        dir = Core::ICore::resourcePath("node/windows");
    } else if (HostOsInfo::isMacHost()) {
        dir = Core::ICore::resourcePath(
            ((HostOsInfo::hostArchitecture() == OsArchArm64) || HostOsInfo::isRunningUnderRosetta())
                ? QString("node/mac-aarch64/bin") : QString("node/mac/bin"));
    } else if (HostOsInfo::isLinuxHost()) {
        const QString arch = QSysInfo::buildCpuArchitecture();
        if (arch == "x86_64") dir = Core::ICore::resourcePath("node/linux-x64/bin");
        else if (arch == "arm") dir = Core::ICore::resourcePath("node/linux-armv7/bin");
        else if (arch == "arm64") dir = Core::ICore::resourcePath("node/linux-arm64/bin");
    }
    return (dir.isEmpty() || !dir.exists()) ? FilePath() : dir;
}

} // namespace AcpClient::Internal
// OPENMV-DIFF //

static Q_LOGGING_CATEGORY(logStdio, "qtc.acpclient.stdio", QtWarningMsg);

using namespace Utils;

namespace AcpClient::Internal {

AcpStdioTransport::AcpStdioTransport(QObject *parent)
    : AcpTransport(parent)
{}

AcpStdioTransport::~AcpStdioTransport()
{
    stop();
}

void AcpStdioTransport::setCommandLine(const CommandLine &cmd)
{
    m_cmd = cmd;
    if (HostOsInfo::isWindowsHost() && m_cmd.executable().baseName() == "npx"
        && !m_cmd.arguments().contains("--yes")) {
        m_cmd.setArguments("--yes " + m_cmd.arguments());
    }
}

void AcpStdioTransport::setWorkingDirectory(const FilePath &workingDirectory)
{
    m_workingDirectory = workingDirectory;
}

void AcpStdioTransport::setEnvironment(const Environment &environment)
{
    m_env = environment;
}

void AcpStdioTransport::start()
{
    if (m_process) {
        if (m_process->isRunning())
            m_process->kill();
        m_process->deleteLater(); // avoid crash if this get's called from a slot handling a process signal
    }

    m_stderrBuffer.clear();

    FilePath executable = m_cmd.executable();
    if (executable.isEmpty()) {
        emit errorOccurred(Tr::tr("No command configured for ACP server."));
        emit finished();
        return;
    }
    if (!executable.isExecutableFile()) {
        executable = executable.searchInPath(FileUtils::usefulExtraSearchPaths());
        if (!executable.isExecutableFile()) {
            // Report the originally configured path in the error message, which is more likely to
            // be helpful to the user than the searched path
            executable = m_cmd.executable();
            const QString errorMessage
                = executable.isAbsolutePath()
                      ? Tr::tr("Command not found: \"%1\". Check that it exists and is executable.")
                      : Tr::tr(
                            "Command not found: \"%1\". Check that it is executable and on your "
                            "PATH.");
            emit errorOccurred(errorMessage.arg(executable.toUserOutput()));
            emit finished();
            return;
        }
        m_cmd.setExecutable(executable);
    }

    m_process = new Process(this);
    m_process->setProcessMode(ProcessMode::Writer);

    connect(m_process, &Process::readyReadStandardOutput, this, &AcpStdioTransport::readOutput);
    connect(m_process, &Process::readyReadStandardError, this, &AcpStdioTransport::readError);
    connect(m_process, &Process::started, this, &AcpTransport::started);
    connect(m_process, &Process::done, this, [this] {
        if (!m_expectStop && m_process->result() != ProcessResult::FinishedWithSuccess) {
            qCWarning(logStdio) << "Process finished with error:" << m_cmd.toUserOutput();
            QString msg = m_process->exitMessage();
            const QString stderrText = m_stderrBuffer.trimmed();
            if (!stderrText.isEmpty())
                msg += QStringLiteral("\n\n%1").arg(stderrText);
            emit errorOccurred(msg);
        }
        m_expectStop = false;
        emit finished();
    });

    // OPENMV-DIFF //
    // Registry agents launch through npx/node/npm. When none is installed on
    // the machine, fall back to the Node.js OpenMV IDE ships (for Copilot) and
    // put its bin directory first on PATH so "#!/usr/bin/env node" resolves.
    CommandLine cmd = m_cmd;
    Environment env = m_env ? *m_env : m_cmd.executable().deviceEnvironment();
    {
        const QString tool = cmd.executable().fileName();
        const bool nodeTool = (tool == "npx") || (tool == "node") || (tool == "npm")
                              || (tool == "npx.cmd") || (tool == "node.exe") || (tool == "npm.cmd");
        if (nodeTool && cmd.executable().parentDir().isEmpty()
            && cmd.executable().searchInPath().isEmpty()) {
            const FilePath binDir = bundledNodeBinDir();
            if (!binDir.isEmpty()) {
                const QString base = QFileInfo(tool).completeBaseName();
                const FilePath node = binDir.pathAppended(HostOsInfo::isWindowsHost() ? QString("node.exe") : QString("node"));
                // npm/npx are JavaScript: run their CLI through the bundled node
                // rather than the bin/ launcher (a copied symlink that no longer
                // finds its own lib/). Layouts: <prefix>/lib/node_modules/npm on
                // Unix, <prefix>/node_modules/npm on Windows.
                const FilePath npmBin = HostOsInfo::isWindowsHost()
                    ? binDir.pathAppended("node_modules/npm/bin")
                    : binDir.parentDir().pathAppended("lib/node_modules/npm/bin");
                if (base == "node" && node.exists()) {
                    cmd.setExecutable(node);
                } else if ((base == "npx" || base == "npm") && node.exists()
                           && npmBin.pathAppended(base + "-cli.js").exists()) {
                    cmd = CommandLine(node, {npmBin.pathAppended(base + "-cli.js").path()});
                    cmd.addArgs(m_cmd.arguments(), CommandLine::Raw);
                }
                if (cmd.executable() != m_cmd.executable()) {
                    qCInfo(logStdio) << "Using the bundled Node.js for" << tool << ":" << cmd.toUserOutput();
                    env.prependOrSetPath(binDir);
                }
            }
        }
    }
    m_process->setCommand(cmd);
    if (!m_workingDirectory.isEmpty())
        m_process->setWorkingDirectory(m_workingDirectory);
    m_process->setEnvironment(env);
    // OPENMV-DIFF //

    qCDebug(logStdio) << "Starting:" << m_cmd.toUserOutput();
    m_process->start();
}

void AcpStdioTransport::stop()
{
    if (m_process && m_process->isRunning()) {
        m_expectStop = true;
        m_process->kill();
        m_process->waitForFinished(QDeadlineTimer(3000));
    }
    delete m_process;
    m_process = nullptr;
}

void AcpStdioTransport::sendData(const QByteArray &data)
{
    if (!m_process) {
        emit errorOccurred(
            Tr::tr("Cannot send data: process has not been started (%1).").arg(m_cmd.toUserOutput()));
        return;
    }
    if (m_process->state() != QProcess::Running) {
        QString msg = Tr::tr("Cannot send data: process \"%1\" is not running (exit code %2).")
                          .arg(m_cmd.toUserOutput())
                          .arg(m_process->exitCode());
        const QString stderrText = m_stderrBuffer.trimmed();
        if (!stderrText.isEmpty())
            msg += QStringLiteral("\n\n%1").arg(stderrText);
        emit errorOccurred(msg);
        return;
    }
    m_process->writeRaw(data);
}

void AcpStdioTransport::readOutput()
{
    if (!m_process)
        return;
    parseData(m_process->readAllRawStandardOutput());
}

void AcpStdioTransport::readError()
{
    if (!m_process)
        return;
    const QByteArray data = m_process->readAllRawStandardError();
    const QString text = QString::fromUtf8(data);
    qCDebug(logStdio) << "stderr:" << data;

    // Keep a bounded buffer of recent stderr for inclusion in error messages
    m_stderrBuffer += text;
    static constexpr int kMaxStderrBuffer = 4096;
    if (m_stderrBuffer.size() > kMaxStderrBuffer)
        m_stderrBuffer = m_stderrBuffer.right(kMaxStderrBuffer);
}

} // namespace AcpClient::Internal
