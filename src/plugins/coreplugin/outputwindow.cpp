// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "outputwindow.h"

#include "actionmanager/actionmanager.h"
#include "coreconstants.h"
#include "coreplugintr.h"
#include "editormanager/editormanager.h"
#include "find/basetextfind.h"
#include "icore.h"
#include "messagemanager.h"

#include <aggregation/aggregate.h>

#include <utils/algorithm.h>
#include <utils/fileutils.h>
#include <utils/outputformatter.h>
#include <utils/qtcassert.h>
#include <utils/theme/theme.h>

#include <QAction>
#include <QCursor>
#include <QElapsedTimer>
#include <QHash>
#include <QLoggingCategory>
#include <QMenu>
#include <QMimeData>
#include <QPair>
#include <QPointer>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QTimer>

// OPENMV-DIFF //
#include <utils/fadingindicator.h>
#include <utils/fileutils.h>
#include <extensionsystem/pluginmanager.h>
#include <QApplication>
#include <QRegularExpression>
#include <QSettings>
#include <QFileDialog>
#include <QMessageBox>
// OPENMV-DIFF //

#ifdef WITH_TESTS
#include <QTest>
#endif

#include <numeric>

using namespace Utils;
using namespace std::chrono_literals;

const qsizetype defaultChunkSize = 10000;
const qsizetype minChunkSize = 1000;

const auto defaultInterval = 10ms;
const auto maxInterval = 1000ms;

static Q_LOGGING_CATEGORY(chunkLog, "qtc.core.outputChunking", QtWarningMsg)

namespace Core {
namespace Internal {

class OutputWindowPrivate
{
public:
    explicit OutputWindowPrivate(QTextDocument *document)
        : startOfNewContentCursor(document)
        , cursor(document)
    {
        startOfNewContentCursor.setKeepPositionOnInsert(true);
    }

    qsizetype totalQueuedValue(const std::function<qsizetype(const QString &)> &getValue) const
    {
        return std::accumulate(
            queuedOutput.cbegin(),
            queuedOutput.cend(),
            0,
            [&](qsizetype val, const QPair<QString, OutputFormat> &c) {
                return val + getValue(c.first);
            });
    }

    //: default file name suggested for saving text from output views
    QString outputFileNameHint{::Core::Tr::tr("output.txt")};

    Key settingsKey;
    OutputFormatter formatter;
    QList<QPair<QString, OutputFormat>> queuedOutput;
    QTimer queueTimer;
    QList<qsizetype> queuedSizeHistory;
    int formatterCalls = 0;
    bool discardExcessiveOutput = false;

    bool flushRequested = false;
    bool scrollToBottom = true;
    bool linksActive = true;
    bool zoomEnabled = false;
    float originalFontSize = 0.;
    bool originalReadOnly = false;
    qsizetype maxCharCount = Core::Constants::DEFAULT_MAX_CHAR_COUNT;
    Qt::MouseButton mouseButtonPressed = Qt::NoButton;
    QTextCursor startOfNewContentCursor;
    QTextCursor cursor;
    QString filterText;
    int lastFilteredBlockNumber = -1;
    qsizetype chunkSize = defaultChunkSize;
    QPalette originalPalette;
    OutputWindow::FilterModeFlags filterMode = OutputWindow::FilterModeFlag::Default;
    int beforeContext = 0;
    int afterContext = 0;
    QTimer scrollTimer;
    QElapsedTimer lastMessage;
    QHash<unsigned int, QPair<int, int>> taskPositions;
    // OPENMV-DIFF //
    // (outputFileNameHint is already declared above by upstream 20.0.2.)
    bool enforceNewline = false;
    int tabWidth = 4;
    // OPENMV-DIFF //
};

} // namespace Internal

/*******************/

OutputWindow::OutputWindow(Context context, const Key &settingsKey, QWidget *parent)
    : QPlainTextEdit(parent)
    , d(new Internal::OutputWindowPrivate(document()))
{
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    //setCenterOnScroll(false);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setUndoRedoEnabled(false);
    d->formatter.setPlainTextEdit(this);

    d->queueTimer.setSingleShot(true);
    d->queueTimer.setInterval(defaultInterval);
    connect(&d->queueTimer, &QTimer::timeout, this, &OutputWindow::handleNextOutputChunk);

    d->settingsKey = settingsKey;

    IContext::attach(this, context);

    auto undoAction = new QAction(this);
    auto redoAction = new QAction(this);
    auto cutAction = new QAction(this);
    auto copyAction = new QAction(this);
    auto pasteAction = new QAction(this);
    auto selectAllAction = new QAction(this);

    ActionManager::registerAction(undoAction, Constants::UNDO, context);
    ActionManager::registerAction(redoAction, Constants::REDO, context);
    ActionManager::registerAction(cutAction, Constants::CUT, context);
    ActionManager::registerAction(copyAction, Constants::COPY, context);
    ActionManager::registerAction(pasteAction, Constants::PASTE, context);
    ActionManager::registerAction(selectAllAction, Constants::SELECTALL, context);

    connect(undoAction, &QAction::triggered, this, &QPlainTextEdit::undo);
    connect(redoAction, &QAction::triggered, this, &QPlainTextEdit::redo);
    connect(cutAction, &QAction::triggered, this, &QPlainTextEdit::cut);
    connect(copyAction, &QAction::triggered, this, &QPlainTextEdit::copy);
    connect(pasteAction, &QAction::triggered, this, &QPlainTextEdit::paste);
    connect(selectAllAction, &QAction::triggered, this, &QPlainTextEdit::selectAll);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this] {
        if (shouldFilterNewContentOnBlockCountChanged())
            filterNewContent();
    });

    connect(this, &QPlainTextEdit::undoAvailable, undoAction, &QAction::setEnabled);
    connect(this, &QPlainTextEdit::redoAvailable, redoAction, &QAction::setEnabled);
    connect(this, &QPlainTextEdit::copyAvailable, cutAction, &QAction::setEnabled);  // OutputWindow never read-only
    connect(this, &QPlainTextEdit::copyAvailable, copyAction, &QAction::setEnabled);
    connect(Core::ICore::instance(), &Core::ICore::saveSettingsRequested, this, [this] {
        if (!d->settingsKey.isEmpty())
            Core::ICore::settings()->setValueWithDefault(d->settingsKey, fontZoom(), 0.f);
    });

    connect(outputFormatter(), &OutputFormatter::openInEditorRequested, this, [](const Link &link) {
        EditorManager::openEditorAt(link);
    });

    connect(verticalScrollBar(), &QAbstractSlider::actionTriggered,
            this, &OutputWindow::updateAutoScroll);

    // For when "Find" changes the position; see QTCREATORBUG-26100.
    connect(this, &QPlainTextEdit::selectionChanged, this, &OutputWindow::updateAutoScroll,
            Qt::QueuedConnection);

    undoAction->setEnabled(false);
    redoAction->setEnabled(false);
    cutAction->setEnabled(false);
    copyAction->setEnabled(false);

    d->scrollTimer.setInterval(10ms);
    d->scrollTimer.setSingleShot(true);
    connect(&d->scrollTimer, &QTimer::timeout,
            this, &OutputWindow::scrollToBottom);
    d->lastMessage.start();

    d->originalFontSize = font().pointSizeF();

    if (!d->settingsKey.isEmpty()) {
        float zoom = Core::ICore::settings()->value(d->settingsKey).toFloat();
        setFontZoom(zoom);
    }

    // Let selected text be colored as if the text edit was editable,
    // otherwise the highlight for searching is too light
    QPalette p = palette();
    QColor activeHighlight = p.color(QPalette::Active, QPalette::Highlight);
    p.setColor(QPalette::Highlight, activeHighlight);
    QColor activeHighlightedText = p.color(QPalette::Active, QPalette::HighlightedText);
    p.setColor(QPalette::HighlightedText, activeHighlightedText);
    setPalette(p);

    auto find = new BaseTextFind(this);
    Aggregation::aggregate({this, find});
    // OPENMV-DIFF //
    m_parser = new OpenMVPluginEscapeCodeParser(this);
    // OPENMV-DIFF //
}

