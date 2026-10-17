// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#include "terminalsettings.h"

// OPENMV-DIFF //
#include "shellmodel.h"
// OPENMV-DIFF //

#include "terminalicons.h"
#include "terminaltr.h"

#include <coreplugin/icore.h>
#include <coreplugin/dialogs/ioptionspage.h>

#include <utils/dropsupport.h>
#include <utils/environment.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/pathchooser.h>
#include <utils/shutdownguard.h>
#include <utils/stringutils.h>
#include <utils/theme/theme.h>

// OPENMV-DIFF //
#include <QSettings>
#include <QStandardItem>
#include <QStandardPaths>
// OPENMV-DIFF //
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QXmlStreamReader>

Q_LOGGING_CATEGORY(schemeLog, "qtc.terminal.scheme", QtWarningMsg)

using namespace Utils;

namespace Terminal {

static QString defaultFontFamily()
{
    if (HostOsInfo::isMacHost())
        return QLatin1String("Menlo");

    if (Utils::HostOsInfo::isAnyUnixHost())
        return QLatin1String("Monospace");

    return QLatin1String("Consolas");
}

static int defaultFontSize()
{
    if (Utils::HostOsInfo::isMacHost())
        return 12;
    if (Utils::HostOsInfo::isAnyUnixHost())
        return 9;
    return 10;
}

// OPENMV-DIFF //
// static QString defaultShell()
// {
//     if (HostOsInfo::isWindowsHost())
//         return qtcEnvironmentVariable("COMSPEC");
//
//     FilePath defaultShell = FilePath::fromUserInput(qtcEnvironmentVariable("SHELL"));
//     if (defaultShell.isExecutableFile())
//         return defaultShell.toUserOutput();
//
//     return Environment::systemEnvironment().searchInPath("sh").toUserOutput();
// }
// OPENMV-DIFF //
// Git Bash: next to git.exe on PATH, else in Git for Windows' install folders.
FilePath gitBashPath()
{
    const FilePath gitExe = FilePath::fromUserInput(QStandardPaths::findExecutable("git.exe"));
    if (!gitExe.isEmpty()) {
        const FilePath bash = gitExe.parentDir().parentDir().pathAppended("bin/bash.exe");
        if (bash.isExecutableFile())
            return bash;
    }
    for (const QString &root : {QString("ProgramFiles"), QString("ProgramFiles(x86)"),
                                QString("ProgramW6432")}) {
        const QString dir = qtcEnvironmentVariable(root);
        if (dir.isEmpty())
            continue;
        const FilePath bash = FilePath::fromUserInput(dir).pathAppended("Git/bin/bash.exe");
        if (bash.isExecutableFile())
            return bash;
    }
    const QString localAppData = qtcEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty()) {
        const FilePath bash
            = FilePath::fromUserInput(localAppData).pathAppended("Programs/Git/bin/bash.exe");
        if (bash.isExecutableFile())
            return bash;
    }
    return {};
}

// PowerShell 7 (pwsh), on PATH or in its default install folder.
FilePath powerShell7Path()
{
    const FilePath pwsh = FilePath::fromUserInput(QStandardPaths::findExecutable("pwsh.exe"));
    if (!pwsh.isEmpty())
        return pwsh;
    const QString dir = qtcEnvironmentVariable("ProgramFiles");
    if (!dir.isEmpty()) {
        const FilePath candidate = FilePath::fromUserInput(dir).pathAppended("PowerShell/7/pwsh.exe");
        if (candidate.isExecutableFile())
            return candidate;
    }
    return {};
}

// WSL's launchers live in the Windows system folder; Git Bash, MSYS2 or Cygwin
// bash.exe live elsewhere.
bool isWslLauncher(const FilePath &shell)
{
    if (!HostOsInfo::isWindowsHost())
        return false;
    const FilePath system32
        = FilePath::fromUserInput(qtcEnvironmentVariable("SystemRoot")).pathAppended("System32");
    const QString name = shell.fileName().toLower();
    return (name == "bash.exe" || name == "wsl.exe")
           && shell.parentDir().toFSPathString().compare(system32.toFSPathString(),
                                                          Qt::CaseInsensitive) == 0;
}

// The WSL launchers exist whenever the WSL feature is on, even with no Linux
// distribution installed (then they just fail). Installed distributions are
// registered here; reading it is instant, unlike running "wsl --list".
bool wslHasDistribution()
{
    if (!HostOsInfo::isWindowsHost())
        return false;
    QSettings lxss("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Lxss",
                   QSettings::NativeFormat);
    return !lxss.childGroups().isEmpty();
}

QString friendlyShellName(const FilePath &shell)
{
    if (!HostOsInfo::isWindowsHost())
        return {};
    if (isWslLauncher(shell))
        return Tr::tr("WSL Bash");
    const QString name = shell.fileName().toLower();
    if (name == "bash.exe" && shell == gitBashPath())
        return Tr::tr("Git Bash");
    if (name == "pwsh.exe")
        return Tr::tr("PowerShell 7");
    if (name == "powershell.exe")
        return Tr::tr("Windows PowerShell");
    if (name == "cmd.exe")
        return Tr::tr("Command Prompt");
    return {};
}

