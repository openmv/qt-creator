// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "collapsibleframe.h"

#include <acp/acp.h>

#include <utils/icon.h>

#include <optional>

#include <QImage> // OPENMV-DIFF
#include <QSet> // OPENMV-DIFF

class QLabel;

namespace Utils {
class ElidingLabel;
class MarkdownBrowser;
} // namespace Utils

namespace AcpClient::Internal {

class LazyMarkdownBrowser; // OPENMV-DIFF

QWidget *toolCallStatusWidget(Acp::ToolCallStatus status, QWidget *parent = nullptr);
QColor toolCallBorderColor(Acp::ToolCallStatus status);
std::optional<Utils::Icon> iconForToolKind(std::optional<Acp::ToolKind> kind);

// OPENMV-DIFF //
// A script the agent ran on the camera: a chip (Python icon, "script.py · N
// lines") that opens it in the editor, as examples open, when clicked.
QWidget *createScriptChip(const QString &script, const QString &name, QWidget *parent = nullptr);
int chipHeight(const QFontMetrics &fm); // of the chips in tool call header rows
// A tool's title for display: "mcp__OpenMV_IDE__open_file" -> "Open file · OpenMV IDE";
// other titles as they are.
QString displayToolTitle(const QString &title);
void openScriptInEditor(const QString &script, const QString &name);
// OPENMV-DIFF //

class ToolCallDetailWidget : public CollapsibleFrame
{
    Q_OBJECT

public:
    explicit ToolCallDetailWidget(const Acp::ToolCall &toolCall, QWidget *parent = nullptr);

    Acp::ToolCallStatus status() const { return m_status; }
    void applyStatus(Acp::ToolCallStatus status);
    void updateContent(const Acp::ToolCallUpdate &update);
    void setContentMaxWidth(int width);
    // OPENMV-DIFF //
    QString script() const { return m_script; } // the script it ran, if it ran one
    QString scriptName() const { return m_scriptName; } // its file name, if the agent gave one
    QImage latestImage() const { return m_latestImage; } // the last image it returned

signals:
    void scriptFound(const QString &script, const QString &name);
    void imageFound(const QImage &image);
    // OPENMV-DIFF //

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    void populateContent(const Acp::ToolCall &toolCall);
    void addDiffContent(const Acp::Diff &diff);
    void addMarkdownContent(const QString &markdown);
    void addTerminalContent(const Acp::Terminal &terminal);
    void addTextContent(const Acp::Content &content);
    void addLocations(const QList<Acp::ToolCallLocation> &locations);
    void addRawInputContent(const QJsonValue &rawInput);
    void addBodyWidget(QWidget *widget);

    QWidget *m_statusWidget = nullptr;
    Utils::ElidingLabel *m_titleLabel = nullptr;
    QLabel *m_commandLabel = nullptr;
    CollapsibleFrame *m_rawInputFrame = nullptr;
    // OPENMV-DIFF //
    // Utils::MarkdownBrowser *m_rawInputContent = nullptr;
    LazyMarkdownBrowser *m_rawInputContent = nullptr;
    // OPENMV-DIFF //
    Acp::ToolCallStatus m_status = Acp::ToolCallStatus::in_progress;
    // OPENMV-DIFF //
    QString m_toolName; // e.g. "mcp__OpenMV_IDE__openmv_script_run"
    bool isIdeTool() const; // one of the IDE's own MCP tools
    QSet<size_t> m_imageHashes; // images shown, so a repeated update adds none
    QImage m_latestImage;
    void addImage(const QByteArray &encoded);
    void addImagesFrom(const QJsonValue &value);
    QString m_script;
    void setScript(const QString &script, const QString &name);
    QString m_scriptName;
    // OPENMV-DIFF //
    int m_contentMaxWidth = -1;
};

} // namespace AcpClient::Internal