OutputWindow::~OutputWindow()
{
    delete d;
}

void OutputWindow::mousePressEvent(QMouseEvent *e)
{
    d->mouseButtonPressed = e->button();
    QPlainTextEdit::mousePressEvent(e);
}

void OutputWindow::handleLink(const QPoint &pos)
{
    const QString href = anchorAt(pos);
    if (!href.isEmpty())
        d->formatter.handleLink(href);
}

void OutputWindow::adaptContextMenu(QMenu *, const QPoint &) {}

void OutputWindow::resetLastFilteredBlockNumber()
{
    d->lastFilteredBlockNumber = -1;
}

bool OutputWindow::shouldFilterNewContentOnBlockCountChanged() const
{
    return !d->filterText.isEmpty();
}

void OutputWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (d->linksActive && d->mouseButtonPressed == Qt::LeftButton)
        handleLink(e->pos());

    // Mouse was released, activate links again
    d->linksActive = true;
    d->mouseButtonPressed = Qt::NoButton;

    QPlainTextEdit::mouseReleaseEvent(e);
}

void OutputWindow::mouseMoveEvent(QMouseEvent *e)
{
    // Cursor was dragged to make a selection, deactivate links
    if (d->mouseButtonPressed != Qt::NoButton && textCursor().hasSelection())
        d->linksActive = false;

    if (!d->linksActive || anchorAt(e->pos()).isEmpty())
        viewport()->setCursor(Qt::IBeamCursor);
    else
        viewport()->setCursor(Qt::PointingHandCursor);
    QPlainTextEdit::mouseMoveEvent(e);
}

void OutputWindow::resizeEvent(QResizeEvent *e)
{
    //Keep scrollbar at bottom of window while resizing, to ensure we keep scrolling
    //This can happen if window is resized while building, or if the horizontal scrollbar appears
    QPlainTextEdit::resizeEvent(e);
    if (d->scrollToBottom)
        scrollToBottom();
}

void OutputWindow::keyPressEvent(QKeyEvent *ev)
{
    QPlainTextEdit::keyPressEvent(ev);

    //Ensure we scroll also on Ctrl+Home or Ctrl+End
    if (ev->matches(QKeySequence::MoveToStartOfDocument))
        verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMinimum);
    else if (ev->matches(QKeySequence::MoveToEndOfDocument))
        verticalScrollBar()->triggerAction(QAbstractSlider::SliderToMaximum);
}

void OutputWindow::setLineParsers(const QList<OutputLineParser *> &parsers)
{
    reset();
    d->formatter.setLineParsers(parsers);
}

OutputFormatter *OutputWindow::outputFormatter() const
{
    return &d->formatter;
}

void OutputWindow::showEvent(QShowEvent *e)
{
    QPlainTextEdit::showEvent(e);
    if (d->scrollToBottom)
        scrollToBottom();
}

