// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "shellmodel.h"
// OPENMV-DIFF //
#include "terminalsettings.h"
// OPENMV-DIFF //

#include <projectexplorer/devicesupport/devicemanager.h>
#include <projectexplorer/projectexplorerconstants.h>

#include <utils/algorithm.h>
#include <utils/environment.h>
#include <utils/filepath.h>

#include <QFileIconProvider>
#include <QStandardPaths>

namespace Terminal::Internal {

using namespace Utils;

struct ShellItemBuilder
{
    explicit ShellItemBuilder(const QFileIconProvider &iconProvider, const CommandLine &value) :
        iconProvider(iconProvider)
    {
        m_item.openParameters.shellCommand = value;
    }
    explicit ShellItemBuilder(const QFileIconProvider &iconProvider, const FilePath &value) :
        iconProvider(iconProvider)
    {
        m_item.openParameters.shellCommand = CommandLine(value);
    }
    ShellItemBuilder &name(const QString &value)
    {
        m_item.name = value;
        return *this;
    }
    ShellItemBuilder &icon(const FilePath &value)
    {
        m_item.openParameters.icon = iconProvider.icon(value.toFileInfo());
        return *this;
    }

    inline operator ShellModelItem() { return item(); }
    ShellModelItem item()
    {
        if (m_item.name.isEmpty())
            m_item.name = m_item.openParameters.shellCommand->executable().toUserOutput();
        if (m_item.openParameters.icon.isNull())
            icon(m_item.openParameters.shellCommand->executable());
        return m_item;
    }

private:
    ShellModelItem m_item;
    const QFileIconProvider &iconProvider;
};

static QSet<FilePath> msysPaths()
{
    QSet<FilePath> res;
    FilePath msys2 = FilePath::fromUserInput(QStandardPaths::findExecutable("msys2.exe"));
    if (msys2.exists())
        res.insert(msys2.parentDir());
    for (const QFileInfo &drive : QDir::drives()) {
        for (const QString &name : QStringList{"msys2", "msys32", "msys64"}) {
            msys2 = FilePath::fromString(drive.filePath()).pathAppended(name);
            if (msys2.pathAppended("msys2.exe").exists())
                res.insert(msys2);
        }
    }
    return res;
}

struct ShellModelPrivate
{
    QList<ShellModelItem> localShells;
    ShellModelPrivate();
};

ShellModelPrivate::ShellModelPrivate()
{
    QFileIconProvider iconProvider;
    if (Utils::HostOsInfo::isWindowsHost()) {
        // OPENMV-DIFF //
        // const FilePath comspec = FilePath::fromUserInput(qtcEnvironmentVariable("COMSPEC"));
        // localShells << ShellItemBuilder(iconProvider, comspec);

        // if (comspec.fileName() != "cmd.exe") {
        //     FilePath cmd = FilePath::fromUserInput(QStandardPaths::findExecutable("cmd.exe"));
        //     localShells << ShellItemBuilder(iconProvider, cmd);
        // }

        // const FilePath powershell = FilePath::fromUserInput(
        //     QStandardPaths::findExecutable("powershell.exe"));

        // if (powershell.exists())
        //     localShells << ShellItemBuilder(iconProvider, powershell);

        // const FilePath sys_bash =
        //         FilePath::fromUserInput(QStandardPaths::findExecutable("bash.exe"));
        // if (sys_bash.exists())
        //     localShells << ShellItemBuilder(iconProvider, {sys_bash, {"--login"}});

        // const FilePath git_exe = FilePath::fromUserInput(QStandardPaths::findExecutable("git.exe"));
        // if (git_exe.exists()) {
        //     FilePath git_bash = git_exe.parentDir().parentDir().pathAppended("bin/bash.exe");
        //     if (git_bash.exists()) {
        //         localShells << ShellItemBuilder(iconProvider, {git_bash, {"--login"}})
        //                   .icon(git_bash.parentDir().parentDir().pathAppended("git-bash.exe"));
        //     }
        // }
        // OPENMV-DIFF //
        // Friendly names ("Git Bash", "WSL Bash", ...) instead of executable paths;
        // WSL only when a distribution is installed (its launcher exists without
        // one and then just fails), with WSL's own icon; Git Bash also when git
        // isn't on PATH; no second entry when bash.exe on PATH is Git Bash.
        const auto named = [&iconProvider](const CommandLine &command) {
            ShellItemBuilder builder(iconProvider, command);
            const QString name = friendlyShellName(command.executable());
            if (!name.isEmpty())
                builder.name(name);
            builder.icon(shellIconFile(command.executable()));
            return builder;
        };

        // Best first, the same order the default shell is picked in.
        const FilePath git_bash = gitBashPath();
        if (git_bash.exists())
            localShells << named({git_bash, {"-l"}}); // "-l", not "--login": shell integration

        const FilePath pwsh = powerShell7Path();
        if (pwsh.exists())
            localShells << named(CommandLine(pwsh));

        const FilePath powershell = FilePath::fromUserInput(
            QStandardPaths::findExecutable("powershell.exe"));
        if (powershell.exists())
            localShells << named(CommandLine(powershell));

        const FilePath comspec = FilePath::fromUserInput(qtcEnvironmentVariable("COMSPEC"));
        localShells << named(CommandLine(comspec));

        if (comspec.fileName() != "cmd.exe") {
            FilePath cmd = FilePath::fromUserInput(QStandardPaths::findExecutable("cmd.exe"));
            localShells << named(CommandLine(cmd));
        }

        const FilePath sys_bash =
                FilePath::fromUserInput(QStandardPaths::findExecutable("bash.exe"));
        if (sys_bash.exists() && sys_bash != git_bash) {
            if (isWslLauncher(sys_bash)) {
                if (wslHasDistribution()) {
                    localShells << named({sys_bash, {"--login"}});
                }
            } else {
                localShells << named({sys_bash, {"--login"}});
            }
        }
        // OPENMV-DIFF //

        const QStringList msys2_args{"-defterm", "-no-start", "-here"};
        for (const FilePath &msys2_path : msysPaths()) {
            const FilePath msys2_cmd = msys2_path.pathAppended("msys2_shell.cmd");
            for (const FilePath &type : msys2_path.dirEntries({{"*.ico"}})) {
                // Only list this type if it has anything in /bin
                QDirIterator it(type.path().replace(".ico", "/bin"), QDir::Files);
                if (!it.hasNext())
                    continue;
                localShells << ShellItemBuilder(iconProvider,
                              {msys2_cmd, msys2_args + QStringList{"-" + type.baseName()}})
                          .icon(type)
                          .name(type.toUserOutput().replace(".ico", ".exe"));
            }
        }
    } else {
        FilePath shellsFile = FilePath::fromString("/etc/shells");
        const auto shellFileContent = shellsFile.fileContents();
        QTC_ASSERT_RESULT(shellFileContent, return);

        QString shellFileContentString = QString::fromUtf8(*shellFileContent);

        // Filter out comments ...
        const QStringList lines
            = Utils::filtered(shellFileContentString.split('\n', Qt::SkipEmptyParts),
                              [](const QString &line) { return !line.trimmed().startsWith('#'); });

        // Convert lines to file paths ...
        const FilePaths shells = Utils::transform(lines, [](const QString &line) {
            return FilePath::fromUserInput(line.trimmed());
        });

        // ... and filter out non-existing shells.
        localShells = Utils::transform(Utils::filtered(shells, &FilePath::exists),
                                       [&iconProvider](const FilePath &shell) {
                                           return ShellItemBuilder(iconProvider, shell).item();
                                       });
    }
}

ShellModel::ShellModel(QObject *parent)
    : QObject(parent)
    , d(new ShellModelPrivate())
{
}

ShellModel::~ShellModel() = default;

QList<ShellModelItem> ShellModel::local() const
{
    return d->localShells;
}

QList<ShellModelItem> ShellModel::remote() const
{
    QList<ShellModelItem> result;

    ProjectExplorer::DeviceManager::forEachDevice(
        [&result](const std::shared_ptr<const ProjectExplorer::IDevice> &device) {
            if (device->type() != ProjectExplorer::Constants::DESKTOP_DEVICE_TYPE)
                result << ShellModelItem{device->displayName(), {CommandLine{device->rootPath()}}};
        });

    return result;
}

// OPENMV-DIFF //
ShellModel &shellModel()
{
    static ShellModel model;
    return model;
}
// OPENMV-DIFF //

} // namespace Terminal::Internal
