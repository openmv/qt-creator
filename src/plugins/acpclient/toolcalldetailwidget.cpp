// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "toolcalldetailwidget.h"

#include "acpclienttr.h"

#include <coreplugin/editormanager/editormanager.h>

#include <utils/link.h>
// OPENMV-DIFF //
#include <extensionsystem/pluginmanager.h>
#include <extensionsystem/pluginspec.h>
#include <utils/fsengine/fileiconprovider.h>

#include <QAbstractButton>
#include <app/app_version.h>
#include <QRegularExpression>
// OPENMV-DIFF //
#include <utils/markdownbrowser.h>
#include <utils/progressindicator.h>
#include <utils/qtcwidgets.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>
#include <utils/elidinglabel.h>
#include <utils/utilsicons.h>

#include <QAbstractTextDocumentLayout>
// OPENMV-DIFF //
#include <QContextMenuEvent>
#include <QMenu>
// OPENMV-DIFF //
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QTextDocument>
#include <QVBoxLayout>
#include <QtMath>

using namespace Acp;
using namespace Utils::StyleHelper::SpacingTokens;

namespace AcpClient::Internal {

// OPENMV-DIFF //
// A markdown view that renders (parses and syntax-highlights) only once it is on
// screen. Tool call bodies start collapsed and most are never opened, but a chat
// has hundreds of tool calls, each with diffs and raw-input JSON (often whole
// files); rendering all of them as they arrived kept the IDE busy for seconds
// when loading a chat, and on every streamed raw-input update.
class LazyMarkdownBrowser : public Utils::MarkdownBrowser
{
public:
    explicit LazyMarkdownBrowser(QWidget *parent = nullptr)
        : Utils::MarkdownBrowser(parent)
    {
        // The tool call card's color, not a box in the text area's own (white on
        // light themes); opaque, or Windows' sharp text smoothing is lost.
        QPalette pal = palette();
        pal.setColor(QPalette::Base, Utils::creatorColor(Utils::Theme::ChatToolCallBackground));
        setPalette(pal);
        setAutoFillBackground(false);
        viewport()->setAutoFillBackground(true);
    }

    void setMarkdownLazily(const QString &markdown)
    {
        if (markdown == m_markdown)
            return;
        m_markdown = markdown;
        m_dirty = true;
        if (isVisible())
            render();
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        if (m_dirty)
            render();
        Utils::MarkdownBrowser::showEvent(event);
    }

    // The browser's menu without "Copy Link Location" unless there's a link to copy.
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QMenu *menu = createStandardContextMenu(event->pos());
        for (QAction *action : menu->actions()) {
            if (action->objectName() == QLatin1String("link-copy") && !action->isEnabled())
                menu->removeAction(action);
        }
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(event->globalPos());
    }

private:
    void render()
    {
        m_dirty = false;
        setMarkdown(m_markdown);
    }

    QString m_markdown;
    bool m_dirty = false;
};
// OPENMV-DIFF //