void OutputWindow::wheelEvent(QWheelEvent *e)
{
    if (d->zoomEnabled) {
        if (e->modifiers() & Qt::ControlModifier) {
            float delta = e->angleDelta().y() / 120.f;

            // Workaround for QTCREATORBUG-22721, remove when properly fixed in Qt
            const float newSize = float(font().pointSizeF()) + delta;
            if (delta < 0.f && newSize < 4.f)
                return;

            zoomInF(delta);
            emit wheelZoom();
            // OPENMV-DIFF //
            Utils::FadingIndicator::showText(this, Tr::tr("Zoom: %1%").arg(int(100 * (font().pointSizeF() / d->originalFontSize))), Utils::FadingIndicator::SmallText);
            // OPENMV-DIFF //
            return;
        }
    }
    QAbstractScrollArea::wheelEvent(e);
    updateAutoScroll();
    updateMicroFocus();
}

void OutputWindow::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu(event->pos());
    menu->setAttribute(Qt::WA_DeleteOnClose);

    adaptContextMenu(menu, event->pos());

    menu->addSeparator();
    QAction *saveAction = menu->addAction(Tr::tr("Save Contents..."));
    connect(saveAction, &QAction::triggered, this, [this] {
        const FilePath file = FileUtils::getSaveFilePath(
            {}, FileUtils::homePath() / d->outputFileNameHint);
        if (!file.isEmpty()) {
            TextFileFormat format;
            format.setEncoding(EditorManager::defaultTextEncoding());
            format.lineTerminationMode = EditorManager::defaultLineEnding();
            if (const Result<> res = format.writeFile(file, toPlainText()); !res)
                MessageManager::writeDisrupting(res.error());
        }
    });
    saveAction->setEnabled(!document()->isEmpty());
    QAction *openAction = menu->addAction(Tr::tr("Copy Contents to Scratch Buffer"));
    connect(openAction, &QAction::triggered, this, [this] {
        QString scratchBufferPrefix = FilePath::fromString(d->outputFileNameHint).baseName();
        if (scratchBufferPrefix.isEmpty())
            scratchBufferPrefix = "scratch";
        const auto tempPath = FileUtils::scratchBufferFilePath(
            QString::fromUtf8("%1-XXXXXX.txt").arg(scratchBufferPrefix));
        if (!tempPath) {
            MessageManager::writeDisrupting(tempPath.error());
            return;
        }
        IEditor * const editor = EditorManager::openEditor(*tempPath);
        if (!editor) {
            MessageManager::writeDisrupting(
                Tr::tr("Failed to open editor for \"%1\".").arg(tempPath->toUserOutput()));
            return;
        }
        editor->document()->setTemporary(true);
        editor->document()->setContents(toPlainText().toUtf8());
    });
    openAction->setEnabled(!document()->isEmpty());

    menu->addSeparator();
    QAction *clearAction = menu->addAction(Tr::tr("Clear"));
    connect(clearAction, &QAction::triggered, this, [this] { clear(); });
    clearAction->setEnabled(!document()->isEmpty());

    menu->popup(event->globalPos());
}

void OutputWindow::setBaseFont(const QFont &newFont)
{
    float zoom = fontZoom();
    d->originalFontSize = newFont.pointSizeF();
    QFont tmp = newFont;
    float newZoom = qMax(d->originalFontSize + zoom, 4.0f);
    tmp.setPointSizeF(newZoom);
    setFont(tmp);
}

float OutputWindow::fontZoom() const
{
    return font().pointSizeF() - d->originalFontSize;
}

void OutputWindow::setFontZoom(float zoom)
{
    QFont f = font();
    if (f.pointSizeF() == d->originalFontSize + zoom)
        return;
    float newZoom = qMax(d->originalFontSize + zoom, 4.0f);
    f.setPointSizeF(newZoom);
    setFont(f);
}

void OutputWindow::setWheelZoomEnabled(bool enabled)
{
    d->zoomEnabled = enabled;
}

bool OutputWindow::updateFilterProperties(
        const QString &filterText,
        Qt::CaseSensitivity caseSensitivity,
        bool isRegexp,
        bool isInverted,
        int beforeContext,
        int afterContext
        )
{
    FilterModeFlags flags;
    flags.setFlag(FilterModeFlag::CaseSensitive, caseSensitivity == Qt::CaseSensitive)
            .setFlag(FilterModeFlag::RegExp, isRegexp)
            .setFlag(FilterModeFlag::Inverted, isInverted);
    if (d->filterMode == flags
        && d->filterText == filterText
        && d->beforeContext == beforeContext
        && d->afterContext == afterContext)
        return false;
    resetLastFilteredBlockNumber();
    if (d->filterText != filterText) {
        const bool filterTextWasEmpty = d->filterText.isEmpty();
        d->filterText = filterText;

        // Update textedit's background color
        if (filterText.isEmpty() && !filterTextWasEmpty) {
            setPalette(d->originalPalette);
            setReadOnly(d->originalReadOnly);
        }
        if (!filterText.isEmpty() && filterTextWasEmpty) {
            d->originalReadOnly = isReadOnly();
            setReadOnly(true);
            const auto newBgColor = [this] {
                const QColor currentColor = palette().color(QPalette::Base);
                const int factor = 120;
                return currentColor.value() < 128 ? currentColor.lighter(factor)
                                                  : currentColor.darker(factor);
            };
            QPalette p = palette();
            p.setColor(QPalette::Base, newBgColor());
            setPalette(p);
        }
    }
    d->filterMode = flags;
    d->beforeContext = beforeContext;
    d->afterContext = afterContext;
    filterNewContent();
    return true;
}

