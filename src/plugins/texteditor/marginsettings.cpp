// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "marginsettings.h"

#include "texteditorsettings.h"
#include "texteditortr.h"

using namespace Utils;

namespace TextEditor {

MarginSettings &marginSettings()
{
    static MarginSettings theMarginSettings;
    return theMarginSettings;
}

MarginSettings::MarginSettings(const Key &keyPrefix)
{
    const bool isGlobal = keyPrefix.isEmpty();
    setAutoApply(!isGlobal);

    setSettingsGroup("textMarginSettings");

    showMargin.setSettingsKey(keyPrefix + "ShowMargin");
    // OPENMV-DIFF //
    // showMargin.setDefaultValue(false);
    // OPENMV-DIFF //
    showMargin.setDefaultValue(true);
    // OPENMV-DIFF //
    showMargin.setLabelText(Tr::tr("Display right &margin after column:"));

    tintMarginArea.setSettingsKey(keyPrefix + "tintMarginArea");
    tintMarginArea.setDefaultValue(true);
    tintMarginArea.setLabelText(Tr::tr("Tint whole margin area"));

    // OPENMV-DIFF //
    // useIndenter.setSettingsKey(keyPrefix + "UseIndenter");
    // OPENMV-DIFF //
    // Always off: only ClangFormat (not built) supplies a context-specific margin.
    // No settings key, so a stored value is never loaded, and no checkbox.
    // OPENMV-DIFF //
    useIndenter.setDefaultValue(false);
    useIndenter.setLabelText(Tr::tr("Use context-specific margin"));
    useIndenter.setToolTip(Tr::tr("If available, use a different margin. "
                                  "For example, the ColumnLimit from the ClangFormat plugin."));

    marginColumn.setSettingsKey(keyPrefix + "MarginColumn");
    // OPENMV-DIFF //
    // marginColumn.setDefaultValue(80);
    // OPENMV-DIFF //
    marginColumn.setDefaultValue(100);
    // OPENMV-DIFF //
    marginColumn.setRange(0, 999);

    // OPENMV-DIFF //
    // centerEditorContentWidthPercent.setSettingsKey(keyPrefix + "centeredEditorContentWidthPercent");
    // OPENMV-DIFF //
    // Always 100% (the full editor width): no settings key, so a stored value is
    // never loaded, and no control on the Display page.
    // OPENMV-DIFF //
    centerEditorContentWidthPercent.setRange(50, 100);
    centerEditorContentWidthPercent.setDefaultValue(100);
    centerEditorContentWidthPercent.setSuffix("%");
    centerEditorContentWidthPercent.setLabelText(Tr::tr("Editor content width:"));
    centerEditorContentWidthPercent.setToolTip(
        Tr::tr(
            "100% means that whole width of editor window is used to display text"
            " content (default).<br>50% means that half of editor width is used to display"
            " text content.<br>Remaining 50% is divided as left and right margins while"
            " centering the content."));

    if (isGlobal)
        readSettings();

    marginColumn.setEnabler(&showMargin);
    tintMarginArea.setEnabler(&showMargin);
}

MarginSettingsData MarginSettings::data() const
{
    MarginSettingsData d;
    d.m_showMargin = showMargin();
    d.m_tintMarginArea = tintMarginArea();
    d.m_useIndenter = useIndenter();
    d.m_marginColumn = marginColumn();
    d.m_centerEditorContentWidthPercent = centerEditorContentWidthPercent();
    return d;
}

void MarginSettings::setData(const MarginSettingsData &data)
{
    showMargin.setValue(data.m_showMargin);
    tintMarginArea.setValue(data.m_tintMarginArea);
    useIndenter.setValue(data.m_useIndenter);
    marginColumn.setValue(data.m_marginColumn);
    centerEditorContentWidthPercent.setValue(data.m_centerEditorContentWidthPercent);
}

} // TextEditor