// OPENMV-DIFF //
class ScriptChip : public QAbstractButton
{
public:
    ScriptChip(const QString &script, const QString &name, QWidget *parent)
        : QAbstractButton(parent)
    {
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(Tr::tr("Open in Editor"));
        const int lines = script.count(QLatin1Char('\n')) + (script.endsWith('\n') ? 0 : 1);
        // The name the agent gave it (an example's, say), else "script.py".
        QString fileName = Utils::FilePath::fromUserInput(name.trimmed()).fileName();
        if (fileName.isEmpty())
            fileName = QStringLiteral("script.py");
        else if (!fileName.endsWith(QLatin1String(".py"), Qt::CaseInsensitive))
            fileName += QStringLiteral(".py");
        setText(lines == 1 ? Tr::tr("%1 \u00b7 1 line").arg(fileName)
                           : Tr::tr("%1 \u00b7 %2 lines").arg(fileName).arg(lines));
        m_icon = Utils::FileIconProvider::icon(Utils::FilePath::fromString("script.py"));
        connect(this, &QAbstractButton::clicked, this,
                [script, fileName] { openScriptInEditor(script, fileName); });
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm = fontMetrics();
        // No taller than a line of text: the header rows it sits in (whose
        // expand arrow is top-aligned) keep their height.
        return QSize(PaddingHS + iconSize() + GapHXs + fm.horizontalAdvance(text()) + PaddingHM,
                     chipHeight(fm));
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor fill = Utils::creatorColor(Utils::Theme::Token_Foreground_Subtle);
        if (underMouse())
            fill = fill.lighter(125);
        Utils::StyleHelper::drawCardBg(&p, rect(), fill, Qt::NoPen, RadiusS);
        const int icon = iconSize();
        const QRect iconRect(PaddingHS, (height() - icon) / 2, icon, icon);
        m_icon.paint(&p, iconRect);
        p.setPen(Utils::creatorColor(Utils::Theme::Token_Text_Default));
        p.drawText(rect().adjusted(PaddingHS + iconSize() + GapHXs, 0, -PaddingHM, 0),
                   Qt::AlignLeft | Qt::AlignVCenter, text());
    }

private:
    int iconSize() const { return 16; }

    QIcon m_icon;
};

QString displayToolTitle(const QString &title)
{
    static const QRegularExpression mcpName(QStringLiteral("^mcp__(.+?)__(.+)$"));
    const QRegularExpressionMatch match = mcpName.match(title.trimmed());
    if (!match.hasMatch())
        return title;
    const QString server = QString(match.captured(1)).replace(QLatin1Char('_'), QLatin1Char(' '));
    QString tool = match.captured(2);
    if (tool.startsWith(QLatin1String("openmv_"))) // the IDE's tools all carry it
        tool = tool.mid(7);
    tool.replace(QLatin1Char('_'), QLatin1Char(' '));
    if (!tool.isEmpty())
        tool[0] = tool[0].toUpper();
    return tool + QStringLiteral(" \u00b7 ") + server;
}

int chipHeight(const QFontMetrics &fm)
{
    return qMax(16, fm.height()) + 2 * PaddingVXs;
}

QWidget *createScriptChip(const QString &script, const QString &name, QWidget *parent)
{
    return new ScriptChip(script, name, parent);
}

// Through the OpenMV plugin (as examples open), else as a plain untitled tab.
void openScriptInEditor(const QString &script, const QString &name)
{
    for (ExtensionSystem::PluginSpec *spec : ExtensionSystem::PluginManager::plugins()) {
        if (spec->id() == QLatin1String("openmv") && spec->plugin()
            && QMetaObject::invokeMethod(spec->plugin(), "openScriptInEditor",
                                         Q_ARG(QString, script), Q_ARG(QString, name))) {
            return;
        }
    }
    QString titlePattern = QStringLiteral("script_$.py");
    Core::EditorManager::openEditorWithContents("PythonEditor.PythonEditor", &titlePattern,
                                                script.toUtf8());
}

// The tool's name: Claude's "mcp__<server>__<tool>" in _meta, else the title.
static QString toolNameOf(const std::optional<QJsonObject> &meta, const QString &title)
{
    if (meta) {
        const QString name = meta->value("claudeCode").toObject().value("toolName").toString();
        if (!name.isEmpty())
            return name;
    }
    return title;
}
// OPENMV-DIFF //

QWidget *toolCallStatusWidget(ToolCallStatus status, QWidget *parent)
{
    if (status == ToolCallStatus::in_progress)
        return new Utils::ProgressIndicator(Utils::ProgressIndicatorSize::Small, parent);

    Utils::Icon icon;
    switch (status) {
    case ToolCallStatus::pending:   icon = Utils::Icons::DOWNLOAD; break;
    case ToolCallStatus::completed: icon = Utils::Icons::OK; break;
    case ToolCallStatus::failed:    icon = Utils::Icons::CRITICAL; break;
    default: break;
    }
    auto *display = new Utils::QtcIconDisplay(parent);
    display->setIcon(icon);
    return display;
}