void OutputWindow::setOutputFileNameHint(const QString &fileName)
{
    d->outputFileNameHint = fileName;
}

OutputWindow::TextMatchingFunction OutputWindow::makeMatchingFilterFunction() const
{
    const bool normal = !d->filterMode.testFlag(FilterModeFlag::Inverted)
                        && !d->filterText.isEmpty();

    if (d->filterText.isEmpty()) {
        return [](const QString &) { return true; };
    } else if (d->filterMode.testFlag(OutputWindow::FilterModeFlag::RegExp)) {
        QRegularExpression regExp(d->filterText);
        if (!d->filterMode.testFlag(OutputWindow::FilterModeFlag::CaseSensitive))
            regExp.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
        if (!regExp.isValid())
            return [](const QString &) { return false; };

        return [regExp, normal](const QString &text) { return regExp.match(text).hasMatch() == normal; };
    } else {
        const auto cs = d->filterMode.testFlag(OutputWindow::FilterModeFlag::CaseSensitive)
                            ? Qt::CaseSensitive : Qt::CaseInsensitive;

        return [cs, filterText = d->filterText, normal](const QString &text) {
            return text.contains(filterText, cs) == normal;
        };
    }

    return {};
}

void OutputWindow::filterNewContent()
{
    const auto findNextMatchFilter = makeMatchingFilterFunction();
    QTC_ASSERT(findNextMatchFilter, return);
    const int requiredBacklog = std::max(d->beforeContext, d->afterContext);
    const int firstBlockIndex = d->lastFilteredBlockNumber - requiredBacklog;

    std::vector<int> matchedBlocks;
    QTextBlock lastBlock = document()->findBlockByNumber(firstBlockIndex);
    if (!lastBlock.isValid())
        lastBlock = document()->begin();

    // Find matching text blocks for the current filter.
    for (; lastBlock != document()->end(); lastBlock = lastBlock.next()) {
        const bool isMatch = findNextMatchFilter(lastBlock.text());

        if (isMatch)
            matchedBlocks.emplace_back(lastBlock.blockNumber());

        lastBlock.setVisible(isMatch);
    }

    // Reveal the context lines before and after the match.
    if (!d->filterText.isEmpty()) {
        for (int blockNumber : matchedBlocks) {
            for (auto i = 1; i <= d->beforeContext; ++i)
                document()->findBlockByNumber(blockNumber - i).setVisible(true);
            for (auto i = 1; i <= d->afterContext; ++i)
                document()->findBlockByNumber(blockNumber + i).setVisible(true);
        }
    }

    d->lastFilteredBlockNumber = document()->lastBlock().blockNumber();

    // FIXME: Why on earth is this necessary? We should probably do something else instead...
    setDocument(document());

    if (d->scrollToBottom)
        scrollToBottom();
}

void OutputWindow::handleNextOutputChunk()
{
    QTC_ASSERT(!d->queuedOutput.isEmpty(), return);

    discardExcessiveOutput();
    if (d->queuedOutput.isEmpty())
        return;

    auto &chunk = d->queuedOutput.first();

    // We want to break off the chunks along line breaks, if possible.
    // Otherwise we can get ugly temporary artifacts e.g. for ANSI escape codes.
    qsizetype actualChunkSize = std::min(d->chunkSize, chunk.first.size());
    const qsizetype minEndPos = std::max(qsizetype(0), actualChunkSize - 1000);
    for (int i = actualChunkSize - 1; i >= minEndPos; --i) {
        if (chunk.first.at(i) == '\n') {
            actualChunkSize = i + 1;
            break;
        }
    }

    qCDebug(chunkLog) << "next queued chunk has" << chunk.first.size() << "bytes";
    if (actualChunkSize == chunk.first.size()) {
        qCDebug(chunkLog) << "chunk can be written in one go";
        handleOutputChunk(chunk.first, chunk.second, ChunkCompleteness::Complete);
        d->queuedOutput.removeFirst();
    } else {
        qCDebug(chunkLog) << "chunk needs to be split";
        handleOutputChunk(chunk.first.left(actualChunkSize), chunk.second, ChunkCompleteness::Split);
        chunk.first.remove(0, actualChunkSize);
    }
    if (!d->queuedOutput.isEmpty())
        d->queueTimer.start();
    else if (d->flushRequested) {
        d->formatter.flush();
        d->flushRequested = false;
    }
}

