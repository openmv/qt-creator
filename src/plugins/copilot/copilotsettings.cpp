// Copyright (C) 2023 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "copilotsettings.h"

#include "authwidget.h"
#include "copilotconstants.h"
#include "copilottr.h"

#include <coreplugin/coreconstants.h>
#include <coreplugin/dialogs/ioptionspage.h>
// OPENMV-DIFF //
#include <coreplugin/icore.h>
// OPENMV-DIFF //

#include <projectexplorer/project.h>

#include <utils/algorithm.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>

#include <QToolTip>
// OPENMV-DIFF //
#include <QLineEdit>
#include <QTimer>
#include <QUrl>
// OPENMV-DIFF //

using namespace Utils;

namespace Copilot {

static void initEnableAspect(BoolAspect &enableCopilot)
{
    enableCopilot.setSettingsKey(Constants::ENABLE_COPILOT);
    enableCopilot.setDisplayName(Tr::tr("Enable Copilot"));
    enableCopilot.setLabelText(Tr::tr("Enable Copilot"));
    enableCopilot.setToolTip(Tr::tr("Enables the Copilot integration."));
    // OPENMV-DIFF //
    // enableCopilot.setDefaultValue(false);
    // OPENMV-DIFF //
    enableCopilot.setDefaultValue(true);
    // OPENMV-DIFF //
}

CopilotSettings &settings()
{
    static CopilotSettings settings;
    return settings;
}

static const QString entryPointFileName = QStringLiteral("language-server.js");

CopilotSettings::CopilotSettings()
{
    setAutoApply(false);

    // OPENMV-DIFF //
    // const FilePath nodeFromPath = FilePath("node").searchInPath();
    // OPENMV-DIFF //
    FilePath nodeFromPath;

    if(Utils::HostOsInfo::isWindowsHost())
    {
        nodeFromPath = Core::ICore::resourcePath(QStringLiteral("node/windows/node.exe"));
    }
    else if(Utils::HostOsInfo::isMacHost())
    {
        if ((Utils::HostOsInfo::hostArchitecture() == Utils::OsArchArm64)
            || Utils::HostOsInfo::isRunningUnderRosetta())
        {
            nodeFromPath = Core::ICore::resourcePath(QStringLiteral("node/mac-aarch64/bin/node"));
        }
        else
        {
            nodeFromPath = Core::ICore::resourcePath(QStringLiteral("node/mac/bin/node"));
        }
    }
    else if(Utils::HostOsInfo::isLinuxHost())
    {
        if(QSysInfo::buildCpuArchitecture() == QStringLiteral("x86_64"))
        {
            nodeFromPath = Core::ICore::resourcePath(QStringLiteral("node/linux-x64/bin/node"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm"))
        {
            nodeFromPath = Core::ICore::resourcePath(QStringLiteral("node/linux-armv7/bin/node"));
        }
        else if(QSysInfo::buildCpuArchitecture() == QStringLiteral("arm64"))
        {
            nodeFromPath = Core::ICore::resourcePath(QStringLiteral("node/linux-arm64/bin/node"));
        }
    }
    // OPENMV-DIFF //

    // From: https://github.com/github/copilot.vim/blob/release/README.md#getting-started
    const QStringList subLocations
        = {"dist/agent.js",
           "copilot/dist/agent.js",
           "dist/language-server.js",
           "copilot-language-server/dist/language-server.js"};

    const QString baseDir = "pack/github/start/copilot.vim";

    const FilePaths locations = {
        // Vim, Linux/macOS:
        FilePath::fromUserInput("~/.vim") / baseDir,

        // Neovim, Linux/macOS:
        FilePath::fromUserInput("~/.config/nvim") / baseDir,

        // Vim, Windows (PowerShell command):
        FilePath::fromUserInput("~/vimfiles") / baseDir,

        // Neovim, Windows (PowerShell command):
        FilePath::fromUserInput("~/AppData/Local/nvim") / baseDir,
    };

    FilePaths searchDirs;
    // OPENMV-DIFF //
    // Prefer the copilot language-server bundled with OpenMV IDE.
    searchDirs.append(Core::ICore::resourcePath(QStringLiteral("copilot.vim/dist/language-server.js")));
    // OPENMV-DIFF //
    for (const auto &loc : locations) {
        for (const auto &subLocation : subLocations)
            searchDirs.append(loc / subLocation);
    }

    const FilePath distFromVim = findOrDefault(searchDirs, &FilePath::exists);

    nodeJsPath.setExpectedKind(PathChooser::ExistingCommand);
    nodeJsPath.setDefaultPathValue(nodeFromPath);
    nodeJsPath.setSettingsKey("Copilot.NodeJsPath");
    nodeJsPath.setLabelText(Tr::tr("Node.js path:"));
    nodeJsPath.setHistoryCompleter("Copilot.NodePath.History");
    nodeJsPath.setDisplayName(Tr::tr("Node.js Path"));
    //: %1 is the URL to nodejs
    nodeJsPath.setToolTip(Tr::tr("Select path to node.js executable. See %1 "
                                 "for installation instructions.")
                              .arg("https://nodejs.org/en/download/"));

    distPath.setExpectedKind(PathChooser::File);
    distPath.setDefaultPathValue(distFromVim);
    distPath.setSettingsKey("Copilot.DistPath");
    //: %1 is the filename of the copilot language server
    distPath.setLabelText(Tr::tr("Path to %1:").arg(entryPointFileName));
    distPath.setHistoryCompleter("Copilot.DistPath.History");
    //: %1 is the filename of the copilot language server
    distPath.setDisplayName(Tr::tr("Path to %1").arg(entryPointFileName));
    //: %1 is the URL to copilot.vim getting started, %2 is the filename of the copilot language server
    distPath.setToolTip(Tr::tr("Select path to %2 in Copilot Neovim plugin. See "
                               "%1 for installation instructions.")
                            .arg("https://github.com/github/copilot.vim#getting-started")
                            .arg(entryPointFileName));

    autoComplete.setDisplayName(Tr::tr("Auto Request"));
    autoComplete.setSettingsKey("Copilot.Autocomplete");
    autoComplete.setLabelText(Tr::tr("Auto request"));
    autoComplete.setDefaultValue(true);
    autoComplete.setToolTip(Tr::tr("Automatically request suggestions for the current text cursor "
                                   "position after changes to the document."));

    proxy.setDisplayName(Tr::tr("Proxy"));
    proxy.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);
    proxy.setSettingsKey("Copilot.Proxy");
    proxy.setLabelText(Tr::tr("Proxy:"));
    proxy.setDefaultValue("");
    // OPENMV-DIFF //
    // proxy.setPlaceHolderText("http://localhost:3128");
    // proxy.setToolTip(Tr::tr("The proxy server to use for connections."));
    // OPENMV-DIFF //
    // Show the whole format, user name and password included.
    proxy.setPlaceHolderText("http://user:password@host:port");
    proxy.setToolTip(Tr::tr("The proxy server to use for connections, as "
                            "http://host:port, or http://user:password@host:port if the proxy "
                            "needs a login."));
    // OPENMV-DIFF //

    proxyRejectUnauthorized.setDisplayName(Tr::tr("Reject Unauthorized"));
    proxyRejectUnauthorized.setSettingsKey("Copilot.ProxyRejectUnauthorized");
    proxyRejectUnauthorized.setLabelText(Tr::tr("Reject unauthorized"));
    proxyRejectUnauthorized.setDefaultValue(true);
    proxyRejectUnauthorized.setToolTip(Tr::tr("Reject unauthorized certificates from the proxy "
                                              "server. Turning this off is a security risk."));

    githubEnterpriseUrl.setDisplayName(Tr::tr("GitHub Enterprise URL"));
    githubEnterpriseUrl.setDisplayStyle(StringAspect::DisplayStyle::LineEditDisplay);
    // OPENMV-DIFF //
    // githubEnterpriseUrl.setSettingsKey("Copilot.GithubEnterpriseUrl");
    // OPENMV-DIFF //
    // Hidden and always empty (github.com): no settings key, so a stored URL is
    // never loaded. Only for companies running Copilot through their own GitHub.
    // OPENMV-DIFF //
    githubEnterpriseUrl.setLabelText(Tr::tr("GitHub Enterprise URL:"));
    githubEnterpriseUrl.setDefaultValue("");
    githubEnterpriseUrl.setToolTip(Tr::tr("The URL of your GitHub Enterprise server."));

    initEnableAspect(enableCopilot);

    readSettings();

    // OPENMV-DIFF //
    // The OpenMV IDE's Qt Creator 14 base had separate proxy fields; this one URL
    // replaced them. Carry an enabled proxy over once (the old password was stored
    // in plain text in the settings), then drop the old keys so clearing the new
    // field later doesn't bring it back.
    {
        QtcSettings *s = Core::ICore::settings();
        const QString host = s->value("Copilot.ProxyHost").toString();
        if (proxy().isEmpty() && s->value("Copilot.UseProxy").toBool() && !host.isEmpty()) {
            QUrl url;
            url.setScheme("http");
            url.setHost(host);
            url.setPort(s->value("Copilot.ProxyPort", 3128).toInt());
            url.setUserName(s->value("Copilot.ProxyUser").toString());
            url.setPassword(s->value("Copilot.ProxyPassword").toString());
            proxy.setValue(url.toString());
            writeSettings();
        }
        for (const Utils::Key &key :
             {Utils::Key("Copilot.UseProxy"), Utils::Key("Copilot.ProxyHost"),
              Utils::Key("Copilot.ProxyPort"), Utils::Key("Copilot.ProxyUser"),
              Utils::Key("Copilot.SaveProxyPassword"), Utils::Key("Copilot.ProxyPassword")}) {
            s->remove(key);
        }
    }
    // OPENMV-DIFF //

    // TODO: As a workaround we set the enabler after reading the settings, as that does not signal
    // a change.
    nodeJsPath.setEnabler(&enableCopilot);
    distPath.setEnabler(&enableCopilot);
    autoComplete.setEnabler(&enableCopilot);
    githubEnterpriseUrl.setEnabler(&enableCopilot);

    proxy.setEnabler(&enableCopilot);
    // OPENMV-DIFF //
    // proxyRejectUnauthorized.setEnabler(&enableCopilot);
    // OPENMV-DIFF //
    // Only meaningful with a proxy: greyed out while the Proxy field is empty.
    const auto updateRejectUnauthorized = [this] {
        proxyRejectUnauthorized.setEnabled(enableCopilot.volatileValue()
                                           && !proxy.volatileValue().trimmed().isEmpty());
    };
    connect(&enableCopilot, &BaseAspect::volatileValueChanged, this, updateRejectUnauthorized);
    connect(&proxy, &BaseAspect::volatileValueChanged, this, updateRejectUnauthorized);
    connect(&enableCopilot, &BaseAspect::changed, this, updateRejectUnauthorized);
    connect(&proxy, &BaseAspect::changed, this, updateRejectUnauthorized);
    updateRejectUnauthorized();
    // OPENMV-DIFF //

    setLayouter([this] {
        using namespace Layouting;

        // clang-format off

        Label warningLabel {
            wordWrap(true),
            textInteractionFlags(
                Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard | Qt::TextSelectableByMouse),
            text(Tr::tr("Enabling %1 is subject to your agreement and abidance with your applicable "
                 "%1 terms. It is your responsibility to know and accept the requirements and "
                 "parameters of using tools like %1. This may include, but is not limited to, "
                 "ensuring you have the rights to allow %1 access to your code, as well as "
                 "understanding any implications of your use of %1 and suggestions produced "
                 "(like copyright, accuracy, etc.).")
                       .arg("Copilot")),
        };

        Label helpLabel {
            textFormat(Qt::MarkdownText),
            wordWrap(true),
            textInteractionFlags(
                Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard | Qt::TextSelectableByMouse),
            openExternalLinks(true),
            onLinkHovered(this, [](const QString &link) { QToolTip::showText(QCursor::pos(), link); }),
            text(Tr::tr(
                "The Copilot plugin requires node.js and the Copilot neovim plugin. "
                "If you install the neovim plugin as described in %1, "
                "the plugin will find the %3 file automatically.\n\n"
                "Otherwise you need to specify the path to the %2 "
                "file from the Copilot neovim plugin.",
                "Markdown text for the copilot instruction label")
                       .arg("[README.md](https://github.com/github/copilot.vim)")
                       .arg("[language-server.js](https://github.com/github/copilot.vim/tree/release/copilot-language-server/dist)")
                       .arg(entryPointFileName)),
        };

        return Column {
            Group {
                title(Tr::tr("Note")),
                Column {
                    warningLabel, br,
                    // OPENMV-DIFF //
                    // helpLabel, br,
                    // OPENMV-DIFF //
                }
            },
            // OPENMV-DIFF //
            // No variable chooser on this page's text fields (StringAspect always adds
            // one): once the page is built, clear the property it attaches by.
            [] {
                auto marker = new QWidget;
                marker->hide();
                QTimer::singleShot(0, marker, [marker] {
                    if (QWidget *page = marker->parentWidget()) {
                        for (QLineEdit *edit : page->findChildren<QLineEdit *>())
                            edit->setProperty("QtCreator.VariableSupport", QVariant());
                    }
                });
                return marker;
            }(),
            // OPENMV-DIFF //
            Form {
                new AuthWidget, br,
                enableCopilot, br,
                // OPENMV-DIFF //
                // OpenMV auto-configures the bundled node binary and language-server, so
                // these are not shown in the UI.
                // nodeJsPath, br,
                // distPath, br,
                // autoComplete, br,
                // OPENMV-DIFF //
                // OPENMV-DIFF //
                // githubEnterpriseUrl, br,
                // hr, br,
                // OPENMV-DIFF //
                proxy, br,
                proxyRejectUnauthorized, br,
            },
            st
        };
        // clang-format on
    });
}

CopilotProjectSettings::CopilotProjectSettings(ProjectExplorer::Project *project)
{
    setAutoApply(true);

    useGlobalSettings.setSettingsKey(Constants::COPILOT_USE_GLOBAL_SETTINGS);
    useGlobalSettings.setDefaultValue(true);

    initEnableAspect(enableCopilot);

    Store map = storeFromVariant(project->namedSettings(Constants::COPILOT_PROJECT_SETTINGS_ID));
    fromMap(map);

    enableCopilot.addOnChanged(this, [this, project] { save(project); });
    useGlobalSettings.addOnChanged(this, [this, project] { save(project); });
}

void CopilotProjectSettings::setUseGlobalSettings(bool useGlobal)
{
    useGlobalSettings.setValue(useGlobal);
}

bool CopilotProjectSettings::isEnabled() const
{
    if (useGlobalSettings())
        return settings().enableCopilot();
    return enableCopilot();
}

void CopilotProjectSettings::save(ProjectExplorer::Project *project)
{
    Store map;
    toMap(map);
    project->setNamedSettings(Constants::COPILOT_PROJECT_SETTINGS_ID, variantFromStore(map));

    // This triggers a restart of the Copilot language server.
    settings().apply();
}

class CopilotSettingsPage : public Core::IOptionsPage
{
public:
    CopilotSettingsPage()
    {
        setId(Constants::COPILOT_GENERAL_OPTIONS_ID);
        setDisplayName("Copilot");
        setCategory(Core::Constants::SETTINGS_CATEGORY_AI);
        setSettingsProvider([] { return &settings(); });
    }
};

const CopilotSettingsPage settingsPage;

} // namespace Copilot