std::optional<Utils::Icon> iconForToolKind(std::optional<ToolKind> kind)
{
    if (kind) {
        switch (*kind) {
        case ToolKind::read:
            return Utils::Icons::SAVEFILE;
        case ToolKind::edit:
            return Utils::Icons::PASTE;
        case ToolKind::delete_:
            return Utils::Icons::CLEAN;
        case ToolKind::move:
            return Utils::Icons::COPY;
        case ToolKind::search:
            return Utils::Icons::ZOOM;
        case ToolKind::execute:
            return Utils::Icons::RUN_SMALL;
        case ToolKind::think:
            return Utils::Icons::EYE_OPEN;
        case ToolKind::fetch:
            return Utils::Icons::DOWNLOAD;
        case ToolKind::switch_mode:
            return Utils::Icons::RELOAD;
        case ToolKind::other:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

QColor toolCallBorderColor(ToolCallStatus status)
{
    switch (status) {
    case ToolCallStatus::pending:     return Qt::gray;
    case ToolCallStatus::in_progress: return QColor{0x1e, 0x90, 0xff}; // dodgerblue
    case ToolCallStatus::completed:   return Qt::green;
    case ToolCallStatus::failed:      return Qt::red;
    }
    return Qt::gray;
}

ToolCallDetailWidget::ToolCallDetailWidget(const ToolCall &toolCall, QWidget *parent)
    : CollapsibleFrame(parent)
{
    setFrameShape(QFrame::NoFrame);
    setCollapsible(false);

    const ToolCallStatus status = toolCall.status().value_or(ToolCallStatus::in_progress);

    // Header: status widget + title + kind badge
    m_statusWidget = toolCallStatusWidget(status, this);
    m_headerLayout->addWidget(m_statusWidget);

    if (const auto icon = iconForToolKind(toolCall.kind())) {
        auto *kindIcon = new Utils::QtcIconDisplay(this);
        kindIcon->setIcon(*icon);
        m_headerLayout->addWidget(kindIcon);
    }

    // OPENMV-DIFF //
    // m_titleLabel = new Utils::ElidingLabel(toolCall.title(), this);
    m_titleLabel = new Utils::ElidingLabel(displayToolTitle(toolCall.title()), this);
    m_titleLabel->setToolTip(toolCall.title());
    // OPENMV-DIFF //
    // OPENMV-DIFF //
    // Elided text may shrink below its full width (the OpenMV IDE's
    // ElidingLabel no longer sets this): a long title made the chat wider
    // than the panel instead of eliding.
    m_titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    // OPENMV-DIFF //
    m_titleLabel->setElideMode(Qt::ElideRight);
    m_headerLayout->addWidget(m_titleLabel, 1);

    m_status = status;
    m_toolName = toolNameOf(toolCall._meta(), toolCall.title()); // OPENMV-DIFF
    populateContent(toolCall);
}

// OPENMV-DIFF //
// The script, on a chip in the header and as highlighted code in the body.
void ToolCallDetailWidget::setScript(const QString &script, const QString &name)
{
    if (script.isEmpty() || script == m_script)
        return;
    const bool first = m_script.isEmpty();
    m_script = script;
    m_scriptName = name;
    if (first) {
        QWidget *chip = createScriptChip(script, name, this);
        m_headerLayout->addWidget(chip);
        setFirstRowHeight(chip->sizeHint().height());
    }
    addMarkdownContent(QStringLiteral("```python\n") + script + QStringLiteral("\n```"));
    emit scriptFound(script, name);
}
// OPENMV-DIFF //

void ToolCallDetailWidget::applyStatus(ToolCallStatus status)
{
    auto *newWidget = toolCallStatusWidget(status, this);
    delete m_headerLayout->replaceWidget(m_statusWidget, newWidget);
    delete m_statusWidget;
    m_statusWidget = newWidget;
    m_status = status;
    update();
}

void ToolCallDetailWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    Utils::StyleHelper::drawCardBg(&p, rect(),
        Utils::creatorColor(Utils::Theme::ChatToolCallBackground),
        QPen(palette().color(QPalette::Mid), 1),
        RadiusM);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                        RadiusM - 0.5, RadiusM - 0.5);
    p.setClipPath(clip);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(QRect(0, 0, 3, height()), toolCallBorderColor(m_status));
    p.setClipping(false);
}

void ToolCallDetailWidget::setContentMaxWidth(int width)
{
    m_contentMaxWidth = width - BodyMarginLeft - BodyMarginRight;
    for (int i = 0; i < m_bodyLayout->count(); ++i) {
        if (QWidget *w = m_bodyLayout->itemAt(i)->widget())
            w->setMaximumWidth(m_contentMaxWidth);
    }
}

void ToolCallDetailWidget::updateContent(const ToolCallUpdate &update)
{
    if (const auto status = update.status())
        applyStatus(*status);
    if (const auto title = update.title(); title && !title->isEmpty())
    {
        // OPENMV-DIFF //
        // m_titleLabel->setText(*title);
        m_titleLabel->setText(displayToolTitle(*title));
        m_titleLabel->setToolTip(*title);
        // OPENMV-DIFF //
    }
    // OPENMV-DIFF //
    if (const QString name = toolNameOf(update._meta(), update.title().value_or(QString()));
        !name.isEmpty() && (m_toolName.isEmpty() || name.startsWith("mcp__"))) {
        m_toolName = name;
    }
    // OPENMV-DIFF //

    if (const auto &rawInput = update.rawInput())
        addRawInputContent(*rawInput);

    // OPENMV-DIFF //
    // Images the tool returned (a camera frame, say): agents pass the tool's raw
    // result along, images included, while the text content carries none.
    if (const auto &rawOutput = update.rawOutput())
        addImagesFrom(*rawOutput);
    // OPENMV-DIFF //

    // Parse updated content if available
    if (const auto &contentArr = update.content()) {
        for (const QJsonValue &val : *contentArr) {
            auto content = fromJson<ToolCallContent>(val);
            if (!content)
                continue;
            if (auto *diff = std::get_if<Diff>(&*content))
                addDiffContent(*diff);
            else if (auto *terminal = std::get_if<Terminal>(&*content))
                addTerminalContent(*terminal);
            else if (auto *textContent = std::get_if<Content>(&*content))
                addTextContent(*textContent);
        }
    }

    // Parse updated locations
    if (const auto &locsArr = update.locations()) {
        QList<ToolCallLocation> locs;
        for (const QJsonValue &val : *locsArr) {
            auto loc = fromJson<ToolCallLocation>(val);
            if (loc)
                locs.append(*loc);
        }
        if (!locs.isEmpty())
            addLocations(locs);
    }
}

void ToolCallDetailWidget::populateContent(const ToolCall &toolCall)
{
    if (const auto &rawInput = toolCall.rawInput())
        addRawInputContent(*rawInput);

    // Content items
    if (const auto &contentList = toolCall.content()) {
        for (const ToolCallContent &tc : *contentList) {
            if (auto *diff = std::get_if<Diff>(&tc))
                addDiffContent(*diff);
            else if (auto *terminal = std::get_if<Terminal>(&tc))
                addTerminalContent(*terminal);
            else if (auto *textContent = std::get_if<Content>(&tc))
                addTextContent(*textContent);
        }
    }

    // Locations
    if (const auto &locs = toolCall.locations())
        addLocations(*locs);
}

void ToolCallDetailWidget::addDiffContent(const Diff &diff)
{
    // File path as clickable link
    auto *pathLabel = new QLabel(this);
    pathLabel->setTextFormat(Qt::RichText);
    const QColor linkColor = Utils::creatorColor(Utils::Theme::TextColorLink);
    pathLabel->setText(QStringLiteral("<a href=\"file://%1\" style=\"color:%2; text-decoration:none;\">%1</a>")
                           .arg(diff.path().toHtmlEscaped(),
                                linkColor.name()));
    pathLabel->setCursor(Qt::PointingHandCursor);
    connect(pathLabel, &QLabel::linkActivated, this, [](const QString &link) {
        const QString path = QUrl(link).toLocalFile();
        if (!path.isEmpty())
            Core::EditorManager::openEditorAt(Utils::Link::fromString(path));
    });
    addBodyWidget(pathLabel);

    // Render diff as markdown code block
    QString markdown = QStringLiteral("```diff\n");
    if (diff.oldText().has_value()) {
        // Show abbreviated diff context
        const QStringList oldLines = diff.oldText()->split(QLatin1Char('\n'));
        const QStringList newLines = diff.newText().split(QLatin1Char('\n'));
        for (const QString &line : oldLines)
            markdown += QStringLiteral("-%1\n").arg(line);
        for (const QString &line : newLines)
            markdown += QStringLiteral("+%1\n").arg(line);
    } else {
        // New file
        const QStringList lines = diff.newText().split(QLatin1Char('\n'));
        for (const QString &line : lines)
            markdown += QStringLiteral("+%1\n").arg(line);
    }
    markdown += QStringLiteral("```");

    addMarkdownContent(markdown);
}

void ToolCallDetailWidget::addMarkdownContent(const QString &markdown)
{
    if (markdown.isEmpty())
        return;

    // OPENMV-DIFF //
    // auto *browser = new Utils::MarkdownBrowser(this);
    auto *browser = new LazyMarkdownBrowser(this);
    // OPENMV-DIFF //
    browser->setFrameShape(QFrame::NoFrame);
    browser->setEnableCodeCopyButton(true);
    browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    browser->setMargins({0, 0, 0, 0});
    browser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // Auto-size height on document size changes (content change and reflow)
    connect(browser->document()->documentLayout(),
            &QAbstractTextDocumentLayout::documentSizeChanged,
            browser, [browser] {
        const int h = qCeil(browser->document()->size().height())
                      + browser->contentsMargins().top()
                      + browser->contentsMargins().bottom();
        browser->setFixedHeight(qMax(h, browser->fontMetrics().height()));
    });

    addBodyWidget(browser);
    // OPENMV-DIFF //
    // browser->setMarkdown(markdown);
    browser->setMarkdownLazily(markdown);
    // OPENMV-DIFF //
}

void ToolCallDetailWidget::addTerminalContent(const Terminal &terminal)
{
    auto *label = new QLabel(this);
    label->setTextFormat(Qt::RichText);
    label->setText(QStringLiteral("<code>Terminal: %1</code>")
                       .arg(terminal.terminalId().toHtmlEscaped()));
    addBodyWidget(label);
}

// OPENMV-DIFF //
// A fenced block of plain text, with a fence its text can't close.
static QString fencedText(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text = text.trimmed();
    const QString fence = text.contains(QStringLiteral("```")) ? QStringLiteral("~~~")
                                                               : QStringLiteral("```");
    return fence + QStringLiteral("text\n") + text + QLatin1Char('\n') + fence;
}

// The result of one of the IDE's own tools, which is a JSON object, laid out:
// the terminal output and any traceback as text blocks, the errors as a list,
// the log, then short facts (ok, elapsedMs, ...) on one line and anything else
// as compact JSON. Empty if it isn't such an object.
static QString ideResultMarkdown(const QString &text)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    const QJsonObject result = document.object();

    QStringList parts;
    if (const QString output = result.value("output").toString(); !output.trimmed().isEmpty())
        parts << QStringLiteral("**Output**\n\n") + fencedText(output);
    if (const QString lastError = result.value("lastError").toString(); !lastError.trimmed().isEmpty())
        parts << QStringLiteral("**Error**\n\n") + fencedText(lastError);
    if (const QJsonArray errors = result.value("errors").toArray(); !errors.isEmpty()) {
        QStringList items;
        for (const QJsonValue &value : errors) {
            const QJsonObject item = value.toObject();
            const QString title = item.value("title").toString();
            const QString body = item.value("text").toString().simplified();
            items << QStringLiteral("- ") + (title.isEmpty() ? body
                                                             : QStringLiteral("**") + title
                                                                   + QStringLiteral("**") + (body.isEmpty() ? QString() : QStringLiteral(": ") + body));
        }
        parts << QStringLiteral("**Errors**\n\n") + items.join(QLatin1Char('\n'));
    }
    if (const QJsonArray log = result.value("log").toArray(); !log.isEmpty()) {
        QStringList lines;
        // OPENMV-DIFF //
        // for (const QJsonValue &value : log)
        //     lines << value.toString();
        // One line each, without the site tag ("[auto.5450e740] Connecting..."):
        // a line break inside would leave the italics' asterisks showing.
        static const QRegularExpression siteTag(QStringLiteral("^\\[[\\w.-]+\\]\\s*"));
        for (const QJsonValue &value : log) {
            const QString line = value.toString().remove(siteTag).simplified();
            if (!line.isEmpty())
                lines << line;
        }
        if (!lines.isEmpty())
        // OPENMV-DIFF //
        parts << QStringLiteral("*") + lines.join(QStringLiteral("; ")) + QStringLiteral("*");
    }

    QStringList facts;
    QJsonObject rest;
    for (auto it = result.constBegin(); it != result.constEnd(); ++it) {
        static const QStringList shown = {"output", "lastError", "errors", "log"};
        if (shown.contains(it.key()))
            continue;
        const QJsonValue value = it.value();
        if (value.isBool())
            facts << it.key() + QStringLiteral(": ") + (value.toBool() ? QStringLiteral("yes") : QStringLiteral("no"));
        else if (value.isDouble())
            facts << it.key() + QStringLiteral(": ") + QString::number(value.toDouble());
        else if (value.isString() && value.toString().size() <= 80 && !value.toString().contains('\n'))
            facts << it.key() + QStringLiteral(": ") + value.toString();
        else if (!value.isNull())
            rest.insert(it.key(), value);
    }
    if (!facts.isEmpty())
        parts << facts.join(QStringLiteral(" \u00b7 "));
    if (!rest.isEmpty()) {
        parts << QStringLiteral("```json\n")
                     + QString::fromUtf8(QJsonDocument(rest).toJson(QJsonDocument::Indented)).trimmed()
                     + QStringLiteral("\n```");
    }
    return parts.join(QStringLiteral("\n\n"));
}