void OutputWindow::handleOutputChunk(
    const QString &output, OutputFormat format, ChunkCompleteness completeness)
{
    QString out = output;
    int maxBlockCount = -1;
    if (out.size() > d->maxCharCount) {
        // Current chunk alone exceeds limit, we need to cut it.
        const qsizetype elided = out.size() - d->maxCharCount;
        out = out.left(d->maxCharCount / 2)
                + "[[[... "
                + Tr::tr("Elided %n characters due to settings limit", nullptr, elided)
                + " ...]]]"
                + out.right(d->maxCharCount / 2);
        maxBlockCount = out.count('\n') + 1;
    } else {
        qsizetype plannedChars = document()->characterCount() + out.size();
        if (plannedChars > d->maxCharCount) {
            maxBlockCount = document()->blockCount();
            QTextBlock tb = document()->firstBlock();
            while (tb.isValid() && plannedChars > d->maxCharCount && maxBlockCount > 1) {
                plannedChars -= tb.length();
                maxBlockCount -= 1;
                tb = tb.next();
            }
        }
    }
    qCDebug(chunkLog) << "new max block count:" << maxBlockCount;
    setMaximumBlockCount(maxBlockCount);

    QElapsedTimer formatterTimer;
    formatterTimer.start();
    d->formatter.appendMessage(out, format);
    ++d->formatterCalls;
    qCDebug(chunkLog) << "formatter took" << formatterTimer.elapsed() << "ms";
    if (formatterTimer.elapsed() > d->queueTimer.interval()) {
        d->queueTimer.setInterval(std::min(maxInterval, d->queueTimer.intervalAsDuration() * 2));
        d->chunkSize = std::max(minChunkSize, d->chunkSize / 2);
        qCDebug(chunkLog) << "increasing interval to" << d->queueTimer.interval()
                          << "ms and lowering chunk size to" << d->chunkSize << "bytes";
    } else if (completeness == ChunkCompleteness::Split
               && formatterTimer.elapsed() < d->queueTimer.interval() / 2) {
        d->queueTimer.setInterval(std::max(1ms, d->queueTimer.intervalAsDuration() * 2 / 3));
        d->chunkSize = d->chunkSize * 1.5;
        qCDebug(chunkLog) << "lowering interval to" << d->queueTimer.interval()
                          << "ms and increasing chunk size to" << d->chunkSize << "bytes";
    }

    if (d->scrollToBottom) {
        if (d->lastMessage.elapsed() < 5) {
            d->scrollTimer.start();
        } else {
            d->scrollTimer.stop();
            scrollToBottom();
        }
    }

    d->lastMessage.start();
    enableUndoRedo();
}

void OutputWindow::discardExcessiveOutput()
{
    // Unless the user instructs us to, we do not mess with the output.
    if (!d->discardExcessiveOutput)
        return;

    // Criterion 1: Are we being flooded?
    // If the pending output has been growing for the last ten times the output formatter
    // was invoked and it is considerably larger than the chunk size, we discard it.
    const qsizetype queuedSize = totalQueuedSize();
    if (!d->queuedSizeHistory.isEmpty() && d->queuedSizeHistory.last() > queuedSize)
        d->queuedSizeHistory.clear();
    d->queuedSizeHistory << queuedSize;
    bool discard = d->queuedSizeHistory.size() > int(10) && queuedSize > 5 * d->chunkSize;
    if (discard)
        qCDebug(chunkLog) << "discarding output due to size";

    // Criterion 2: Are we too slow?
    // If it would take longer than a minute to print the pending output and we have
    // already presented a reasonable amount of output to the user, we discard it.
    if (!discard) {
        discard = d->formatterCalls >= 10
                  && (queuedSize / d->chunkSize) * d->queueTimer.intervalAsDuration() > 60s;
        if (discard)
            qCDebug(chunkLog) << "discarding output due to time";
    }

    if (discard) {
        discardPendingToolOutput();
        d->queuedSizeHistory.clear();
        return;
    }
}

void OutputWindow::discardPendingToolOutput()
{
    Utils::erase(d->queuedOutput, [](const std::pair<QString, OutputFormat> &chunk) {
        return chunk.second != NormalMessageFormat && chunk.second != ErrorMessageFormat;
    });
    d->formatter.appendMessage(Tr::tr("[Discarding excessive amount of pending output.]\n"),
                               ErrorMessageFormat);
    emit outputDiscarded();
}

void OutputWindow::updateAutoScroll()
{
    d->scrollToBottom = verticalScrollBar()->sliderPosition() >= verticalScrollBar()->maximum() - 1;
}

qsizetype OutputWindow::totalQueuedSize() const
{
    return d->totalQueuedValue([](const QString &s) { return s.size(); });
}

qsizetype OutputWindow::totalQueuedLines() const
{
    return d->totalQueuedValue([](const QString &s) { return s.count('\n'); });
}

void OutputWindow::setMaxCharCount(qsizetype count)
{
    d->maxCharCount = count;
    setMaximumBlockCount(count / 100);
}

qsizetype OutputWindow::maxCharCount() const
{
    return d->maxCharCount;
}

void OutputWindow::appendMessage(const QString &output, OutputFormat format)
{
    if (d->queuedOutput.isEmpty() || d->queuedOutput.last().second != format)
        d->queuedOutput.push_back({output, format});
    else
        d->queuedOutput.last().first.append(output);
    if (!d->queueTimer.isActive())
        d->queueTimer.start();
}

void OutputWindow::registerPositionOf(unsigned taskId, int linkedOutputLines, int skipLines,
                                      int offset, TaskSource taskSource)
{
    if (linkedOutputLines <= 0)
        return;

    // For Tasks that result from an OutputLineParser, the corresponding content is the last
    // one written to the text edit, otherwise it's the last queued output.
    const int extraLines = taskSource == TaskSource::Parsed ? 0 : totalQueuedLines();

    const int blocknumber = document()->blockCount() - offset;

    // -1 because OutputFormatter has already added the newline.
    const int firstLine = blocknumber - linkedOutputLines - skipLines - 1 + extraLines;

    const int lastLine = firstLine + linkedOutputLines - 1;

    d->taskPositions.insert(taskId, {firstLine, lastLine});
}

bool OutputWindow::knowsPositionOf(unsigned taskId) const
{
    return d->taskPositions.contains(taskId);
}