// Git Bash's and WSL's bash.exe have a generic console icon; their launchers
// (git-bash.exe, wsl.exe) carry the real ones.
FilePath shellIconFile(const FilePath &shell)
{
    if (!HostOsInfo::isWindowsHost())
        return shell;
    if (isWslLauncher(shell)) {
        const FilePath wsl = shell.parentDir().pathAppended("wsl.exe");
        if (wsl.exists())
            return wsl;
    } else if (shell.fileName().toLower() == "bash.exe" && shell == gitBashPath()) {
        const FilePath gitBashExe = shell.parentDir().parentDir().pathAppended("git-bash.exe");
        if (gitBashExe.exists())
            return gitBashExe;
    }
    return shell;
}

// A stable id for a shell, saved by the "Default shell" setting: not the
// display name (translated) and, for the common shells, not the path (moves on
// reinstall).
static QString shellKey(const CommandLine &command)
{
    const FilePath exe = command.executable();
    if (isWslLauncher(exe))
        return QString("wsl");
    if (HostOsInfo::isWindowsHost()) {
        if (exe == gitBashPath())
            return QString("git-bash");
        const QString base = exe.baseName().toLower();
        if (base == "pwsh" || base == "powershell" || base == "cmd")
            return base;
    }
    return QString(exe.toUserOutput() + ' ' + command.arguments()).trimmed();
}

// The best shell available, with the arguments it needs:
//   Windows: Git Bash, PowerShell 7, Windows PowerShell, then cmd (%COMSPEC%).
//            (Not a bare bash.exe on PATH: that is usually WSL's launcher.)
//   macOS/Linux: the user's login shell ($SHELL), then bash, then sh; as a login shell.
static std::pair<QString, QString> defaultShellAndArguments()
{
    if (HostOsInfo::isWindowsHost()) {
        if (const FilePath bash = gitBashPath(); !bash.isEmpty())
            return {bash.toUserOutput(), QString("-l")}; // "-l": shell integration needs it
        if (const FilePath pwsh = powerShell7Path(); !pwsh.isEmpty())
            return {pwsh.toUserOutput(), QString()};
        const FilePath powershell
            = FilePath::fromUserInput(QStandardPaths::findExecutable("powershell.exe"));
        if (!powershell.isEmpty())
            return {powershell.toUserOutput(), QString()};
        return {qtcEnvironmentVariable("COMSPEC"), QString()};
    }

    FilePath defaultShell = FilePath::fromUserInput(qtcEnvironmentVariable("SHELL"));
    if (defaultShell.isExecutableFile())
        return {defaultShell.toUserOutput(), QString("-l")};

    const FilePath bash = Environment::systemEnvironment().searchInPath("bash");
    if (!bash.isEmpty())
        return {bash.toUserOutput(), QString("-l")};

    return {Environment::systemEnvironment().searchInPath("sh").toUserOutput(), QString("-l")};
}
// OPENMV-DIFF //

void setupColor(TerminalSettings *settings,
                ColorAspect &color,
                const QString &label,
                const QColor &defaultColor,
                const QString &humanReadableName = {})
{
    // OPENMV-DIFF //
    // color.setSettingsKey(keyFromString(label));
    // OPENMV-DIFF //
    // The colors always follow the IDE theme (Flat Dark / Flat Light): no settings
    // keys, so a stored or imported palette is never loaded, and no Colors group.
    // OPENMV-DIFF //
    color.setDefaultValue(defaultColor);
    color.setToolTip(Tr::tr("The color used for %1.")
                         .arg(humanReadableName.isEmpty() ? label : humanReadableName));
    color.setWithResetButton(false);
    color.setMinimumSize({0, 0});
    settings->registerAspect(&color);
}

static Result<> loadXdefaults(const FilePath &path)
{
    const Result<QByteArray> readResult = path.fileContents();
    if (!readResult)
        return make_unexpected(readResult.error());

    static const QRegularExpression re(R"(.*\*(color[0-9]{1,2}|foreground|background):\s*(#[0-9a-f]{6}))");

    for (const QByteArray &line : readResult->split('\n')) {
        if (line.trimmed().startsWith('!'))
            continue;

        const auto match = re.match(QString::fromUtf8(line));
        if (match.hasMatch()) {
            const QString colorName = match.captured(1);
            const QColor color(match.captured(2));
            if (colorName == "foreground") {
                settings().foregroundColor.setVolatileValue(color);
            } else if (colorName == "background") {
                settings().backgroundColor.setVolatileValue(color);
            } else {
                const int colorIndex = colorName.mid(5).toInt();
                if (colorIndex >= 0 && colorIndex < 16)
                    settings().colors[colorIndex].setVolatileValue(color);
            }
        }
    }

    return {};
}