// Image blocks in a tool result, as MCP ({type: image, data, mimeType}) or the
// Anthropic API ({type: image, source: {data, media_type}}) put them, at any depth.
void ToolCallDetailWidget::addImagesFrom(const QJsonValue &value)
{
    if (value.isArray()) {
        for (const QJsonValue &item : value.toArray())
            addImagesFrom(item);
        return;
    }
    if (!value.isObject())
        return;
    const QJsonObject object = value.toObject();
    if (object.value("type").toString() == QLatin1String("image")) {
        const QString data = object.value("data").isString()
                                 ? object.value("data").toString()
                                 : object.value("source").toObject().value("data").toString();
        if (!data.isEmpty())
            addImage(data.toLatin1());
        return;
    }
    for (const QString key : {QStringLiteral("content"), QStringLiteral("resource")})
        addImagesFrom(object.value(key));
}

// A rounded thumbnail of an image the tool returned, its size in the tooltip.
void ToolCallDetailWidget::addImage(const QByteArray &encoded)
{
    const QByteArray bytes = QByteArray::fromBase64(encoded);
    const size_t hash = qHash(bytes);
    if (m_imageHashes.contains(hash))
        return;
    QImage image;
    if (!image.loadFromData(bytes))
        return;
    m_imageHashes.insert(hash);

    const qreal dpr = devicePixelRatioF();
    const QImage scaled = image.scaled(qRound(320 * dpr), qRound(240 * dpr),
                                       Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap thumbnail(scaled.size());
    thumbnail.fill(Qt::transparent);
    {
        QPainter p(&thumbnail);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(QPointF(), scaled.size()), RadiusS * dpr, RadiusS * dpr);
        p.setClipPath(clip);
        p.drawImage(0, 0, scaled);
    }
    thumbnail.setDevicePixelRatio(dpr);
    auto *label = new QLabel(this);
    label->setPixmap(thumbnail);
    label->setToolTip(QStringLiteral("%1 \u00d7 %2").arg(image.width()).arg(image.height()));
    label->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    addBodyWidget(label);
    m_latestImage = image;
    m_latestImageWidget = label;
    emit imageFound(image);
}