void OutputWindow::showPositionOf(unsigned taskId)
{
    QPair<int, int> position = d->taskPositions.value(taskId);
    QTextCursor newCursor(document()->findBlockByNumber(position.second));

    // Move cursor to end of last line of interest:
    newCursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);
    setTextCursor(newCursor);

    // Move cursor and select lines:
    newCursor.setPosition(document()->findBlockByNumber(position.first).position(),
                          QTextCursor::KeepAnchor);
    setTextCursor(newCursor);

    // Center cursor now:
    centerCursor();
}

QMimeData *OutputWindow::createMimeDataFromSelection() const
{
    const auto mimeData = new QMimeData;
    QString content;
    const int selStart = textCursor().selectionStart();
    const int selEnd = textCursor().selectionEnd();
    const QTextBlock firstBlock = document()->findBlock(selStart);
    const QTextBlock lastBlock = document()->findBlock(selEnd);
    for (QTextBlock curBlock = firstBlock; curBlock != lastBlock; curBlock = curBlock.next()) {
        if (!curBlock.isVisible())
            continue;
        if (curBlock == firstBlock)
            content += curBlock.text().mid(selStart - firstBlock.position());
        else
            content += curBlock.text();
        content += '\n';
    }
    if (lastBlock.isValid() && lastBlock.isVisible()) {
        if (firstBlock == lastBlock)
            content = textCursor().selectedText();
        else
            content += lastBlock.text().mid(0, selEnd - lastBlock.position());
    }
    mimeData->setText(content);
    return mimeData;
}

void OutputWindow::clear()
{
    d->formatter.clear();
    d->scrollToBottom = true;
    d->taskPositions.clear();
    d->startOfNewContentCursor.setPosition(0);
}

void OutputWindow::clearLinesPrefixedWith(const QString& prefix, bool deleteTrailingLineBreak)
{
    QTextDocument *doc = document();

    auto block = doc->lastBlock();
    while (true) {
        if (block.text().startsWith(prefix)) {
            QTextCursor c(block);
            c.select(QTextCursor::BlockUnderCursor);
            c.removeSelectedText();
            if (deleteTrailingLineBreak)
                c.deleteChar();
        }
        if (block == doc->firstBlock())
            break;
        block = block.previous();
    }
}

void OutputWindow::flush()
{
    if (totalQueuedSize() > 5 * d->chunkSize) {
        d->flushRequested = true;
        return;
    }
    d->queueTimer.stop();
    for (const auto &chunk : std::as_const(d->queuedOutput))
        handleOutputChunk(chunk.first, chunk.second, ChunkCompleteness::Complete);
    d->queuedOutput.clear();
    d->formatter.flush();
}

void OutputWindow::reset()
{
    flush();
    if (!d->queuedOutput.isEmpty()) {
        discardPendingToolOutput();
        flush();

        // For the unlikely case that we ourselves have sent excessive amount of output
        // via NormalMessageFormat or ErrorMessageFormat.
        d->queuedOutput.clear();
    }
    d->queueTimer.stop();
    d->queuedSizeHistory.clear();
    d->formatter.reset();
    d->formatterCalls = 0;
    d->scrollToBottom = true;
    d->flushRequested = false;
}

void OutputWindow::scrollToBottom()
{
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    // QPlainTextEdit destroys the first calls value in case of multiline
    // text, so make sure that the scroll bar actually gets the value set.
    // Is a noop if the first call succeeded.
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

void OutputWindow::grayOutOldContent()
{
    // OPENMV-DIFF //
    if(document()->isEmpty())
        return;
    d->enforceNewline = true;
    // Follow the sticky user-intent flag, not a fresh value()==maximum() check
    // that fast continuous output keeps re-pinning so you can never scroll up.
    const bool atBottom = d->scrollToBottom;
    // OPENMV-DIFF //
    if (!d->cursor.atEnd())
        d->cursor.movePosition(QTextCursor::End);
    QTextCharFormat endFormat = d->cursor.charFormat();

    d->cursor.setPosition(d->startOfNewContentCursor.position());
    d->cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor, 1);

    QTextCharFormat format;
    const QColor bkgColor = palette().base().color();
    const QColor fgdColor = palette().text().color();
    double bkgFactor = 0.50;
    double fgdFactor = 1.-bkgFactor;
    format.setForeground(QColor((bkgFactor * bkgColor.red() + fgdFactor * fgdColor.red()),
                             (bkgFactor * bkgColor.green() + fgdFactor * fgdColor.green()),
                             (bkgFactor * bkgColor.blue() + fgdFactor * fgdColor.blue()) ));
    d->cursor.mergeCharFormat(format);

    d->cursor.movePosition(QTextCursor::End);
    d->cursor.setCharFormat(endFormat);
    d->startOfNewContentCursor.setPosition(d->cursor.position());
    d->cursor.insertBlock(QTextBlockFormat());
    // OPENMV-DIFF //
    if (atBottom)
        scrollToBottom();
    // OPENMV-DIFF //
}

void OutputWindow::enableUndoRedo()
{
    setMaximumBlockCount(0);
    setUndoRedoEnabled(true);
}

void OutputWindow::setWordWrapEnabled(bool wrap)
{
    if (wrap)
        setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    else
        setWordWrapMode(QTextOption::NoWrap);
}

void OutputWindow::setDiscardExcessiveOutput(bool discard)
{
    d->discardExcessiveOutput = discard;
}