static Result<> loadItermColors(const FilePath &path)
{
    QFile f(path.toFSPathString());
    const bool opened = f.open(QIODevice::ReadOnly);
    if (!opened)
        return make_unexpected(Tr::tr("Failed to open file."));

    QXmlStreamReader reader(&f);
    while (!reader.atEnd() && reader.readNextStartElement()) {
        if (reader.name() == u"plist") {
            while (!reader.atEnd() && reader.readNextStartElement()) {
                if (reader.name() == u"dict") {
                    QString colorName;
                    while (!reader.atEnd() && reader.readNextStartElement()) {
                        if (reader.name() == u"key") {
                            colorName = reader.readElementText();
                        } else if (reader.name() == u"dict") {
                            QColor color;
                            int component = 0;
                            while (!reader.atEnd() && reader.readNextStartElement()) {
                                if (reader.name() == u"key") {
                                    const auto &text = reader.readElementText();
                                    if (text == u"Red Component")
                                        component = 0;
                                    else if (text == u"Green Component")
                                        component = 1;
                                    else if (text == u"Blue Component")
                                        component = 2;
                                    else if (text == u"Alpha Component")
                                        component = 3;
                                } else if (reader.name() == u"real") {
                                    // clang-format off
                                    switch (component) {
                                        case 0: color.setRedF(reader.readElementText().toDouble()); break;
                                        case 1: color.setGreenF(reader.readElementText().toDouble()); break;
                                        case 2: color.setBlueF(reader.readElementText().toDouble()); break;
                                        case 3: color.setAlphaF(reader.readElementText().toDouble()); break;
                                    }
                                    // clang-format on
                                } else {
                                    reader.skipCurrentElement();
                                }
                            }

                            if (colorName.startsWith("Ansi")) {
                                const auto c = colorName.mid(5, 2);
                                const int colorIndex = c.toInt();
                                if (colorIndex >= 0 && colorIndex < 16)
                                    settings().colors[colorIndex].setVolatileValue(
                                        color);
                            } else if (colorName == "Foreground Color") {
                                settings().foregroundColor.setVolatileValue(color);
                            } else if (colorName == "Background Color") {
                                settings().backgroundColor.setVolatileValue(color);
                            } else if (colorName == "Selection Color") {
                                settings().selectionColor.setVolatileValue(color);
                            }
                        }
                    }
                }
            }
            break;
        }
    }
    if (reader.hasError())
        return make_unexpected(reader.errorString());

    return {};
}

static Result<> loadWindowsTerminalColors(const FilePath &path)
{
    const Result<QByteArray> readResult = path.fileContents();
    if (!readResult)
        return make_unexpected(readResult.error());

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(*readResult, &error);
    if (error.error != QJsonParseError::NoError)
        return make_unexpected(Tr::tr("JSON parsing error: \"%1\", at offset: %2")
                                   .arg(error.errorString())
                                   .arg(error.offset));

    const QJsonObject colors = doc.object();

    // clang-format off
    const QList<QPair<QStringView, ColorAspect *>> colorKeys = {
        qMakePair(u"background", &settings().backgroundColor),
        qMakePair(u"foreground", &settings().foregroundColor),
        qMakePair(u"selectionBackground", &settings().selectionColor),

        qMakePair(u"black", &settings().colors[0]),
        qMakePair(u"brightBlack", &settings().colors[8]),

        qMakePair(u"red", &settings().colors[1]),
        qMakePair(u"brightRed", &settings().colors[9]),

        qMakePair(u"green", &settings().colors[2]),
        qMakePair(u"brightGreen", &settings().colors[10]),

        qMakePair(u"yellow", &settings().colors[3]),
        qMakePair(u"brightYellow", &settings().colors[11]),

        qMakePair(u"blue", &settings().colors[4]),
        qMakePair(u"brightBlue", &settings().colors[12]),

        qMakePair(u"magenta", &settings().colors[5]),
        qMakePair(u"brightMagenta", &settings().colors[13]),

        qMakePair(u"cyan", &settings().colors[6]),
        qMakePair(u"brightCyan", &settings().colors[14]),

        qMakePair(u"white", &settings().colors[7]),
        qMakePair(u"brightWhite", &settings().colors[15])
    };
    // clang-format on

    for (const auto &pair : colorKeys) {
        const auto it = colors.find(pair.first);
        if (it != colors.end()) {
            const QString colorString = it->toString();
            if (colorString.startsWith("#")) {
                QColor color(colorString.mid(0, 7));
                if (colorString.size() > 7) {
                    int alpha = colorString.mid(7).toInt(nullptr, 16);
                    color.setAlpha(alpha);
                }
                if (color.isValid())
                    pair.second->setVolatileValue(color);
            }
        }
    }

    return {};
}

