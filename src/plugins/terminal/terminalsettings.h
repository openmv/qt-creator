// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0+ OR GPL-3.0 WITH Qt-GPL-exception-1.0

#pragma once

#include <utils/aspects.h>

namespace Terminal {

class TerminalSettings : public Utils::AspectContainer
{
public:
    TerminalSettings();

    Utils::BoolAspect enableTerminal{this};

    Utils::FontFamilyAspect fontFamily{this};
    Utils::IntegerAspect fontSize{this};
    // OPENMV-DIFF //
    Utils::IntegerAspect fontZoom{this};    // percent of the font size, like the editor's
    Utils::IntegerAspect lineSpacing{this}; // percent of the font's line height
    // OPENMV-DIFF //
    Utils::FilePathAspect shell{this};
    Utils::StringAspect shellArguments{this};

    Utils::ColorAspect foregroundColor;
    Utils::ColorAspect backgroundColor;
    Utils::ColorAspect selectionColor;
    Utils::ColorAspect findMatchColor;

    Utils::ColorAspect colors[16];

    Utils::BoolAspect allowBlinkingCursor{this};

    Utils::ToggleAspect sendEscapeToTerminal{this};
    Utils::BoolAspect audibleBell{this};
    Utils::ToggleAspect lockKeyboard{this};

    Utils::BoolAspect enableMouseTracking{this};
    Utils::BoolAspect enableLiveReflow{this};

    // OPENMV-DIFF //
    // Which shell + opens: "auto" (the best one found) or a shell's stable id. The
    // shell path and arguments above are no longer saved; they follow this.
    Utils::StringSelectionAspect defaultShell{this};
    void applyDefaultShell();
    // OPENMV-DIFF //
};

TerminalSettings &settings();

// OPENMV-DIFF //
// Windows shells, found and named the same way for the default shell, the
// new-shell menu and the tab labels.
Utils::FilePath gitBashPath();      // Git for Windows' bash.exe, or empty
Utils::FilePath powerShell7Path();  // pwsh.exe, or empty
bool isWslLauncher(const Utils::FilePath &shell); // System32's bash.exe / wsl.exe
bool wslHasDistribution();          // a WSL distro is installed for this user
QString friendlyShellName(const Utils::FilePath &shell); // e.g. "Git Bash", or empty
Utils::FilePath shellIconFile(const Utils::FilePath &shell); // the file to take its icon from
// OPENMV-DIFF //

} // Terminal