bool ToolCallDetailWidget::isIdeTool() const
{
    QString server = QLatin1String(Core::Constants::IDE_DISPLAY_NAME);
    server.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    return m_toolName.startsWith(QStringLiteral("mcp__") + server + QStringLiteral("__"));
}
// OPENMV-DIFF //

void ToolCallDetailWidget::addTextContent(const Content &content)
{
    const auto &block = content.content();
    // OPENMV-DIFF //
    // if (auto *textBlock = std::get_if<TextContent>(&block))
    //     addMarkdownContent(textBlock->text());
    if (auto *textBlock = std::get_if<TextContent>(&block)) {
        // Claude's stand-in text for an image ("[Image: source: <file>]"): the
        // image itself is shown (addImagesFrom()).
        static const QRegularExpression imageStandIn(QStringLiteral("^\\[Image: source: [^\\]]*\\]$"));
        if (imageStandIn.match(textBlock->text().trimmed()).hasMatch())
            return;
        const QString laidOut = isIdeTool() ? ideResultMarkdown(textBlock->text()) : QString();
        addMarkdownContent(laidOut.isEmpty() ? textBlock->text() : laidOut);
    } else if (auto *imageBlock = std::get_if<ImageContent>(&block)) {
        addImage(imageBlock->data().toLatin1());
    }
    // OPENMV-DIFF //
}