static Result<> loadVsCodeColors(const FilePath &path)
{
    const Result<QByteArray> readResult = path.fileContents();
    if (!readResult)
        return make_unexpected(readResult.error());

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(*readResult, &error);
    if (error.error != QJsonParseError::NoError)
        return make_unexpected(Tr::tr("JSON parsing error: \"%1\", at offset: %2")
                                   .arg(error.errorString())
                                   .arg(error.offset));

    const QJsonObject root = doc.object();
    const auto itColors = root.find("colors");
    if (itColors == root.end())
        return make_unexpected(Tr::tr("No colors found."));

    const QJsonObject colors = itColors->toObject();

    // clang-format off
    const QList<QPair<QStringView, ColorAspect *>> colorKeys = {
        qMakePair(u"editor.background", &settings().backgroundColor),
        qMakePair(u"terminal.foreground", &settings().foregroundColor),
        qMakePair(u"terminal.selectionBackground", &settings().selectionColor),

        qMakePair(u"terminal.ansiBlack", &settings().colors[0]),
        qMakePair(u"terminal.ansiBrightBlack", &settings().colors[8]),

        qMakePair(u"terminal.ansiRed", &settings().colors[1]),
        qMakePair(u"terminal.ansiBrightRed", &settings().colors[9]),

        qMakePair(u"terminal.ansiGreen", &settings().colors[2]),
        qMakePair(u"terminal.ansiBrightGreen", &settings().colors[10]),

        qMakePair(u"terminal.ansiYellow", &settings().colors[3]),
        qMakePair(u"terminal.ansiBrightYellow", &settings().colors[11]),

        qMakePair(u"terminal.ansiBlue", &settings().colors[4]),
        qMakePair(u"terminal.ansiBrightBlue", &settings().colors[12]),

        qMakePair(u"terminal.ansiMagenta", &settings().colors[5]),
        qMakePair(u"terminal.ansiBrightMagenta", &settings().colors[13]),

        qMakePair(u"terminal.ansiCyan", &settings().colors[6]),
        qMakePair(u"terminal.ansiBrightCyan", &settings().colors[14]),

        qMakePair(u"terminal.ansiWhite", &settings().colors[7]),
        qMakePair(u"terminal.ansiBrightWhite", &settings().colors[15])
    };
    // clang-format on

    for (const auto &pair : colorKeys) {
        const auto it = colors.find(pair.first);
        if (it != colors.end()) {
            const QString colorString = it->toString();
            if (colorString.startsWith("#")) {
                QColor color(colorString.mid(0, 7));
                if (colorString.size() > 7) {
                    int alpha = colorString.mid(7).toInt(nullptr, 16);
                    color.setAlpha(alpha);
                }
                if (color.isValid())
                    pair.second->setVolatileValue(color);
            }
        }
    }

    return {};
}

static Result<> loadKonsoleColorScheme(const FilePath &path)
{
    auto parseColor = [](const QStringList &parts) -> Result<QColor> {
        if (parts.size() != 3 && parts.size() != 4)
            return make_unexpected(Tr::tr("Invalid color format."));
        int alpha = parts.size() == 4 ? parts[3].toInt() : 255;
        return QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt(), alpha);
    };

    // clang-format off
    TerminalSettings &s = settings();
    const QPair<QString, ColorAspect *> colorKeys[] = {
        { "Background/Color", &s.backgroundColor },
        { "Foreground/Color", &s.foregroundColor},

        { "Color0/Color", &s.colors[0] },
        { "Color0Intense/Color", &s.colors[8] },

        { "Color1/Color", &s.colors[1] },
        { "Color1Intense/Color", &s.colors[9] },

        { "Color2/Color", &s.colors[2] },
        { "Color2Intense/Color", &s.colors[10] },

        { "Color3/Color", &s.colors[3] },
        { "Color3Intense/Color", &s.colors[11] },

        { "Color4/Color", &s.colors[4] },
        { "Color4Intense/Color", &s.colors[12] },

        { "Color5/Color", &s.colors[5] },
        { "Color5Intense/Color", &s.colors[13] },

        { "Color6/Color", &s.colors[6] },
        { "Color6Intense/Color", &s.colors[14] },

        { "Color7/Color", &s.colors[7] },
        { "Color7Intense/Color", &s.colors[15] }
    };
    // clang-format on

    QSettings ini(path.toFSPathString(), QSettings::IniFormat);
    for (const auto &colorKey : colorKeys) {
        if (ini.contains(colorKey.first)) {
            const auto color = parseColor(ini.value(colorKey.first).toStringList());
            if (!color)
                return make_unexpected(color.error());

            colorKey.second->setVolatileValue(*color);
        }
    }

    return {};
}

static Result<> loadXFCE4ColorScheme(const FilePath &path)
{
    Result<QByteArray> arr = path.fileContents();
    if (!arr)
        return make_unexpected(arr.error());

    arr->replace(';', ',');

    QTemporaryFile f;
    if (!f.open())
        return make_unexpected(f.errorString());
    f.write(*arr);
    f.close();

    QSettings ini(f.fileName(), QSettings::IniFormat);
    TerminalSettings &s = settings();

    // clang-format off
    const QPair<QString, ColorAspect *> colorKeys[] = {
        { "Scheme/ColorBackground", &s.backgroundColor },
        { "Scheme/ColorForeground", &s.foregroundColor }
    };
    // clang-format on

    for (const auto &colorKey : colorKeys) {
        if (ini.contains(colorKey.first))
            colorKey.second->setVolatileValue(QColor(ini.value(colorKey.first).toString()));
    }

    const QStringList colors = ini.value(QLatin1String("Scheme/ColorPalette")).toStringList();
    int i = 0;
    for (const QString &color : colors)
        s.colors[i++].setVolatileValue(QColor(color));

    return {};
}

