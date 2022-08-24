/****************************************************************************
**
** Copyright (C) 2022 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of Qt Creator.
**
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3 as published by the Free Software
** Foundation with exceptions as appearing in the file LICENSE.GPL3-EXCEPT
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-3.0.html.
**
****************************************************************************/

#pragma once

#include <cppeditor/cppcodestylesettingspage.h>

#include <memory>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLabel;
QT_END_NAMESPACE

namespace ProjectExplorer { class Project; }

namespace ClangFormat {

class ClangFormatGlobalConfigWidget : public CppEditor::CppCodeStyleWidget
{
    Q_OBJECT

public:
    explicit ClangFormatGlobalConfigWidget(ProjectExplorer::Project *project = nullptr,
                                           QWidget *parent = nullptr);
    ~ClangFormatGlobalConfigWidget() override;
    void apply() override;

private:
    void initCheckBoxes();
    void initIndentationOrFormattingCombobox();
    void initOverrideCheckBox();

    bool projectClangFormatFileExists();

    ProjectExplorer::Project *m_project;

    QLabel *m_projectHasClangFormat;
    QLabel *m_formattingModeLabel;
    QComboBox *m_indentingOrFormatting;
    QCheckBox *m_formatWhileTyping;
    QCheckBox *m_formatOnSave;
    QCheckBox *m_overrideDefault;
};

} // namespace ClangFormat