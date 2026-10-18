// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "chatinputcompletion.h"

#include <texteditor/texteditor.h>

// OPENMV-DIFF //
#include <functional>
// OPENMV-DIFF //

namespace Utils { class HistoryCompleter; }

namespace AcpClient::Internal {

class ChatInputEdit : public TextEditor::TextEditorWidget
{
    Q_OBJECT

public:
    explicit ChatInputEdit(QWidget *parent = nullptr);

    void setAvailableCommands(const QList<CommandInfo> &commands);
    // OPENMV-DIFF //
    // Pasted (or dropped) images and files go to this handler instead of being
    // inserted as text; it returns whether it took them.
    void setAttachHandler(const std::function<bool(const QMimeData *)> &handler)
    {
        m_attachHandler = handler;
    }
    // OPENMV-DIFF //

signals:
    void sendRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void setDisplaySettings(const TextEditor::DisplaySettingsData &settings) override;
    void setMarginSettings(const TextEditor::MarginSettingsData &settings) override;
    int extraAreaWidth(int * = nullptr) const override { return 0; }
    // OPENMV-DIFF //
    bool canInsertFromMimeData(const QMimeData *source) const override;
    void insertFromMimeData(const QMimeData *source) override;
    // OPENMV-DIFF //

private:
    void updateHeight();
    void updateSuggestion();
    void historyUp();
    void historyDown();

    ChatInputCompletionProvider *m_completionProvider = nullptr;
    Utils::HistoryCompleter *m_history = nullptr;
    int m_historyIndex = -1;
    QString m_editBuffer;
    std::function<bool(const QMimeData *)> m_attachHandler; // OPENMV-DIFF
};

} // namespace AcpClient::Internal