static Result<> loadVsCodeOrWindows(const FilePath &path)
{
    return loadVsCodeColors(path).or_else(
        [path](const auto &) { return loadWindowsTerminalColors(path); });
}

// OPENMV-DIFF //
// static Result<> loadColorScheme(const FilePath &path)
// OPENMV-DIFF //
// Unused while the Colors group (and its Load Theme button) is hidden.
[[maybe_unused]] static Result<> loadColorScheme(const FilePath &path)
// OPENMV-DIFF //
{
    if (path.endsWith("Xdefaults"))
        return loadXdefaults(path);
    else if (path.suffix() == "itermcolors")
        return loadItermColors(path);
    else if (path.suffix() == "json")
        return loadVsCodeOrWindows(path);
    else if (path.suffix() == "colorscheme")
        return loadKonsoleColorScheme(path);
    else if (path.suffix() == "theme" || path.completeSuffix() == "theme.txt")
        return loadXFCE4ColorScheme(path);

    return make_unexpected(Tr::tr("Unknown color scheme format."));
}

TerminalSettings &settings()
{
    static GuardedObject<TerminalSettings> theSettings;
    return theSettings;
}

TerminalSettings::TerminalSettings()
{
    setSettingsGroup("Terminal");
    setAutoApply(false);

    // OPENMV-DIFF //
    // enableTerminal.setSettingsKey("EnableTerminal");
    // OPENMV-DIFF //
    // Hidden, always on: it routes Qt Creator's run configurations and "Open
    // Terminal Here" to this pane, and the OpenMV IDE has neither. No settings key.
    // OPENMV-DIFF //
    enableTerminal.setLabelText(Tr::tr("Use internal terminal"));
    enableTerminal.setToolTip(
        Tr::tr("Uses the internal terminal when \"Run In Terminal\" is "
               "enabled and for \"Open Terminal here\"."));
    enableTerminal.setDefaultValue(true);

    fontFamily.setSettingsKey("FontFamily");
    fontFamily.setLabelText(Tr::tr("Family:"));
    // fontFamily.setHistoryCompleter("Terminal.Fonts.History");
    fontFamily.setToolTip(Tr::tr("The font family used in the terminal."));
    fontFamily.setDefaultValue(defaultFontFamily());
    fontFamily.setFontFilters(QFontComboBox::MonospacedFonts);

    fontSize.setSettingsKey("FontSize");
    fontSize.setLabelText(Tr::tr("Size:"));
    fontSize.setToolTip(Tr::tr("The font size used in the terminal (in points)."));
    fontSize.setDefaultValue(defaultFontSize());
    fontSize.setRange(1, 100);

    // OPENMV-DIFF //
    // Zoom and line spacing, as the text editor's font settings have.
    fontZoom.setSettingsKey("FontZoom");
    fontZoom.setLabelText(Tr::tr("Zoom:"));
    fontZoom.setToolTip(Tr::tr("Scales the font size. Ctrl+wheel zooms a single shell for the "
                               "session."));
    fontZoom.setSuffix("%");
    fontZoom.setRange(10, 3000);
    fontZoom.setSingleStep(10);
    fontZoom.setDefaultValue(100);

    lineSpacing.setSettingsKey("LineSpacing");
    lineSpacing.setLabelText(Tr::tr("Line spacing:"));
    lineSpacing.setToolTip(Tr::tr("The height of each line, as a percentage of the font's."));
    lineSpacing.setSuffix("%");
    lineSpacing.setRange(50, 300);
    lineSpacing.setSingleStep(10);
    lineSpacing.setDefaultValue(100);
    // OPENMV-DIFF //

    allowBlinkingCursor.setSettingsKey("AllowBlinkingCursor");
    allowBlinkingCursor.setLabelText(Tr::tr("Allow blinking cursor"));
    allowBlinkingCursor.setToolTip(Tr::tr("Allow the cursor to blink."));
    // OPENMV-DIFF //
    // allowBlinkingCursor.setDefaultValue(false);
    // OPENMV-DIFF //
    allowBlinkingCursor.setDefaultValue(true);
    // OPENMV-DIFF //

    // OPENMV-DIFF //
    // shell.setSettingsKey("ShellPath");
    // OPENMV-DIFF //
    shell.setLabelText(Tr::tr("Shell path:"));
    shell.setExpectedKind(PathChooser::ExistingCommand);
    shell.setHistoryCompleter("Terminal.Shell.History");
    shell.setToolTip(Tr::tr("The shell executable to be started."));
    // OPENMV-DIFF //
    // shell.setDefaultValue(defaultShell());
    // OPENMV-DIFF //
    const auto [defaultShellPath, defaultShellArguments] = defaultShellAndArguments();
    shell.setDefaultValue(defaultShellPath);
    // OPENMV-DIFF //

    // OPENMV-DIFF //
    // shellArguments.setSettingsKey("ShellArguments");
    // OPENMV-DIFF //
    shellArguments.setLabelText(Tr::tr("Shell arguments:"));
    shellArguments.setDisplayStyle(StringAspect::LineEditDisplay);
    shellArguments.setHistoryCompleter("Terminal.Shell.History");
    shellArguments.setToolTip(Tr::tr("The arguments to be passed to the shell."));
    // OPENMV-DIFF //
    // if (!HostOsInfo::isWindowsHost())
    //     shellArguments.setDefaultValue(QString("-l"));
    // OPENMV-DIFF //
    shellArguments.setDefaultValue(defaultShellArguments);
    // OPENMV-DIFF //

    sendEscapeToTerminal.setSettingsKey("SendEscapeToTerminal");
    sendEscapeToTerminal.setLabelText(Tr::tr("Send escape key to terminal"));
    sendEscapeToTerminal.setToolTip(Tr::tr("Sends the escape key to the terminal when pressed "
                                           "instead of closing the terminal."));
    // OPENMV-DIFF //
    // sendEscapeToTerminal.setDefaultValue(false);
    // OPENMV-DIFF //
    // A focused shell gets every key, Esc included (leave it by clicking away).
    sendEscapeToTerminal.setDefaultValue(true);
    // OPENMV-DIFF //

    static const QString escKey = QKeySequence(Qt::Key_Escape).toString(QKeySequence::NativeText);
    static const QString shiftEsc = QKeySequence(QKeyCombination(Qt::ShiftModifier, Qt::Key_Escape))
                                        .toString(QKeySequence::NativeText);
    sendEscapeToTerminal.setOnText(escKey);
    sendEscapeToTerminal.setOffText(shiftEsc);
    sendEscapeToTerminal.setOnTooltip(Tr::tr("Sends Esc to terminal instead of %1.")
                                          .arg(QGuiApplication::applicationDisplayName()));
    sendEscapeToTerminal.setOffTooltip(Tr::tr("Press %1 to send Esc to terminal.").arg(shiftEsc));
    QObject::connect(&sendEscapeToTerminal,
                     &ToggleAspect::changed,
                     this,
                     &TerminalSettings::writeSettings);

    lockKeyboard.setSettingsKey("LockKeyboard");
    lockKeyboard.setLabelText(Tr::tr("Block shortcuts in terminal"));
    lockKeyboard.setToolTip(
        Tr::tr("Keeps Qt Creator shortcuts from interfering with the terminal."));
    // OPENMV-DIFF //
    // lockKeyboard.setDefaultValue(false);
    // OPENMV-DIFF //
    // A focused shell gets every key; only the shell pane's own commands
    // (new/next/previous shell, find, preferences) still reach the IDE.
    lockKeyboard.setDefaultValue(true);
    // OPENMV-DIFF //

    lockKeyboard.setIcon(LOCK_KEYBOARD_ICON.icon());
    lockKeyboard.setOffIcon(UNLOCK_KEYBOARD_ICON.icon());
    lockKeyboard.setOnTooltip(Tr::tr("%1 shortcuts are blocked when focus is inside the terminal.")
                                  .arg(qApp->applicationDisplayName()));
    lockKeyboard.setOffTooltip(
        Tr::tr("%1 shortcuts take precedence.").arg(qApp->applicationDisplayName()));

    QObject::connect(&lockKeyboard, &ToggleAspect::changed, this, &TerminalSettings::writeSettings);

    audibleBell.setSettingsKey("AudibleBell");
    audibleBell.setLabelText(Tr::tr("Audible bell"));
    audibleBell.setToolTip(Tr::tr("Makes the terminal beep when a bell "
                                  "character is received."));
    audibleBell.setDefaultValue(true);

    enableMouseTracking.setSettingsKey("EnableMouseTracking");
    enableMouseTracking.setLabelText(Tr::tr("Enable mouse tracking"));
    enableMouseTracking.setToolTip(Tr::tr("Enables mouse tracking in the terminal."));
    enableMouseTracking.setDefaultValue(true);

    enableLiveReflow.setSettingsKey("EnableLiveReflow");
    enableLiveReflow.setLabelText(Tr::tr("Enable live reflow (experimental)"));
    enableLiveReflow.setToolTip(
        Tr::tr("Wraps and reflows text when resizing the terminal. "
               "Note that this does not work properly with all shells and prompts."));
    enableLiveReflow.setDefaultValue(false);

    setupColor(this, foregroundColor, "Foreground", creatorColor(Theme::TerminalForeground));
    setupColor(this, backgroundColor, "Background", creatorColor(Theme::TerminalBackground));
    setupColor(this, selectionColor, "Selection", creatorColor(Theme::TerminalSelection));

    setupColor(this, findMatchColor, "Find matches", creatorColor(Theme::TerminalFindMatch));

    setupColor(this, colors[0], "0", creatorColor(Theme::TerminalAnsi0), "black");
    setupColor(this, colors[8], "8", creatorColor(Theme::TerminalAnsi8), "bright black");

    setupColor(this, colors[1], "1", creatorColor(Theme::TerminalAnsi1), "red");
    setupColor(this, colors[9], "9", creatorColor(Theme::TerminalAnsi9), "bright red");

    setupColor(this, colors[2], "2", creatorColor(Theme::TerminalAnsi2), "green");
    setupColor(this, colors[10], "10", creatorColor(Theme::TerminalAnsi10), "bright green");

    setupColor(this, colors[3], "3", creatorColor(Theme::TerminalAnsi3), "yellow");
    setupColor(this, colors[11], "11", creatorColor(Theme::TerminalAnsi11), "bright yellow");

    setupColor(this, colors[4], "4", creatorColor(Theme::TerminalAnsi4), "blue");
    setupColor(this, colors[12], "12", creatorColor(Theme::TerminalAnsi12), "bright blue");

    setupColor(this, colors[5], "5", creatorColor(Theme::TerminalAnsi5), "magenta");
    setupColor(this, colors[13], "13", creatorColor(Theme::TerminalAnsi13), "bright magenta");

    setupColor(this, colors[6], "6", creatorColor(Theme::TerminalAnsi6), "cyan");
    setupColor(this, colors[14], "14", creatorColor(Theme::TerminalAnsi14), "bright cyan");

    setupColor(this, colors[7], "7", creatorColor(Theme::TerminalAnsi7), "white");
    setupColor(this, colors[15], "15", creatorColor(Theme::TerminalAnsi15), "bright white");

    setLayouter([this] {
        using namespace Layouting;

        // OPENMV-DIFF //
        // auto loadThemeButton = new QPushButton(Tr::tr("Load Theme..."));
        // auto resetTheme = new QPushButton(Tr::tr("Reset Theme"));
        // auto copyTheme = schemeLog().isDebugEnabled() ? new QPushButton(Tr::tr("Copy Theme"))
        //                                               : nullptr;

        // connect(loadThemeButton, &QPushButton::clicked, this, [] {
        //     const FilePath path = FileUtils::getOpenFilePath(
        //         "Open Theme",
        //         {},
        //         "All Scheme formats (*.itermcolors *.json *.colorscheme *.theme *.theme.txt);;"
        //         "Xdefaults (.Xdefaults Xdefaults);;"
        //         "iTerm Color Schemes(*.itermcolors);;"
        //         "VS Code Color Schemes(*.json);;"
        //         "Windows Terminal Schemes(*.json);;"
        //         "Konsole Color Schemes(*.colorscheme);;"
        //         "XFCE4 Terminal Color Schemes(*.theme *.theme.txt);;"
        //         "All files (*)",
        //         nullptr,
        //         {},
        //         true,
        //         false);

        //     if (path.isEmpty())
        //         return;

        //     const Result<> result = loadColorScheme(path);
        //     if (!result)
        //         QMessageBox::warning(Core::ICore::dialogParent(), Tr::tr("Error"), result.error());
        // });

        // connect(resetTheme, &QPushButton::clicked, this, [this] {
        //     foregroundColor.setVolatileValue(foregroundColor.defaultValue());
        //     backgroundColor.setVolatileValue(backgroundColor.defaultValue());
        //     selectionColor.setVolatileValue(selectionColor.defaultValue());

        //     for (ColorAspect &color : colors)
        //         color.setVolatileValue(color.defaultValue());
        // });

        // if (schemeLog().isDebugEnabled()) {
        //     connect(copyTheme, &QPushButton::clicked, this, [this] {
        //         auto toThemeColor = [](const ColorAspect &color) -> QString {
        //             QColor c = color.value();
        //             QString a = c.alpha() != 255 ? QString("%1").arg(c.alpha(), 2, 16, QChar('0'))
        //                                          : QString();
        //             return QString("%1%2%3%4")
        //                 .arg(a)
        //                 .arg(c.red(), 2, 16, QChar('0'))
        //                 .arg(c.green(), 2, 16, QChar('0'))
        //                 .arg(c.blue(), 2, 16, QChar('0'));
        //         };

        //         QString theme;
        //         QTextStream stream(&theme);
        //         stream << "TerminalForeground=" << toThemeColor(foregroundColor) << '\n';
        //         stream << "TerminalBackground=" << toThemeColor(backgroundColor) << '\n';
        //         stream << "TerminalSelection=" << toThemeColor(selectionColor) << '\n';
        //         stream << "TerminalFindMatch=" << toThemeColor(findMatchColor) << '\n';
        //         for (int i = 0; i < 16; ++i)
        //             stream << "TerminalAnsi" << i << '=' << toThemeColor(colors[i]) << '\n';

        //         setClipboardAndSelection(theme);
        //     });
        // }
        // OPENMV-DIFF //
        // No Colors group (the colors follow the IDE theme), so no theme buttons.
        // OPENMV-DIFF //

        // clang-format off
        return Column {
            Group {
                title(Tr::tr("General")),
                Column {
                    // OPENMV-DIFF //
                    // enableTerminal, st,
                    // OPENMV-DIFF //
                    sendEscapeToTerminal, st,
                    lockKeyboard, st,
                    audibleBell, st,
                    allowBlinkingCursor, st,
                    enableMouseTracking, st,
                    enableLiveReflow, st,
                },
            },
            Group {
                title(Tr::tr("Font")),
                // OPENMV-DIFF //
                // Row {
                //     fontFamily, Space(20),
                //     fontSize, st,
                // },
                // OPENMV-DIFF //
                // Two rows, columns aligned: family and size, then zoom and line
                // spacing (as on the text editor's Font page).
                Row {
                    Grid {
                        fontFamily, Space(20), fontSize, br,
                        fontZoom, Space(20), lineSpacing, br,
                    },
                    st,
                },
                // OPENMV-DIFF //
            },
            // OPENMV-DIFF //
            // Group {
            //     title(Tr::tr("Colors")),
            //     Column {
            //         Row {
            //             Tr::tr("Foreground"), foregroundColor, st,
            //             Tr::tr("Background"), backgroundColor, st,
            //             Tr::tr("Selection"), selectionColor, st,
            //             Tr::tr("Find match"), findMatchColor, st,
            //         },
            //         Row {
            //             colors[0], colors[1],
            //             colors[2], colors[3],
            //             colors[4], colors[5],
            //             colors[6], colors[7]
            //         },
            //         Row {
            //             colors[8], colors[9],
            //             colors[10], colors[11],
            //             colors[12], colors[13],
            //             colors[14], colors[15]
            //         },
            //         Row {
            //             loadThemeButton, resetTheme, copyTheme, st,
            //         }
            //     },
            // },
            // OPENMV-DIFF //
            Group {
                title(Tr::tr("Default Shell")),
                // OPENMV-DIFF //
                // Column {
                //     shell,
                //     shellArguments,
                // },
                // OPENMV-DIFF //
                // Full width, like the shell path field it replaces; the group title
                // already names it.
                Column {
                    defaultShell,
                },
                // OPENMV-DIFF //
            },
            st,
        };
        // clang-format on
    });

    // OPENMV-DIFF //
    defaultShell.setSettingsKey("DefaultShell");
    defaultShell.setToolTip(Tr::tr("The shell the + button opens. Automatic picks the best one "
                                   "installed and follows changes to what is installed."));
    defaultShell.setComboBoxEditable(false);
    defaultShell.setDefaultValue("auto");
    defaultShell.setFillCallback([](const StringSelectionAspect::ResultCallback &cb) {
        QList<QStandardItem *> items;
        const FilePath best = FilePath::fromUserInput(defaultShellAndArguments().first);
        const QString bestName = friendlyShellName(best);
        auto automatic = new QStandardItem(
            Tr::tr("Automatic (%1)").arg(bestName.isEmpty() ? best.fileName() : bestName));
        automatic->setData(QString("auto"));
        items.append(automatic);
        for (const Internal::ShellModelItem &shellItem : Internal::shellModel().local()) {
            if (!shellItem.openParameters.shellCommand)
                continue;
            auto item = new QStandardItem(shellItem.openParameters.icon, shellItem.name);
            item->setData(shellKey(*shellItem.openParameters.shellCommand));
            items.append(item);
        }
        cb(items);
    });
    connect(&defaultShell, &BaseAspect::changed, this, &TerminalSettings::applyDefaultShell);
    // OPENMV-DIFF //

    readSettings();
    // OPENMV-DIFF //
    applyDefaultShell();
    // OPENMV-DIFF //
}