// OPENMV-DIFF //
QString OutputWindow::doNewlineEnforcement(const QString &out)
{
    // Do NOT re-pin d->scrollToBottom = true here. appendText() follows that
    // sticky user-intent flag, so setting it true on every chunk yanked a
    // scrolled-up user back to the tail under continuous output.
    QString s = out;
    if (d->enforceNewline) {
        s.prepend(QLatin1Char('\n'));
        d->enforceNewline = false;
    }

    if (s.endsWith(QLatin1Char('\n'))) {
        d->enforceNewline = true; // make appendOutputInline put in a newline next time
        s.chop(1);
    }

    return s;
}

// OPENMV-DIFF //
// Some emoji (e.g. the Send arrow U+27A1, in the Dingbats block) are also covered
// by monospace code fonts as a plain text glyph, so Qt's font fallback picks that
// over the color emoji font and they render as mono symbols. Render emoji runs with
// the platform emoji font explicitly so they always show as color glyphs.
static QString emojiFontFamily()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Segoe UI Emoji");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("Apple Color Emoji");
#else
    return QStringLiteral("Noto Color Emoji");
#endif
}

static bool isEmojiCodepoint(uint cp)
{
    return ((cp >= 0x1F000) && (cp <= 0x1FAFF))   // supplementary emoji blocks
        || ((cp >= 0x2600) && (cp <= 0x27BF))     // misc symbols + dingbats (✅ ⚠ ➡)
        || ((cp >= 0x2B00) && (cp <= 0x2BFF));    // misc symbols and arrows (⬅)
}

static uint codepointAt(const QString &text, int i, int &len)
{
    const QChar c = text.at(i);
    if (c.isHighSurrogate() && ((i + 1) < text.size()) && text.at(i + 1).isLowSurrogate()) {
        len = 2;
        return QChar::surrogateToUcs4(c, text.at(i + 1));
    }
    len = 1;
    return c.unicode();
}

static bool textHasEmoji(const QString &text)
{
    for (int i = 0, len = 0; i < text.size(); i += len)
        if (isEmojiCodepoint(codepointAt(text, i, len)))
            return true;
    return false;
}

// Insert text splitting it into emoji / non-emoji runs, forcing the emoji font on
// emoji runs. VS16 (U+FE0F) and ZWJ (U+200D) continue an emoji run.
static void insertTextEmojiAware(QTextCursor &cursor, const QString &text, const QTextCharFormat &baseFormat)
{
    const int n = text.size();
    int i = 0, len = 0;
    while (i < n) {
        const bool emoji = isEmojiCodepoint(codepointAt(text, i, len));
        const int runStart = i;
        i += len;
        while (i < n) {
            const uint cp = codepointAt(text, i, len);
            bool e = isEmojiCodepoint(cp) || (emoji && ((cp == 0xFE0F) || (cp == 0x200D)));
            if (e != emoji)
                break;
            i += len;
        }
        QTextCharFormat f = baseFormat;
        if (emoji)
            f.setFontFamilies(QStringList{emojiFontFamily()});
        cursor.insertText(text.mid(runStart, i - runStart), f);
    }
}
// OPENMV-DIFF //