void ToolCallDetailWidget::addLocations(const QList<ToolCallLocation> &locations)
{
    for (const ToolCallLocation &loc : locations) {
        auto *label = new QLabel(this);
        label->setTextFormat(Qt::RichText);
        label->setCursor(Qt::PointingHandCursor);

        QString display = loc.path();
        if (const auto line = loc.line())
            display += QStringLiteral(":%1").arg(*line);

        label->setText(QStringLiteral("<a href=\"loc://%1:%2\">%3</a>")
                           .arg(loc.path().toHtmlEscaped())
                           .arg(loc.line().value_or(0))
                           .arg(display.toHtmlEscaped()));

        connect(label, &QLabel::linkActivated, this, [](const QString &link) {
            const QUrl url(link);
            const QString path = url.host() + url.path();
            const int line = url.port(0);
            Core::EditorManager::openEditorAt(
                Utils::Link{Utils::FilePath::fromString(path), line});
        });

        addBodyWidget(label);
    }
}

void ToolCallDetailWidget::addRawInputContent(const QJsonValue &rawInput)
{
    if (rawInput.isUndefined() || rawInput.isNull())
        return;

    QString json;
    QString command;
    if (rawInput.isObject()) {
        const QJsonObject inputObject = rawInput.toObject();
        if (inputObject.isEmpty())
            return;
        json = QString::fromUtf8(QJsonDocument(inputObject).toJson(QJsonDocument::Indented));
        if (inputObject.contains("command") && inputObject["command"].isString())
            command = inputObject["command"].toString();
        // OPENMV-DIFF //
        // A script run on the camera: show the script itself (and offer it).
        const bool scriptRun = m_toolName.contains("openmv_script_run")
                               || m_titleLabel->toolTip().contains("openmv_script_run");
        if (scriptRun && inputObject.value("text").isString())
            setScript(inputObject.value("text").toString(), inputObject.value("name").toString());
        // OPENMV-DIFF //
        if (m_titleLabel->text().contains(command))
            command.clear();
    } else if (rawInput.isArray()) {
        const QJsonArray inputArray = rawInput.toArray();
        if (inputArray.isEmpty())
            return;
        json = QString::fromUtf8(QJsonDocument(inputArray).toJson(QJsonDocument::Indented));
    } else if (rawInput.isString()) {
        const QString s = rawInput.toString();
        if (s.isEmpty())
            return;
        json = '"' + s + '"';
    } else {
        json = rawInput.toVariant().toString();
    }
    json = json.trimmed();
    if (json.isEmpty())
        return;

    const QString markdown = QStringLiteral("```json\n%1\n```").arg(json);

    if (!command.isEmpty()) {
        if (!m_commandLabel) {
            m_commandLabel = new QLabel(this);
            m_commandLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            m_commandLabel->setWordWrap(true);
            QFont mono = m_commandLabel->font();
            mono.setStyleHint(QFont::Monospace);
            mono.setFamily(QFont(QStringLiteral("monospace")).defaultFamily());
            m_commandLabel->setFont(mono);
            if (m_contentMaxWidth >= 0)
                m_commandLabel->setMaximumWidth(m_contentMaxWidth);
            m_bodyLayout->insertWidget(0, m_commandLabel);
        }
        m_commandLabel->setText(command);
    }

    if (!m_rawInputContent) {
        auto *frame = new CollapsibleFrame(this); frame->setFrameShape(QFrame::NoFrame);
        frame->setCollapsed(true);

        frame->headerLayout()->addWidget(new QLabel(Tr::tr("Raw Input"), frame), 1);

        // OPENMV-DIFF //
        // auto *browser = new Utils::MarkdownBrowser(frame);
        auto *browser = new LazyMarkdownBrowser(frame);
        // OPENMV-DIFF //
        browser->setFrameShape(QFrame::NoFrame);
        browser->setEnableCodeCopyButton(true);
        browser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        browser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        browser->setMargins({0, 0, 0, 0});
        browser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        connect(browser->document()->documentLayout(),
                &QAbstractTextDocumentLayout::documentSizeChanged,
                browser, [browser] {
            const int h = qCeil(browser->document()->size().height())
                          + browser->contentsMargins().top()
                          + browser->contentsMargins().bottom();
            browser->setFixedHeight(qMax(h, browser->fontMetrics().height()));
        });
        frame->bodyLayout()->addWidget(browser);

        if (m_contentMaxWidth >= 0)
            frame->setMaximumWidth(m_contentMaxWidth);
        const int insertIndex = m_commandLabel ? 1 : 0;
        m_bodyLayout->insertWidget(insertIndex, frame);
        m_rawInputFrame = frame;
        m_rawInputContent = browser;
    }
    // OPENMV-DIFF //
    // m_rawInputContent->setMarkdown(markdown);
    m_rawInputContent->setMarkdownLazily(markdown);
    // OPENMV-DIFF //
}

void ToolCallDetailWidget::addBodyWidget(QWidget *widget)
{
    if (!isCollapsible()) {
        setCollapsible(true);
        setCollapsed(true);
    }
    if (m_contentMaxWidth >= 0)
        widget->setMaximumWidth(m_contentMaxWidth);
    m_bodyLayout->addWidget(widget);
}

} // namespace AcpClient::Internal
