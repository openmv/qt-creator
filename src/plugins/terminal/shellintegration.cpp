// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "shellintegration.h"

#include "terminalsettings.h"

#include <utils/environment.h>
#include <utils/filepath.h>
#include <utils/stringutils.h>

#include <QApplication>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(integrationLog, "qtc.terminal.shellintegration", QtWarningMsg)

using namespace Utils;

namespace Terminal {

struct FileToCopy
{
    FilePath source;
    QString destName;
};

// clang-format off
struct
{
    struct
    {
        FilePath rcFile{":/terminal/shellintegrations/shellintegration-bash.sh"};
    } bash;
    struct
    {
        QList<FileToCopy> files{
            {":/terminal/shellintegrations/shellintegration-env.zsh", ".zshenv"},
            {":/terminal/shellintegrations/shellintegration-login.zsh", ".zlogin"},
            {":/terminal/shellintegrations/shellintegration-profile.zsh", ".zprofile"},
            {":/terminal/shellintegrations/shellintegration-rc.zsh", ".zshrc"}
        };
    } zsh;
    struct
    {
        FilePath script{":/terminal/shellintegrations/shellintegration.ps1"};
    } pwsh;
    struct
    {
        FilePath script{":/terminal/shellintegrations/shellintegration-clink.lua"};
    } clink;
    struct
    {
        FilePath script{":/terminal/shellintegrations/shellintegration.fish"};
    } fish;

} filesToCopy;
// clang-format on

bool ShellIntegration::canIntegrate(const Utils::CommandLine &cmdLine)
{
    if (cmdLine.executable().baseName() == "zsh")
        return true;

    if (cmdLine.executable().baseName() == "cmd")
        return true;

    if (!cmdLine.arguments().isEmpty() && cmdLine.arguments() != "-l")
        return false;

    if (cmdLine.executable().baseName() == "bash")
        return true;

    if (cmdLine.executable().baseName() == "pwsh"
        || cmdLine.executable().baseName() == "powershell") {
        return true;
    }

    if (cmdLine.executable().baseName() == "fish")
        return true;

    return false;
}

static QString unescape(QStringView str)
{
    QString result;
    result.reserve(str.length());
    for (int i = 0; i < str.length(); ++i) {
        if (str[i] == '\\' && str.length() > i + 1) {
            if (str[i + 1] == '\\') {
                // e.g. \\ -> "\"
                result.append('\\');
                i++;
                continue;
            } else if (str[i + 1] == 'x' && str.length() > i + 3) {
                // e.g.: \x5c -> \ (0x5c is the ASCII code for \)
                result.append(QChar(str.sliced(i + 2, 2).toUShort(nullptr, 16)));
                i += 3;
                continue;
            }
        }

        result.append(str[i]);
    }
    return result;
}

// OPENMV-DIFF //
// PowerShell reports its directory with backslashes; FilePath only splits on
// '/', so the tab's folder name came out as the whole path. Only for Windows
// shells: elsewhere a backslash is an ordinary file-name character.
static QString toShellPath(const QString &path, bool windowsShell)
{
    return windowsShell ? QString(path).replace('\\', '/') : path;
}
// OPENMV-DIFF //

void ShellIntegration::onOsc(int cmd, std::string_view str, bool initial, bool final)
{
    if (initial)
        m_oscBuffer.clear();

    m_oscBuffer.append(str);

    if (!final)
        return;

    QString d = QString::fromLocal8Bit(m_oscBuffer);
    const auto [command, data] = Utils::splitAtFirst(d, ';');

    if (cmd == 1337) {
        const auto [key, value] = Utils::splitAtFirst(command, '=');
        if (key == QStringView(u"CurrentDir")) {
            // OPENMV-DIFF //
            // const FilePath cwd = m_shell.withNewPath(value.toString());
            // OPENMV-DIFF //
            const FilePath cwd = m_shell.withNewPath(
                toShellPath(value.toString(), m_shell.osType() == OsTypeWindows));
            // OPENMV-DIFF //
            emit currentDirChanged(cwd);
        }

    } else if (cmd == 7) {
        const QString decoded = QUrl::fromPercentEncoding(d.toUtf8());
        // OPENMV-DIFF //
        // const FilePath cwd = m_shell.withNewPath(decoded);
        // OPENMV-DIFF //
        const FilePath cwd = m_shell.withNewPath(
            toShellPath(decoded, m_shell.osType() == OsTypeWindows));
        // OPENMV-DIFF //
        emit currentDirChanged(cwd);
    } else if (cmd == 133) {
        qCDebug(integrationLog) << "OSC 133:" << data;
    } else if (cmd == 633 && command.length() == 1) {
        if (command[0] == 'E') {
            const CommandLine cmdLine = CommandLine::fromUserInput(data.chopped(1).toString());
            emit commandChanged(cmdLine);
        } else if (command[0] == 'D') {
            emit commandChanged({});
        } else if (command[0] == 'P') {
            const auto [key, value] = Utils::splitAtFirst(data, '=');
            if (key == QStringView(u"Cwd")) {
                // OPENMV-DIFF //
                // const FilePath cwd = m_shell.withNewPath(unescape(value.toString()));
                // OPENMV-DIFF //
                const FilePath cwd = m_shell.withNewPath(
                    toShellPath(unescape(value.toString()), m_shell.osType() == OsTypeWindows));
                // OPENMV-DIFF //
                emit currentDirChanged(cwd);
            }
        }
    }
}

void ShellIntegration::onBell()
{
    if (settings().audibleBell.value())
        QApplication::beep();
}

void ShellIntegration::onTitle(const QString &title)
{
    emit titleChanged(title);
}

void ShellIntegration::prepareProcess(Utils::Process &process)
{
    Environment env = process.environment().hasChanges() ? process.environment()
                                                         : Environment::systemEnvironment();
    CommandLine cmd = process.commandLine();

    m_shell = cmd.executable();

    const Result<FilePath> tmpDir = cmd.executable().tmpDir();
    QTC_CHECK_RESULT(tmpDir);
    if (!tmpDir)
        return;

    if (!canIntegrate(cmd))
        return;

    env.set("VSCODE_INJECTION", "1");
    env.set("TERM_PROGRAM", "vscode");

    if (m_shell.isLocal()) {
        env.set("QT_CREATOR_EXECUTABLE_PATH", QCoreApplication::applicationFilePath());
        env.set("QT_CREATOR_PID", QString::number(QCoreApplication::applicationPid()));
    }

    Result<std::unique_ptr<TemporaryFilePath>> tempDir
        = TemporaryFilePath::create(*tmpDir / "shellintegration-XXXXXXXX", true);
    QTC_CHECK_RESULT(tempDir);
    if (!tempDir)
        return;

    m_tempDir = std::move(*tempDir);

    if (cmd.executable().baseName() == "bash") {
        const FilePath rcPath = filesToCopy.bash.rcFile;
        const FilePath tmpRc = m_tempDir->filePath() / filesToCopy.bash.rcFile.fileName();
        const Result<> copyResult = rcPath.copyFile(tmpRc);
        QTC_ASSERT_RESULT(copyResult, return);

        if (cmd.arguments() == "-l")
            env.set("VSCODE_SHELL_LOGIN", "1");

        cmd = {cmd.executable(), {"--init-file", tmpRc.nativePath()}};
    } else if (cmd.executable().baseName() == "zsh") {
        for (const FileToCopy &file : std::as_const(filesToCopy.zsh.files)) {
            const Result<> copyResult = file.source.copyFile(m_tempDir->filePath() / file.destName);
            QTC_ASSERT_RESULT(copyResult, return);
        }

        const Utils::FilePath originalZdotDir = FilePath::fromUserInput(
            env.value_or("ZDOTDIR", QDir::homePath()));

        env.set("ZDOTDIR", m_tempDir->filePath().nativePath());
        env.set("USER_ZDOTDIR", originalZdotDir.nativePath());
    } else if (cmd.executable().baseName() == "pwsh"
               || cmd.executable().baseName() == "powershell") {
        const FilePath rcPath = filesToCopy.pwsh.script;
        const FilePath tmpRc = m_tempDir->filePath() / filesToCopy.pwsh.script.fileName();
        const Result<> copyResult = rcPath.copyFile(tmpRc);
        QTC_ASSERT_RESULT(copyResult, return);

        // OPENMV-DIFF //
        // cmd.addArgs(QString("-noexit -command try { . '%1' } catch {Write-Host \"Shell "
        //                     "integration error:\" $_}")
        //                 .arg(tmpRc.nativePath()),
        //             CommandLine::Raw);
        // OPENMV-DIFF //
        // Windows' default execution policy (Restricted) refuses every .ps1 file,
        // this one included. The policy covers script files, not commands, so
        // load the script's text as a script block and dot-source that: the
        // integration loads, and the user's policy still applies to everything
        // they run in this shell. The command goes in as -EncodedCommand (base64
        // of UTF-16LE): it needs parentheses, and raw Windows arguments with
        // &()<>| get run through cmd.exe /c instead of starting PowerShell.
        const QString command
            = QString("try { . ([scriptblock]::Create([IO.File]::ReadAllText('%1'))) } "
                      "catch {Write-Host \"Shell integration error:\" $_}")
                  .arg(tmpRc.nativePath().replace('\'', "''"));
        const QByteArray utf16le(reinterpret_cast<const char *>(command.utf16()),
                                 command.size() * 2);
        cmd.addArgs(QString("-noexit -EncodedCommand %1")
                        .arg(QString::fromLatin1(utf16le.toBase64())),
                    CommandLine::Raw);
        // OPENMV-DIFF //
    } else if (cmd.executable().baseName() == "cmd") {
        const FilePath rcPath = filesToCopy.clink.script;
        const FilePath tmpRc = m_tempDir->filePath() / filesToCopy.clink.script.fileName();
        const Result<> copyResult = rcPath.copyFile(tmpRc);
        QTC_ASSERT_RESULT(copyResult, return);

        env.set("CLINK_HISTORY_LABEL", "QtCreator");
        env.appendOrSet("CLINK_PATH", tmpRc.parentDir().nativePath());

        if (cmd.executable().isLocal()) {
            const FilePath binPath = (m_tempDir->filePath() / "bin");
            if (binPath.ensureWritableDir()) {
                env.appendOrSetPath(binPath);
                const FilePath bat = binPath / "qtc.bat";

                const QString batContents = QString("@echo off\r\n\"%1\" -client -pid %2 %*\r\n")
                                                .arg(QCoreApplication::applicationFilePath())
                                                .arg(QCoreApplication::applicationPid());
                if (!bat.writeFileContents(batContents.toLocal8Bit())) {
                    qCWarning(integrationLog)
                        << "Failed to create qtc.bat file at" << bat.toUserOutput();
                }
            } else {
                qCWarning(integrationLog)
                    << "Failed to create Clink bin directory at" << binPath.toUserOutput();
            }
        }

    } else if (cmd.executable().baseName() == "fish") {
        FilePath xdgDir = m_tempDir->filePath() / "fish_xdg_data";
        FilePath subDir = xdgDir.resolvePath(QString("fish/vendor_conf.d"));
        QTC_ASSERT(subDir.createDir(), return);
        const Result<> copyResult = filesToCopy.fish.script.copyFile(
            subDir.resolvePath(filesToCopy.fish.script.fileName()));
        QTC_ASSERT_RESULT(copyResult, return);

        env.appendOrSet("XDG_DATA_DIRS", xdgDir.toUserOutput());
    }

    process.setCommand(cmd);
    process.setEnvironment(env);
}

void ShellIntegration::onSetClipboard(const QByteArray &text)
{
    setClipboardAndSelection(QString::fromLocal8Bit(text));
}

} // namespace Terminal