void OutputWindow::appendText(const QString &textIn, bool ensureLineStart)
{
    // OPENMV-DIFF //
    // Protocol/debug lines share this stream with the camera's serial output. If the
    // camera left a dangling partial line (e.g. the ">>> " REPL prompt with no trailing
    // newline), force the incoming line onto a fresh line instead of appending to it.
    // Keyed on the source (the debug sink passes ensureLineStart), never on content, so
    // user scripts printing emojis mid-line are left alone.
    //
    // The terminal defers a trailing newline (d->enforceNewline) rather than emitting it
    // immediately, so the cursor sits at the end of the previous line's text even when
    // logically at a line start. Treat a pending deferred newline as line-start, and
    // request the break via that same flag (not a raw '\n' insert) so we don't double up
    // with the deferred newline and leave a blank line between rows.
    if (ensureLineStart)
    {
        if (!d->cursor.atEnd())
            d->cursor.movePosition(QTextCursor::End);
        if (!d->enforceNewline && !d->cursor.atBlockStart())
            d->enforceNewline = true;
    }
    // OPENMV-DIFF //
    for(const Utils::FormattedText &parsedText : m_handler.parseText(Utils::FormattedText(textIn)))
    {
        if(parsedText.text.isEmpty())
        {
            m_parser->parseEscapeCodes(m_handler.getEscapeCodes());
        }

        QTextCharFormat format = parsedText.format;
        QString text = QString(parsedText.text).replace(QLatin1String("\r\n"), QLatin1String("\n"));
        int index = text.indexOf(QStringLiteral("Traceback (most recent call last):\n"));

        if(index != -1)
        {
            m_handler.endFormatScope();
            m_parser->resetParser();
            appendText(text.left(index));
            grayOutOldContent();
            format = QTextCharFormat();
            text = text.mid(index);
        }

        text = text.replace(QStringLiteral("MPY: soft reboot\n"), QStringLiteral(""));
        text = text.replace(QStringLiteral("Exception: IDE interrupt\n"), QStringLiteral(""));
        text = text.replace(QStringLiteral("KeyboardInterrupt: \n"), QStringLiteral(""));

        QChar lastChar = QChar();
        QString string;
        int column = 0;

        for(int i = 0, j = text.size(); i < j; i++)
        {
            switch(text.at(i).unicode())
            {
                case 15:
                case 17: // XON
                case 19: // XOFF
                case 23:
                case 24:
                case 25:
                case 26:
                case 27: // Escape - AnsiEscapeCodeHandler
                case 28:
                case 29:
                case 30:
                case 31:
                {
                    break;
                }

                case 0: // Null
                {
                    break;
                }

                case 1: // Home Cursor
                {
                    break;
                }

                case 2: // Move Cursor Left
                {
                    break;
                }

                case 3: // Clear Screen
                {
                    break;
                }

                case 4:
                case 127: // Delete
                {
                    break;
                }

                case 5: // End Cursor
                {
                    break;
                }

                case 6: // Move Cursor Right
                {
                    break;
                }

                case 7: // Beep Speaker
                {
                    QApplication::beep();
                    break;
                }

                case 8: // Backspace
                {
                    break;
                }

                case 9: // Tab
                {
                    for(int k = d->tabWidth - (column % d->tabWidth); k > 0; k--)
                    {
                        string.append(QLatin1Char(' '));
                        column += 1;
                    }

                    break;
                }

                case 10: // Line Feed
                {
                    if(lastChar.unicode() != '\r')
                    {
                        string.append(QLatin1Char('\n'));
                        column = 0;
                    }

                    break;
                }

                case 11: // Clear to end of line.
                {
                    break;
                }

                case 12: // Clear lines below.
                {
                    break;
                }

                case 13: // Carriage Return
                {
                    string.append(QLatin1Char('\n'));
                    column = 0;
                    break;
                }

                case 14: // Move Cursor Down
                {
                    break;
                }

                case 16: // Move Cursor Up
                {
                    break;
                }

                case 18: // Move to start of line.
                {
                    break;
                }

                case 20: // Move to end of line.
                {
                    break;
                }

                case 21: // Clear to start of line.
                {
                    break;
                }

                case 22: // Clear lines above.
                {
                    break;
                }

                default:
                {
                    string.append(text.at(i));
                    column += 1;
                    break;
                }
            }

            lastChar = text.at(i);
        }

        if(d->cursor.document()->isEmpty())
        {
            string.remove(QRegularExpression(QStringLiteral("^\\s+")));
        }

        text = m_parser->parseText(string);

        if(text.isEmpty())
        {
            continue;
        }

        // Follow the sticky user-intent flag, not a fresh value()==maximum() check
    // that fast continuous output keeps re-pinning so you can never scroll up.
    const bool atBottom = d->scrollToBottom;
        if (!d->cursor.atEnd())
            d->cursor.movePosition(QTextCursor::End);
        d->cursor.beginEditBlock();
        // OPENMV-DIFF //
        // d->cursor.insertText(doNewlineEnforcement(text), format);
        const QString enforced = doNewlineEnforcement(text);
        if (textHasEmoji(enforced))
            insertTextEmojiAware(d->cursor, enforced, format);
        else
            d->cursor.insertText(enforced, format);
        // OPENMV-DIFF //

        d->cursor.endEditBlock();
        if (atBottom)
            scrollToBottom();
    }
}

bool OutputWindow::isScrollbarAtBottom() const
{
    return verticalScrollBar()->value() == verticalScrollBar()->maximum();
}

static const char settingsGroup[] = "OutputWindow";
static const char saveLogFilePath[] = "SaveLogFilePath";

void OutputWindow::save()
{
    QtcSettings *settings = ICore::settings();
    // OPENMV-DIFF //
    // settings->beginGroup(settingsGroup);
    const Utils::Key logKey = Utils::Key(settingsGroup) + '/' + Utils::Key(saveLogFilePath);
    // OPENMV-DIFF //

    QString path =
        QFileDialog::getSaveFileName(Core::ICore::dialogParent(), Tr::tr("Save Log"),
            // OPENMV-DIFF //
            // settings->value(saveLogFilePath, QDir::homePath()).toString(),
            settings->value(logKey, QDir::homePath()).toString(),
            // OPENMV-DIFF //
            Tr::tr("Text Files (*.txt);;All files (*)"));

    if(!path.isEmpty())
    {
        Utils::FileSaver file(Utils::FilePath::fromString(path));

        if(!file.hasError())
        {
            if((!file.write(toPlainText().toUtf8())) || (!file.finalize()))
            {
                QMessageBox::critical(Core::ICore::dialogParent(),
                    Tr::tr("Save Log"),
                    Tr::tr("Error: %L1!").arg(file.errorString()));
            }
            else
            {
                // OPENMV-DIFF //
                // settings->setValue(saveLogFilePath, path);
                settings->setValue(logKey, path);
                // OPENMV-DIFF //
            }
        }
        else
        {
            QMessageBox::critical(Core::ICore::dialogParent(),
                Tr::tr("Save Log"),
                Tr::tr("Error: %L1!").arg(file.errorString()));
        }
    }
    // OPENMV-DIFF //
    // settings->endGroup();
    // OPENMV-DIFF //
}

void OutputWindow::setTabSettings(int tabWidth)
{
    d->tabWidth = tabWidth;
}

void OutputWindow::setSerialDebugLevel(int level)
{
    if (m_serialDebugLevel != level) {
        m_serialDebugLevel = level;
        emit serialDebugLevelChanged(level);
    }
}
// OPENMV-DIFF //
// OBSOLETE: OpenMV's 14.0.2 base kept the TestFormatterA/TestFormatterB
// WITH_TESTS classes inline here. Upstream 20.0.2 relocated those output-window
// tests out of this translation unit, so the stale copies are dropped.
// OPENMV-DIFF //

} // namespace Core