class TerminalSettingsPage final : public Core::IOptionsPage
{
public:
    TerminalSettingsPage()
    {
        setId("Terminal.General");
        // OPENMV-DIFF //
        // setDisplayName("Terminal");
        // OPENMV-DIFF //
        setDisplayName("Shell");
        // OPENMV-DIFF //
        setCategory("ZY.Terminal");
        setSettingsProvider([] { return &settings(); });
    }
};

const TerminalSettingsPage settingsPage;

// OPENMV-DIFF //
// Sets the shell path and arguments (no longer saved) from the "Default shell"
// choice; a shell that is no longer installed falls back to Automatic.
void TerminalSettings::applyDefaultShell()
{
    const QString choice = defaultShell();
    if (choice != "auto") {
        for (const Internal::ShellModelItem &item : Internal::shellModel().local()) {
            if (item.openParameters.shellCommand
                && shellKey(*item.openParameters.shellCommand) == choice) {
                shell.setValue(item.openParameters.shellCommand->executable());
                shellArguments.setValue(item.openParameters.shellCommand->arguments());
                return;
            }
        }
    }
    const auto [path, arguments] = defaultShellAndArguments();
    shell.setValue(FilePath::fromUserInput(path));
    shellArguments.setValue(arguments);
}
// OPENMV-DIFF //

} // Terminal
