// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acpmessageview.h"

#include "acpclienttr.h"
#include "acpsettings.h"
#include "collapsibleframe.h"
#include "sessionpickerwidget.h"
#include "toolcalldetailwidget.h"

#include <utils/async.h>
#include <aggregation/aggregate.h>

#include <coreplugin/find/ifindsupport.h>
#include <coreplugin/icore.h>

#include <utils/algorithm.h>
#include <utils/elidinglabel.h>
#include <utils/layoutbuilder.h>
#include <utils/markdownbrowser.h>
#include <utils/progressindicator.h>
#include <utils/qtcassert.h>
#include <utils/qtcwidgets.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>
// OPENMV-DIFF //
#include <utils/fsengine/fileiconprovider.h>
#include <utils/stringutils.h>

#include <QAbstractButton>
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QScopedValueRollback>
#include <QMenu>
// OPENMV-DIFF //

#include <limits>

#include <QAbstractTextDocumentLayout>
#include <QComboBox>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QtMath>
#include <QPushButton>
#include <QResizeEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QScrollBar>
#include <QTextDocument>
// OPENMV-DIFF //
#include <QTextEdit>
// OPENMV-DIFF //
#include <QTimer>
#include <QVBoxLayout>
#include <QElapsedTimer>

using namespace Acp;
using namespace Utils::StyleHelper::SpacingTokens;

namespace AcpClient::Internal {

// ---------------------------------------------------------------------------
// AvatarWidget — circular avatar for agent messages
// ---------------------------------------------------------------------------

class AvatarWidget : public QWidget
{
public:
    explicit AvatarWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(28, 28);
    }

    void setIcon(const QIcon &icon) { m_icon = icon; update(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(2, 2, -2, -2);

        if (!m_icon.isNull()) {
            // OPENMV-DIFF //
            // m_icon.paint(&p, r.toAlignedRect());
            // Tinted with the theme's muted text color: the icons come colored for
            // the panels (black on light themes), too heavy beside the messages.
            const qreal dpr = devicePixelRatioF();
            QPixmap tinted = m_icon.pixmap(r.size().toSize(), dpr);
            {
                QPainter tint(&tinted);
                tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
                tint.fillRect(tinted.rect(), Utils::creatorColor(Utils::Theme::Token_Text_Muted));
            }
            p.drawPixmap(r.toAlignedRect(), tinted);
            // OPENMV-DIFF //
        } else {
            QRadialGradient gradient(r.center(), r.width() / 2.0);
            gradient.setColorAt(0, QColor(0x5e, 0xea, 0x8d));
            gradient.setColorAt(0.6, QColor(0x22, 0xc5, 0x5e));
            gradient.setColorAt(1.0, QColor(0x16, 0xa3, 0x4a));
            p.setBrush(gradient);
            p.setPen(Qt::NoPen);
            p.drawEllipse(r);
        }
    }

private:
    QIcon m_icon;
};

// ---------------------------------------------------------------------------
// UserMessageWidget — bubble for user messages, plain text via QLabel
// ---------------------------------------------------------------------------

// OPENMV-DIFF //
// Text views paint an opaque background in the color behind them: transparent
// ones lose Windows' sharp (ClearType) text smoothing and look fuzzy.
static void setOpaqueBackground(QAbstractScrollArea *view, const QColor &color)
{
    QPalette pal = view->palette();
    pal.setColor(QPalette::Base, color);
    view->setPalette(pal);
    view->setAutoFillBackground(false);
    view->viewport()->setAutoFillBackground(true);
}

// The color the chat is drawn on: that of the nearest ancestor that paints one.
static QColor chatBackground(QWidget *widget)
{
    for (QWidget *w = widget; w; w = w->parentWidget()) {
        if (w->autoFillBackground())
            return w->palette().color(w->backgroundRole());
    }
    return widget ? widget->palette().color(QPalette::Window) : QColor();
}

// Message text: on light themes the OpenMV views' (frame buffer, histogram...)
// text color, softer than the near-black default; the default on dark themes.
static QColor chatTextColor()
{
    return Utils::creatorColor(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface)
                                   ? Utils::Theme::Token_Text_Default
                                   : Utils::Theme::TextColorNormal);
}

// The user's bubble: the theme's foreground on dark themes, its lighter
// "subtle" one on light themes (the default was a heavy grey there).
static QColor userBubbleColor()
{
    return Utils::creatorColor(Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface)
                                   ? Utils::Theme::Token_Foreground_Default
                                   : Utils::Theme::Token_Foreground_Subtle);
}
// OPENMV-DIFF //

// OPENMV-DIFF //
// The text in a user bubble. Unlike a QLabel it also wraps long unbroken runs
// (URLs, paths, keyboard mashing) instead of cutting them off, and stays
// selectable, copying the exact text.
class BubbleText : public QTextEdit
{
public:
    explicit BubbleText(QWidget *parent)
        : QTextEdit(parent)
    {
        setReadOnly(true);
        setFrameShape(QFrame::NoFrame);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setLineWrapMode(QTextEdit::WidgetWidth);
        setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumWidth(1);
        setContentsMargins(0, 0, 0, 0);
        document()->setDocumentMargin(0);
        QPalette pal = palette();
        pal.setColor(QPalette::Text, chatTextColor());
        setPalette(pal);
        setOpaqueBackground(this, userBubbleColor());
        connect(document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this](const QSizeF &size) {
            setFixedHeight(qMax(qCeil(size.height()), fontMetrics().height()));
        });
    }
};
// OPENMV-DIFF //

// OPENMV-DIFF //
// Copies a message: a copy icon, a check mark for a moment after copying.
class CopyMessageButton : public QAbstractButton
{
public:
    explicit CopyMessageButton(const std::function<QString()> &text, QWidget *parent)
        : QAbstractButton(parent)
    {
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(Tr::tr("Copy"));
        setFixedSize(22, 22);
        connect(this, &QAbstractButton::clicked, this, [this, text] {
            Utils::setClipboardAndSelection(text());
            m_copied = true;
            update();
            QTimer::singleShot(1500, this, [this] {
                m_copied = false;
                update();
            });
        });
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (underMouse()) {
            QColor c = Utils::creatorColor(Utils::Theme::TextColorDisabled);
            c.setAlphaF(c.alphaF() * .5);
            Utils::StyleHelper::drawCardBg(&p, rect(), c, Qt::NoPen, RadiusS);
        }
        const QIcon icon = m_copied ? Utils::Icons::OK.icon() : Utils::Icons::COPY.icon();
        icon.paint(&p, rect().adjusted(3, 3, -3, -3), Qt::AlignCenter);
    }

private:
    bool m_copied = false;
};

// A message with its actions (copy) under it, shown while the mouse is over it.
// Their space stays reserved so nothing moves when they appear.
class MessageRow : public QWidget
{
public:
    explicit MessageRow(QWidget *parent)
        : QWidget(parent)
    {}

    void setActions(QWidget *actions)
    {
        m_actions = actions;
        QSizePolicy policy = actions->sizePolicy();
        policy.setRetainSizeWhenHidden(true);
        actions->setSizePolicy(policy);
        actions->hide();
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        if (m_actions)
            m_actions->show();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent *event) override
    {
        if (m_actions)
            m_actions->hide();
        QWidget::leaveEvent(event);
    }

private:
    QWidget *m_actions = nullptr;
};

// A file or folder attached to a user message: its icon and name; the full path
// in the tooltip.
class FileChip : public QWidget
{
public:
    FileChip(const Utils::FilePath &file, QWidget *parent)
        : QWidget(parent)
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(PaddingHS, PaddingVXs, PaddingHM, PaddingVXs);
        layout->setSpacing(GapHXs);
        auto *icon = new QLabel(this);
        icon->setPixmap(Utils::FileIconProvider::icon(file).pixmap(16, 16));
        layout->addWidget(icon);
        auto *name = new QLabel(file.isDir() ? file.fileName() + '/' : file.fileName(), this);
        layout->addWidget(name);
        setToolTip(file.toUserOutput());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Utils::StyleHelper::drawCardBg(&p, rect(),
                                       Utils::creatorColor(Utils::Theme::Token_Foreground_Subtle),
                                       Qt::NoPen, RadiusS);
    }
};
// OPENMV-DIFF //

class UserMessageWidget : public QFrame
{
public:
    explicit UserMessageWidget(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setFrameShape(QFrame::NoFrame);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(PrimitiveL, PrimitiveM, PrimitiveL, PrimitiveM);
        // OPENMV-DIFF //
        // layout->setSpacing(0);
        layout->setSpacing(GapVL); // between the text, attached files and images
        // OPENMV-DIFF //

        // OPENMV-DIFF //
        // m_label = new QLabel(this);
        // m_label->setTextFormat(Qt::PlainText);
        // m_label->setWordWrap(true);
        // m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
        // m_label->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        // m_label->setMinimumWidth(1);
        // QPalette pal = m_label->palette();
        // pal.setColor(QPalette::WindowText,
        //              Utils::creatorColor(Utils::Theme::Token_Text_Default));
        // m_label->setPalette(pal);
        m_label = new BubbleText(this);
        // OPENMV-DIFF //
        layout->addWidget(m_label);
    }

    // OPENMV-DIFF //
    // void setText(const QString &text) { m_label->setText(text); }
    void setText(const QString &text)
    {
        m_label->setPlainText(text);
        m_label->setVisible(!text.isEmpty()); // a message may be only images
    }

    // Attached images, as thumbnails under the text.
    void addImage(const QImage &image)
    {
        if (!m_imageRow) {
            m_imageRow = new QHBoxLayout;
            m_imageRow->setContentsMargins(0, 0, 0, 0);
            m_imageRow->setSpacing(GapHS);
            static_cast<QVBoxLayout *>(layout())->addLayout(m_imageRow);
        }
        const qreal dpr = devicePixelRatioF();
        const QImage scaled = image.scaled(qRound(160 * dpr), qRound(120 * dpr),
                                           Qt::KeepAspectRatio, Qt::SmoothTransformation);
        // Rounded corners, like the bubble and the cards.
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
        m_imageRow->addWidget(label);
        updateGeometry();
    }
    QString text() const { return m_label->toPlainText(); }

    // Attached files and folders, as chips between the text and the images.
    void addFile(const Utils::FilePath &file)
    {
        if (!m_fileBox) {
            m_fileBox = new QWidget(this);
            Layouting::Flow{}.attachTo(m_fileBox);
            m_fileBox->layout()->setContentsMargins(0, 0, 0, 0);
            m_fileBox->layout()->setSpacing(GapHS);
            static_cast<QVBoxLayout *>(layout())->insertWidget(1, m_fileBox);
        }
        auto *chip = new FileChip(file, m_fileBox);
        m_fileBox->layout()->addWidget(chip);
        updateGeometry();
    }
    // OPENMV-DIFF //

    QSize sizeHint() const override
    {
        const QFontMetrics fm = m_label->fontMetrics();
        const QMargins lm = layout()->contentsMargins();
        const QMargins cm = m_label->contentsMargins();
        int maxLineWidth = 0;
        // OPENMV-DIFF //
        // for (const QString &line : m_label->text().split(QLatin1Char('\n')))
        for (const QString &line : m_label->toPlainText().split(QLatin1Char('\n')))
        // OPENMV-DIFF //
            maxLineWidth = qMax(maxLineWidth, fm.horizontalAdvance(line));
        // OPENMV-DIFF //
        if (m_imageRow)
            maxLineWidth = qMax(maxLineWidth, m_imageRow->sizeHint().width());
        if (m_fileBox)
            maxLineWidth = qMax(maxLineWidth, m_fileBox->sizeHint().width());
        // The text view lays out with fractional advances and keeps room for its
        // (hidden) cursor: a few pixels of slack, or a short message wraps at its
        // first break chance ("/" + "compact").
        maxLineWidth += 4;
        // OPENMV-DIFF //
        const int w = maxLineWidth + lm.left() + lm.right() + cm.left() + cm.right();
        return QSize(w, QFrame::sizeHint().height());
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        // OPENMV-DIFF //
        // const QColor bg = Utils::creatorColor(Utils::Theme::Token_Foreground_Default);
        const QColor bg = userBubbleColor();
        // OPENMV-DIFF //
        Utils::StyleHelper::drawCardBg(&p, rect(), bg, Qt::NoPen, RadiusS);
    }

private:
    // OPENMV-DIFF //
    // QLabel *m_label = nullptr;
    BubbleText *m_label = nullptr;
    QHBoxLayout *m_imageRow = nullptr;
    QWidget *m_fileBox = nullptr;
    // OPENMV-DIFF //
};

// ---------------------------------------------------------------------------
// AgentMessageWidget — rendered via MarkdownBrowser
// ---------------------------------------------------------------------------

class AgentMessageWidget : public Utils::MarkdownBrowser
{
public:
    explicit AgentMessageWidget(QWidget *parent = nullptr)
        : Utils::MarkdownBrowser(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setOpenExternalLinks(true);
        setEnableCodeCopyButton(true);
        setMargins({0, 0, 0, 0});
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setTextColor(Utils::creatorColor(Utils::Theme::Token_Text_Default));
        // OPENMV-DIFF //
        // On the chat's background (see showEvent()), not a box in the text area's
        // own color (white on light themes).
        setAutoFillBackground(false);
        // OPENMV-DIFF //
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumWidth(1);

        m_renderTimer = new QTimer(this);
        m_renderTimer->setSingleShot(true);
        m_renderTimer->setInterval(100);
        connect(m_renderTimer, &QTimer::timeout, this, [this] {
            // OPENMV-DIFF //
            // setMarkdown(m_rawText);
            render();
            // OPENMV-DIFF //
        });

        // Batched while text streams in; a resize updates it at once (resizeEvent()).
        connect(document()->documentLayout(),
                &QAbstractTextDocumentLayout::documentSizeChanged,
                this, [this] {
            if (!m_heightUpdatePending) {
                m_heightUpdatePending = true;
                QTimer::singleShot(0, this, [this] {
                    m_heightUpdatePending = false;
                    updateBrowserHeight();
                    updateGeometry();
                });
            }
        });

        connect(document(), &QTextDocument::contentsChanged,
                this, [this] { m_cachedUnwrappedIdealWidth = -1; });
    }

    void appendText(const QString &text)
    {
        m_rawText += text;
        m_renderTimer->start();
    }

    QString rawText() const { return m_rawText; } // OPENMV-DIFF: the Markdown, for copying

    // OPENMV-DIFF //
    void showEvent(QShowEvent *event) override
    {
        setOpaqueBackground(this, chatBackground(parentWidget()));
        Utils::MarkdownBrowser::showEvent(event);
    }
    // OPENMV-DIFF //

    // OPENMV-DIFF //
    // A new width re-wraps the text right away (in the base class); take the new
    // height in the same pass. Waiting for the queued update painted a frame of
    // re-wrapped text at the old height, which flickered while dragging a splitter.
    void resizeEvent(QResizeEvent *event) override
    {
        Utils::MarkdownBrowser::resizeEvent(event);
        if (event->size().width() != event->oldSize().width())
            updateBrowserHeight();
    }
    // OPENMV-DIFF //

    // OPENMV-DIFF //
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
    // OPENMV-DIFF //

    void flush()
    {
        if (m_renderTimer->isActive()) {
            m_renderTimer->stop();
            // OPENMV-DIFF //
            // setMarkdown(m_rawText);
            render();
            // OPENMV-DIFF //
        }
    }

    QSize sizeHint() const override
    {
        QTextDocument *doc = document();
        if (m_cachedUnwrappedIdealWidth < 0) {
            const qreal savedTextWidth = doc->textWidth();
            doc->setTextWidth(-1);
            m_cachedUnwrappedIdealWidth = doc->idealWidth();
            doc->setTextWidth(savedTextWidth);
        }
        if (m_cachedUnwrappedIdealWidth <= 0)
            return Utils::MarkdownBrowser::sizeHint();
        const QMargins bcm = contentsMargins();
        const int docMargin = static_cast<int>(doc->documentMargin());
        const int contentWidth = qCeil(m_cachedUnwrappedIdealWidth)
                                 + bcm.left() + bcm.right() + 2 * docMargin;
        return QSize(contentWidth, Utils::MarkdownBrowser::sizeHint().height());
    }

private:
    // OPENMV-DIFF //
    // Each render re-parses, re-highlights and re-lays out the whole message, so a
    // long message re-rendered every 100 ms while it streams kept the IDE busy
    // most of the time. Space the renders by how long one takes, so they use at
    // most about a quarter of the time (100 ms to 1 s apart).
    // The markdown view colors every piece of text the theme's default text color
    // (headings too); that text gets the chat's (chatTextColor()) instead. Links
    // and highlighted code keep theirs.
    void recolorText()
    {
        const QColor from = Utils::creatorColor(Utils::Theme::Token_Text_Default);
        const QColor to = chatTextColor();
        if (from == to)
            return;
        QTextCursor cursor(document());
        cursor.beginEditBlock();
        for (QTextBlock block = document()->begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const QTextFragment fragment = it.fragment();
                if (fragment.charFormat().foreground().color() != from)
                    continue;
                QTextCharFormat format;
                format.setForeground(to);
                cursor.setPosition(fragment.position());
                cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
                cursor.mergeCharFormat(format);
            }
        }
        cursor.endEditBlock();
    }

    void render()
    {
        QElapsedTimer timer;
        timer.start();
        setMarkdown(m_rawText);
        recolorText();
        (void) document()->size(); // the layout, otherwise done later (uncounted)
        (void) sizeHint();         // the unwrapped width, likewise
        m_renderTimer->setInterval(qBound(100, int(timer.elapsed()) * 4, 1000));
    }
    // OPENMV-DIFF //

    void updateBrowserHeight()
    {
        const int docHeight = qCeil(document()->size().height())
                              + contentsMargins().top()
                              + contentsMargins().bottom();
        setFixedHeight(qMax(docHeight, fontMetrics().height()));
    }

    QTimer *m_renderTimer = nullptr;
    QString m_rawText;
    bool m_heightUpdatePending = false;
    mutable qreal m_cachedUnwrappedIdealWidth = -1;
};

// ---------------------------------------------------------------------------
// ThoughtWidget
// ---------------------------------------------------------------------------

class ThoughtWidget : public CollapsibleFrame
{
public:
    explicit ThoughtWidget(const QString &text, QWidget *parent = nullptr)
        : CollapsibleFrame(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setCollapsible(true);

        auto *header = new QLabel(QStringLiteral("<i>Thought</i>"), this);
        QPalette hpal = header->palette();
        hpal.setColor(QPalette::WindowText,
                      Utils::creatorColor(Utils::Theme::Token_Text_Subtle));
        setIndicatorColor(Utils::Theme::Token_Text_Subtle);
        header->setPalette(hpal);
        m_headerLayout->addWidget(header, 1);

        m_label = new QLabel(this);
        m_label->setWordWrap(true);
        m_label->setTextFormat(Qt::PlainText);
        m_label->setText(text);
        QPalette pal = m_label->palette();
        pal.setColor(QPalette::WindowText,
                     Utils::creatorColor(Utils::Theme::Token_Text_Subtle));
        m_label->setPalette(pal);
        QFont f = m_label->font();
        f.setItalic(true);
        f.setPointSizeF(f.pointSizeF() * 0.9);
        m_label->setFont(f);
        m_bodyLayout->addWidget(m_label);
    }

    void appendText(const QString &text)
    {
        // OPENMV-DIFF //
        // m_label->setText(m_label->text() + text);
        // Batch the streamed chunks: re-wrapping the whole (growing) thought for
        // every chunk added up to a lot of work for long thoughts.
        if (m_pendingText.isEmpty()) {
            QTimer::singleShot(100, this, [this] {
                m_label->setText(m_label->text() + std::exchange(m_pendingText, {}));
            });
        }
        m_pendingText += text;
        // OPENMV-DIFF //
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        QPen pen(Utils::creatorColor(Utils::Theme::Token_Stroke_Subtle), 2, Qt::SolidLine);
        pen.setCapStyle(Qt::FlatCap);
        p.setPen(pen);
        p.drawLine(QPointF(1, 0), QPointF(1, height()));
    }

private:
    QLabel *m_label = nullptr;
    QString m_pendingText; // OPENMV-DIFF
};

// ---------------------------------------------------------------------------
// ToolCallGroupWidget — groups consecutive tool calls into one collapsible
// ---------------------------------------------------------------------------

// OPENMV-DIFF //
// A small image that acts as a button (so a click on it isn't also a click on
// the header it sits in, which would open or close the group).
class ImageThumbButton : public QAbstractButton
{
public:
    explicit ImageThumbButton(QWidget *parent)
        : QAbstractButton(parent)
    {
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    void setPixmap(const QPixmap &pixmap)
    {
        m_pixmap = pixmap;
        updateGeometry();
        update();
    }

    QSize sizeHint() const override { return m_pixmap.deviceIndependentSize().toSize(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.drawPixmap(0, 0, m_pixmap);
    }

private:
    QPixmap m_pixmap;
};
// OPENMV-DIFF //

class ToolCallGroupWidget : public CollapsibleFrame
{
public:
    explicit ToolCallGroupWidget(QWidget *parent = nullptr)
        : CollapsibleFrame(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setCollapsed(true);

        auto *headerVBox = new QVBoxLayout;
        headerVBox->setContentsMargins(0, 0, 0, 0);
        headerVBox->setSpacing(0);

        // Summary row: main icon + "N tool calls" + per-status icon+count pairs
        auto *summaryRow = new QHBoxLayout;
        summaryRow->setContentsMargins(0, 0, 0, 0);
        summaryRow->setSpacing(GapHXs);

        m_summaryIcon = new Utils::QtcIconDisplay(this);
        summaryRow->addWidget(m_summaryIcon);

        m_summaryLabel = new QLabel(this);
        summaryRow->addWidget(m_summaryLabel);

        // Per-status icon + count widgets (hidden by default)
        static constexpr ToolCallStatus statusMap[StatusCount] = {
            ToolCallStatus::completed,
            ToolCallStatus::in_progress,
            ToolCallStatus::failed,
            ToolCallStatus::pending,
        };
        for (int i = 0; i < StatusCount; ++i) {
            m_statusIcons[i] = toolCallStatusWidget(statusMap[i], this);
            m_statusIcons[i]->setVisible(false);
            summaryRow->addWidget(m_statusIcons[i]);

            m_statusLabels[i] = new QLabel(this);
            m_statusLabels[i]->setVisible(false);
            summaryRow->addWidget(m_statusLabels[i]);
        }

        summaryRow->addStretch(1);
        m_summaryRow = summaryRow; // OPENMV-DIFF: script chips go here
        headerVBox->addLayout(summaryRow);

        // Running-tools row
        auto *runningRow = new QHBoxLayout;
        runningRow->setContentsMargins(0, 0, 0, 0);
        runningRow->setSpacing(GapHXs);

        m_runningIcon = toolCallStatusWidget(ToolCallStatus::in_progress, this);
        m_runningIcon->setVisible(false);
        runningRow->addWidget(m_runningIcon);

        m_runningLabel = new QLabel(this);
        m_runningLabel->setWordWrap(true);
        m_runningLabel->setVisible(false);
        QFont smallFont = m_runningLabel->font();
        smallFont.setPointSizeF(smallFont.pointSizeF() * 0.9);
        m_runningLabel->setFont(smallFont);
        runningRow->addWidget(m_runningLabel, 1);
        headerVBox->addLayout(runningRow);

        m_headerLayout->addLayout(headerVBox, 1);

        m_innerLayout = new QVBoxLayout;
        m_innerLayout->setContentsMargins(0, 0, 0, 0);
        m_innerLayout->setSpacing(GapVXs);
        m_bodyLayout->addLayout(m_innerLayout);

        updateSummary();
    }

    // OPENMV-DIFF //
    // A script the agent ran, on the summary row: seen without expanding the group.
    void addScriptChip(const QString &script, const QString &name)
    {
        if (m_scripts.contains(script))
            return;
        m_scripts.append(script);
        QWidget *chip = createScriptChip(script, name, this);
        m_summaryRow->insertWidget(m_summaryRow->count() - 1, chip);
        setFirstRowHeight(chip->sizeHint().height());
    }

    // The last image a tool in the group returned (a camera frame, say), small,
    // on the summary row; the full one is in the tool call.
    // Clicking it opens the group and that tool call, and brings the full-size
    // image into view.
    void setLatestImage(const QImage &image, ToolCallDetailWidget *detail)
    {
        if (image.isNull())
            return;
        m_imageDetail = detail;
        const int height = chipHeight(fontMetrics());
        if (!m_imageThumb) {
            m_imageThumb = new ImageThumbButton(this);
            connect(m_imageThumb, &QAbstractButton::clicked, this, [this] { revealImage(); });
            m_summaryRow->insertWidget(m_summaryRow->count() - 1, m_imageThumb);
            setFirstRowHeight(height);
        }
        const qreal dpr = devicePixelRatioF();
        const QImage scaled = image.scaledToHeight(qRound(height * dpr), Qt::SmoothTransformation);
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
        m_imageThumb->setPixmap(thumbnail);
        m_imageThumb->setToolTip(QStringLiteral("%1 \u00d7 %2").arg(image.width()).arg(image.height()));
    }

    void revealImage()
    {
        if (!m_imageDetail)
            return;
        setCollapsed(false);
        m_imageDetail->setCollapsed(false);
        // After the layout has made room for them.
        QTimer::singleShot(0, this, [this] {
            QWidget *target = m_imageDetail ? m_imageDetail->latestImageWidget() : nullptr;
            for (QWidget *w = parentWidget(); target && w; w = w->parentWidget()) {
                if (auto *scrollArea = qobject_cast<QScrollArea *>(w)) {
                    scrollArea->ensureWidgetVisible(target, 0, PaddingVL);
                    break;
                }
            }
        });
    }
    // OPENMV-DIFF //

    void addChildWidget(QWidget *widget)
    {
        m_innerLayout->addWidget(widget);
        updateSummary();
    }

    void trackStatus(const QString &toolCallId, ToolCallStatus status)
    {
        m_statuses[toolCallId] = status;
        updateSummary();
    }

    void trackTitle(const QString &toolCallId, const QString &title)
    {
        m_titles[toolCallId] = displayToolTitle(title); // OPENMV-DIFF
        updateSummary();
    }

    void updateSummary()
    {
        const int total = m_statuses.size();

        int counts[StatusCount] = {};
        QStringList runningNames;
        for (auto it = m_statuses.cbegin(); it != m_statuses.cend(); ++it) {
            switch (it.value()) {
            case ToolCallStatus::completed:   ++counts[Completed]; break;
            case ToolCallStatus::failed:      ++counts[Failed]; break;
            case ToolCallStatus::in_progress:
                ++counts[InProgress];
                if (const QString title = m_titles.value(it.key()); !title.isEmpty())
                    runningNames << title;
                break;
            case ToolCallStatus::pending:
                ++counts[Pending];
                if (const QString title = m_titles.value(it.key()); !title.isEmpty())
                    runningNames << title;
                break;
            }
        }

        m_summaryIcon->setIcon(Utils::Icons::PROJECT); // wrench icon
        m_summaryLabel->setText(QStringLiteral("<b>%1 tool call%2</b>")
                                    .arg(total)
                                    .arg(total != 1 ? QStringLiteral("s") : QString()));

        for (int i = 0; i < StatusCount; ++i) {
            const bool visible = counts[i] > 0;
            m_statusIcons[i]->setVisible(visible);
            m_statusLabels[i]->setVisible(visible);
            if (visible)
                m_statusLabels[i]->setText(QString::number(counts[i]));
        }

        // Show currently running / pending tool names on a separate line
        const bool hasRunning = !runningNames.isEmpty();
        m_runningIcon->setVisible(hasRunning);
        m_runningLabel->setVisible(hasRunning);
        if (hasRunning)
            m_runningLabel->setText(runningNames.join(QStringLiteral(", ")));
    }

private:
    enum StatusIndex { Completed, InProgress, Failed, Pending, StatusCount };

    Utils::QtcIconDisplay *m_summaryIcon = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QWidget *m_statusIcons[StatusCount] = {};
    QLabel *m_statusLabels[StatusCount] = {};
    QWidget *m_runningIcon = nullptr;
    QLabel *m_runningLabel = nullptr;
    QVBoxLayout *m_innerLayout = nullptr;
    QHBoxLayout *m_summaryRow = nullptr; // OPENMV-DIFF
    ImageThumbButton *m_imageThumb = nullptr; // OPENMV-DIFF
    QPointer<ToolCallDetailWidget> m_imageDetail; // OPENMV-DIFF: whose image it is
    QStringList m_scripts; // OPENMV-DIFF
    QHash<QString, ToolCallStatus> m_statuses;
    QHash<QString, QString> m_titles;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Utils::StyleHelper::drawCardBg(&p, rect(),
            Utils::creatorColor(Utils::Theme::ChatToolCallBackground));
    }
};

// ---------------------------------------------------------------------------
// PlanWidget
// ---------------------------------------------------------------------------

class PlanWidget : public CollapsibleFrame
{
public:
    explicit PlanWidget(const Plan &plan, QWidget *parent = nullptr)
        : CollapsibleFrame(parent)
    {
        setFrameShape(QFrame::NoFrame);

        auto *header = new QLabel(QStringLiteral("<b>Approach</b>"), this);
        m_headerLayout->addWidget(header, 1);

        QString listHtml = QStringLiteral(
            "<ul style='margin-top:2px; margin-bottom:2px; list-style-type:none; padding-left:4px;'>");
        for (const PlanEntry &entry : plan.entries()) {
            QString icon;
            switch (entry.status()) {
            case PlanEntryStatus::completed:
                icon = QStringLiteral("<span style='color:green'>\u2713</span>");
                break;
            case PlanEntryStatus::in_progress:
                icon = QStringLiteral("<span style='color:dodgerblue'>\u25D4</span>");
                break;
            case PlanEntryStatus::pending:
                icon = QStringLiteral("<span style='color:gray'>\u25CB</span>");
                break;
            }
            listHtml += QStringLiteral("<li style='margin-bottom:3px;'>%1 %2</li>")
                            .arg(icon, entry.content().toHtmlEscaped());
        }
        listHtml += QStringLiteral("</ul>");

        auto *list = new QLabel(listHtml, this);
        list->setTextFormat(Qt::RichText);
        list->setWordWrap(true);
        m_bodyLayout->addWidget(list);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Utils::StyleHelper::drawCardBg(&p, rect(),
            Utils::creatorColor(Utils::Theme::ChatPlanBackground));
    }
};

static QString optionKindToUiString(Acp::PermissionOptionKind kind)
{
    switch (kind) {
    case Acp::PermissionOptionKind::allow_once:
        return Tr::tr("Allow");
    case Acp::PermissionOptionKind::allow_always:
        return Tr::tr("Allow Always");
    case Acp::PermissionOptionKind::reject_once:
        return Tr::tr("Reject");
    case Acp::PermissionOptionKind::reject_always:
        return Tr::tr("Reject Always");
    }
    return {};
}

// ---------------------------------------------------------------------------
// PermissionRequestWidget — inline widget for permission requests
// ---------------------------------------------------------------------------

class PermissionRequestWidget : public CollapsibleFrame
{
public:
    explicit PermissionRequestWidget(const QString &title, const QString &kindText,
                                     const QString &command, QWidget *parent = nullptr)
        : CollapsibleFrame(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setCollapsible(false);

        m_iconDisplay = new Utils::QtcIconDisplay(this);
        m_iconDisplay->setIcon(Utils::Icons::WARNING);
        m_headerLayout->addWidget(m_iconDisplay);

        auto *headerLabel = new QLabel(Tr::tr("Permission Request"), this);
        QFont boldFont = headerLabel->font();
        boldFont.setBold(true);
        headerLabel->setFont(boldFont);
        headerLabel->setWordWrap(true);
        m_headerLayout->addWidget(headerLabel, 1);

        if (!kindText.isEmpty()) {
            auto *kindLabel = new QLabel(QStringLiteral("[%1]").arg(kindText), this);
            QFont smallFont = kindLabel->font();
            smallFont.setPointSizeF(smallFont.pointSizeF() * 0.85);
            kindLabel->setFont(smallFont);
            kindLabel->setWordWrap(true);
            m_headerLayout->addWidget(kindLabel);
        }

        if (!title.isEmpty()) {
            // OPENMV-DIFF //
            // auto *titleLabel = new Utils::ElidingLabel(title, this);
            // titleLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            // titleLabel->setWordWrap(true);
            // All of it, wrapped: it's what the user is approving (a whole shell
            // command, say), and an eliding label kept it to one cut-off line.
            auto *titleLabel = new QLabel(title, this);
            titleLabel->setTextFormat(Qt::PlainText);
            titleLabel->setWordWrap(true);
            titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            titleLabel->setMinimumWidth(1);
            // OPENMV-DIFF //
            m_bodyLayout->addWidget(titleLabel);
        }
        if (!command.isEmpty()) {
            auto *commandLabel = new QLabel(command, this);
            commandLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            commandLabel->setWordWrap(true);
            QFont mono = commandLabel->font();
            mono.setStyleHint(QFont::Monospace);
            mono.setFamily(QFont(QStringLiteral("monospace")).defaultFamily());
            commandLabel->setFont(mono);
            m_bodyLayout->addWidget(commandLabel);
        }
        auto *buttonWidget = new QWidget(this);
        Layouting::Flow{}.attachTo(buttonWidget);
        m_buttonLayout = buttonWidget->layout();
        m_buttonRow = buttonWidget; // OPENMV-DIFF
        m_bodyLayout->addWidget(buttonWidget);

        m_statusLabel = new Utils::ElidingLabel(this);
        // OPENMV-DIFF //
        // Elided text may shrink below its full width (the OpenMV IDE's
        // ElidingLabel no longer sets this): a long title made the chat wider
        // than the panel instead of eliding.
        m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        // OPENMV-DIFF //
        m_statusLabel->hide();
        m_headerLayout->addWidget(m_statusLabel);
    }

    Utils::QtcButton *addOptionButton(const QString &text, Acp::PermissionOptionKind kind)
    {
        const bool reject = kind == Acp::PermissionOptionKind::reject_once
                            || kind == Acp::PermissionOptionKind::reject_always;
        const auto role = reject ? Utils::QtcButton::MediumTertiary
                                 : Utils::QtcButton::MediumPrimary;
        QString buttonText;
        if (text.isEmpty())
            buttonText = optionKindToUiString(kind);
        // OPENMV-DIFF //
        // else if (text.size() > 30)
        //     buttonText = text.left(27) + QStringLiteral("...");
        // Room for Claude's "Yes, and don't ask again for ..." options.
        else if (text.size() > 48)
            buttonText = text.left(45) + QStringLiteral("...");
        // OPENMV-DIFF //
        else
            buttonText = text;
        auto *button = new Utils::QtcButton(buttonText, role, this);
        button->setToolTip(text);
        m_buttonLayout->addWidget(button);
        m_buttons.append(button);
        return button;
    }

    void setResolved(const QString &text, bool accepted)
    {
        for (Utils::QtcButton *button : std::as_const(m_buttons))
            button->hide();
        // OPENMV-DIFF: the row too, or its height stays as a gap under the text
        m_buttonRow->hide();
        m_iconDisplay->setIcon(accepted ? Utils::Icons::OK : Utils::Icons::CRITICAL);
        m_statusLabel->setText(QStringLiteral("<i>%1</i>").arg(text.toHtmlEscaped()));
        m_statusLabel->show();
        setCollapsible(true);
        setCollapsed(true);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor bg = Utils::creatorColor(Utils::Theme::ChatToolCallBackground);
        Utils::StyleHelper::drawCardBg(&p, rect(), bg, QPen(Qt::NoPen),
                                       Utils::StyleHelper::SpacingTokens::RadiusS);
        // Orange left border to distinguish from regular tool calls
        QPainterPath clip;
        clip.addRoundedRect(rect(), Utils::StyleHelper::SpacingTokens::RadiusS,
                            Utils::StyleHelper::SpacingTokens::RadiusS);
        p.setClipPath(clip);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(QRect(0, 0, 3, height()), QColor{0xe6, 0x9c, 0x24}); // amber
        p.setClipping(false);
    }

private:
    Utils::QtcIconDisplay *m_iconDisplay = nullptr;
    QLayout *m_buttonLayout = nullptr;
    QWidget *m_buttonRow = nullptr; // OPENMV-DIFF
    Utils::ElidingLabel *m_statusLabel = nullptr;
    QList<Utils::QtcButton *> m_buttons;
};

// ---------------------------------------------------------------------------
// AuthenticationWidget — inline widget for authentication flow
// ---------------------------------------------------------------------------

class AuthenticationWidget : public CollapsibleFrame
{
    Q_OBJECT

public:
    explicit AuthenticationWidget(const QList<Acp::AuthMethod> &methods,
                                  QWidget *parent = nullptr)
        : CollapsibleFrame(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setCollapsible(false);

        auto *icon = new QLabel(QStringLiteral("\U0001F512"), this); // 🔒
        m_headerLayout->addWidget(icon);

        auto *label = new QLabel(QStringLiteral("<b>Authentication Required</b>"), this);
        label->setTextFormat(Qt::RichText);
        m_headerLayout->addWidget(label, 1);

        // Method selector (only shown if multiple methods)
        m_methodCombo = new QComboBox(this);
        for (const Acp::AuthMethod &method : methods) {
            const auto *agent = std::get_if<Acp::AuthMethodAgent>(&method);
            QTC_ASSERT(agent, continue);
            m_methodCombo->addItem(agent->name(), agent->id());
        }
        m_methodCombo->setVisible(methods.size() > 1);
        m_bodyLayout->addWidget(m_methodCombo);

        // Description label
        m_descriptionLabel = new QLabel(this);
        m_descriptionLabel->setWordWrap(true);
        QPalette descPal = m_descriptionLabel->palette();
        descPal.setColor(QPalette::WindowText, descPal.color(QPalette::PlaceholderText));
        m_descriptionLabel->setPalette(descPal);
        m_descriptionLabel->hide();
        m_bodyLayout->addWidget(m_descriptionLabel);

        // Error label
        m_errorLabel = new QLabel(this);
        m_errorLabel->setWordWrap(true);
        QPalette errorPal = m_errorLabel->palette();
        errorPal.setColor(QPalette::WindowText, QColor(0xfc, 0x8c, 0x8c));
        m_errorLabel->setPalette(errorPal);
        m_errorLabel->hide();
        m_bodyLayout->addWidget(m_errorLabel);

        // Buttons
        auto *buttonLayout = new QHBoxLayout;
        buttonLayout->setSpacing(6);

        m_authButton = new QPushButton(Tr::tr("Authenticate"), this);
        buttonLayout->addWidget(m_authButton);
        buttonLayout->addStretch();
        m_bodyLayout->addLayout(buttonLayout);

        // Status label (shown after resolution)
        m_statusLabel = new QLabel(this);
        m_statusLabel->setTextFormat(Qt::RichText);
        m_statusLabel->hide();
        m_bodyLayout->addWidget(m_statusLabel);

        // Update description when method changes
        auto updateDescription = [this, methods] {
            const int idx = m_methodCombo->currentIndex();
            if (idx >= 0 && idx < methods.size()) {
                const auto *agent = std::get_if<Acp::AuthMethodAgent>(&methods.at(idx));
                QTC_ASSERT(agent, return);
                const auto &desc = agent->description();
                if (desc.has_value() && !desc->isEmpty()) {
                    m_descriptionLabel->setText(*desc);
                    m_descriptionLabel->show();
                    return;
                }
            }
            m_descriptionLabel->hide();
        };
        updateDescription();
        QObject::connect(m_methodCombo, &QComboBox::currentIndexChanged,
                         this, updateDescription);

        QObject::connect(m_authButton, &QPushButton::clicked, this, [this] {
            m_errorLabel->hide();
            const QString methodId = m_methodCombo->currentData().toString();
            if (!methodId.isEmpty())
                emit authenticateRequested(methodId);
        });
    }

    void showError(const QString &error)
    {
        m_errorLabel->setText(error);
        m_errorLabel->show();
        m_authButton->setEnabled(true);
    }

    void setResolved()
    {
        m_authButton->setEnabled(false);
        m_methodCombo->setEnabled(false);
        m_errorLabel->hide();
        m_statusLabel->setText(
            QStringLiteral("<i>%1</i>").arg(Tr::tr("Authenticated").toHtmlEscaped()));
        m_statusLabel->show();
    }

signals:
    void authenticateRequested(const QString &methodId);

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor bg = Utils::creatorColor(Utils::Theme::ChatToolCallBackground);
        Utils::StyleHelper::drawCardBg(&p, rect(), bg, QPen(Qt::NoPen), RadiusS);
        QPainterPath clip;
        clip.addRoundedRect(rect(), RadiusS, RadiusS);
        p.setClipPath(clip);
        p.setRenderHint(QPainter::Antialiasing, false);
        p.fillRect(QRect(0, 0, 3, height()), QColor{0x1e, 0x90, 0xff}); // blue
        p.setClipping(false);
    }

private:
    QComboBox *m_methodCombo = nullptr;
    QLabel *m_descriptionLabel = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_authButton = nullptr;
    QLabel *m_statusLabel = nullptr;
};

// ---------------------------------------------------------------------------
// MessageViewFindSupport searches across all MarkdownBrowser widgets
// ---------------------------------------------------------------------------

class MessageViewFindSupport : public Core::IFindSupport
{
public:
    explicit MessageViewFindSupport(AcpMessageView *view)
        : m_view(view)
    {}

    bool supportsReplace() const override { return false; }

    Utils::FindFlags supportedFindFlags() const override
    {
        return Utils::FindCaseSensitively | Utils::FindWholeWords
               | Utils::FindRegularExpression;
    }

    void resetIncrementalSearch() override { m_incrementalStart = {nullptr, {}}; }

    void highlightAll(const QString &txt, Utils::FindFlags findFlags) override
    {
        if (txt.isEmpty()) {
            clearHighlights();
            return;
        }
        const QTextDocument::FindFlags flags = toDocFlags(findFlags);
        QTextCharFormat format;
        format.setBackground(
            Utils::creatorColor(Utils::Theme::OutputPanes_SearchResultBackgroundColor));
        for (auto *browser : browsers()) {
            QList<QTextEdit::ExtraSelection> selections;
            int pos = 0;
            QTextCursor found = findInBrowser(browser, txt, pos, findFlags, flags);
            while (!found.isNull()) {
                selections.append({found, format});
                pos = found.selectionEnd();
                if (found.position() == found.anchor())
                    ++pos; // Avoid infinite loop on zero-length matches
                found = findInBrowser(browser, txt, pos, findFlags, flags);
            }
            browser->setExtraSelections(selections);
        }
    }

    void clearHighlights() override
    {
        for (auto *browser : browsers())
            browser->setExtraSelections({});
    }

    QString currentFindString() const override { return {}; }
    QString completedFindString() const override { return {}; }

    Result findIncremental(const QString &txt, Utils::FindFlags findFlags) override
    {
        if (txt.isEmpty()) {
            clearHighlights();
            return NotFound;
        }

        if (m_incrementalStart.first == nullptr) {
            if (auto *browser = topmostVisibleBrowser()) {
                QWidget *viewport = m_view->viewport();
                const int browserTopInViewport = browser->mapTo(viewport, QPoint(0, 0)).y();
                const QPoint visibleTopInBrowser(0, qMax(0, -browserTopInViewport));
                m_incrementalStart = {browser, browser->cursorForPosition(visibleTopInBrowser)};
            }
        }

        Result result = findText(txt, findFlags, true);
        highlightAll(result == Found ? txt : QString(), findFlags);
        return result;
    }

    Result findStep(const QString &txt, Utils::FindFlags findFlags) override
    {
        if (txt.isEmpty())
            return NotFound;
        return findText(txt, findFlags, false);
    }

private:
    QList<Utils::MarkdownBrowser *> browsers() const
    {
        return m_view->findChildren<Utils::MarkdownBrowser *>();
    }

    Utils::MarkdownBrowser *topmostVisibleBrowser() const
    {
        QWidget *viewport = m_view->viewport();
        const QRect viewportRect = viewport->rect();
        Utils::MarkdownBrowser *topmost = nullptr;
        int topmostY = std::numeric_limits<int>::max();
        for (auto *browser : browsers()) {
            const QRect browserRect(browser->mapTo(viewport, QPoint(0, 0)), browser->size());
            if (browserRect.intersects(viewportRect) && browserRect.top() < topmostY) {
                topmostY = browserRect.top();
                topmost = browser;
            }
        }
        return topmost;
    }

    static QRegularExpression searchRegex(const QString &txt, Utils::FindFlags findFlags)
    {
        return QRegularExpression(
            (findFlags & Utils::FindRegularExpression) ? txt : QRegularExpression::escape(txt),
            (findFlags & Utils::FindCaseSensitively) ? QRegularExpression::NoPatternOption
                                                     : QRegularExpression::CaseInsensitiveOption);
    }

    static QTextDocument::FindFlags toDocFlags(Utils::FindFlags findFlags)
    {
        QTextDocument::FindFlags flags;
        if (findFlags & Utils::FindWholeWords)
            flags |= QTextDocument::FindWholeWords;
        return flags;
    }

    QTextCursor findInBrowser(
        const Utils::MarkdownBrowser *browser,
        const QString &txt,
        int startPos,
        Utils::FindFlags findFlags,
        QTextDocument::FindFlags docFlags)
    {
        if (startPos < 0)
            startPos = findFlags & Utils::FindBackward ? browser->document()->characterCount() - 1 : 0;
        return browser->document()->find(searchRegex(txt, findFlags), startPos, docFlags);
    }

    Result findText(const QString &txt, Utils::FindFlags findFlags, bool incremental)
    {
        const QList<Utils::MarkdownBrowser *> allBrowsers = browsers();
        const int count = allBrowsers.size();
        if (count == 0)
            return NotFound;

        const bool backward = findFlags & Utils::FindBackward;
        QTextDocument::FindFlags docFlags = toDocFlags(findFlags);
        if (backward)
            docFlags |= QTextDocument::FindBackward;

        // Determine starting browser and position
        int startIdx = -1;

        auto findIn = [&](int browserIndex, int startPos = -1) {
            Utils::MarkdownBrowser *browser = allBrowsers[browserIndex];
            const QTextCursor found = findInBrowser(browser, txt, startPos, findFlags, docFlags);

            if (found.isNull())
                return false;

            m_currentResult = {browser, found};
            browser->setTextCursor(found);
            browser->ensureCursorVisible();
            // Scroll the message view to make this browser visible
            m_view->ensureWidgetVisible(browser);
            return true;
        };

        if (incremental && m_incrementalStart.first != nullptr) {
            if (startIdx = allBrowsers.indexOf(m_incrementalStart.first); startIdx >= 0) {
                int startPos = backward ? m_incrementalStart.second.selectionEnd()
                                        : m_incrementalStart.second.selectionStart();
                if (findIn(startIdx, startPos))
                    return Found;
                startIdx = (backward ? startIdx - 1 + count : startIdx + 1) % count;
            }
        } else if (!incremental && m_currentResult.first != nullptr) {
            if (startIdx = allBrowsers.indexOf(m_currentResult.first); startIdx >= 0) {
                int startPos = backward ? m_currentResult.second.selectionStart()
                                        : m_currentResult.second.selectionEnd();
                if (findIn(startIdx, startPos))
                    return Found;
                startIdx = (backward ? startIdx - 1 + count : startIdx + 1) % count;
            }
        }


        if (startIdx < 0)
            startIdx = backward ? allBrowsers.size() - 1 : 0;

        // Search from current position, then wrap around
        for (int i = 0; i < count; ++i) {
            const int idx = backward ? (startIdx - i + count) % count : (startIdx + i) % count;
            if (findIn(idx))
                return Found;
        }

        m_currentResult = {nullptr, QTextCursor()};
        return NotFound;
    }

    AcpMessageView *m_view;
    QPair<Utils::MarkdownBrowser *, QTextCursor> m_currentResult;
    QPair<Utils::MarkdownBrowser *, QTextCursor> m_incrementalStart;
};

// ---------------------------------------------------------------------------
// AcpMessageView
// ---------------------------------------------------------------------------

static const char SETTINGS_THOUGHTS_VISIBLE[] = "acpclient/thoughtsVisible";

AcpMessageView::AcpMessageView(QWidget *parent)
    : QScrollArea(parent)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_container = new QWidget(this);
    m_layout = new QVBoxLayout(m_container);
    m_layout->setContentsMargins(PaddingHXl, PaddingHXl, PaddingHXl, PaddingHXl);
    m_layout->setSpacing(GapVL);

    auto *trailingRow = new QWidget(m_container);
    auto *trailingLayout = new QHBoxLayout(trailingRow);
    trailingLayout->setContentsMargins(0, 0, 0, 0);
    trailingLayout->setSpacing(GapHS);

    m_progressIndicator = new Utils::ProgressIndicator(
        Utils::ProgressIndicatorSize::Small, trailingRow);
    m_progressIndicator->setVisible(false);
    trailingLayout->addWidget(m_progressIndicator);

    m_elapsedLabel = new QLabel(trailingRow);
    m_elapsedLabel->setVisible(false);
    QFont elapsedFont = m_elapsedLabel->font();
    elapsedFont.setPointSizeF(elapsedFont.pointSizeF() * 0.9);
    elapsedFont.setFamily(QStringLiteral("monospace"));
    m_elapsedLabel->setFont(elapsedFont);
    trailingLayout->addWidget(m_elapsedLabel);

    trailingLayout->addStretch();

    m_layout->addWidget(trailingRow);
    m_layout->addStretch();

    m_elapsedTimer = new QElapsedTimer();
    m_progressUpdateTimer = new QTimer(this);
    m_progressUpdateTimer->setInterval(1000);
    connect(m_progressUpdateTimer, &QTimer::timeout, this, &AcpMessageView::updateElapsedTimeLabel);

    setWidget(m_container);
    // OPENMV-DIFF //
    // White on light themes (see ChatPanel); replies paint the same color.
    if (!Utils::creatorTheme()->flag(Utils::Theme::DarkUserInterface)) {
        QPalette pal = viewport()->palette();
        pal.setColor(QPalette::Window, Utils::creatorColor(Utils::Theme::BackgroundColorNormal));
        pal.setColor(QPalette::Base, Utils::creatorColor(Utils::Theme::BackgroundColorNormal));
        viewport()->setPalette(pal);
        viewport()->setAutoFillBackground(true);
        // setWidget() made the container fill its background (the theme's grey)
        // over the viewport: white too.
        m_container->setPalette(pal);
    }
    // OPENMV-DIFF //

    Aggregation::aggregate({this, new MessageViewFindSupport(this)});

    // Track whether user has scrolled up to suppress auto-scroll
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        m_autoScroll = (value >= verticalScrollBar()->maximum() - 10);
        // OPENMV-DIFF //
        if (!m_restoringAnchor)
            captureAnchor();
        // OPENMV-DIFF //
    });
    // OPENMV-DIFF //
    // connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, &AcpMessageView::scrollToBottom);
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, &AcpMessageView::restoreAnchor);
    // OPENMV-DIFF //


    m_thoughtsVisible = Core::ICore::settings()->value(SETTINGS_THOUGHTS_VISIBLE, true).toBool();
}

void AcpMessageView::setThoughtsVisible(bool visible)
{
    if (m_thoughtsVisible == visible)
        return;
    m_thoughtsVisible = visible;
    Core::ICore::settings()->setValue(SETTINGS_THOUGHTS_VISIBLE, visible);
    for (ThoughtWidget *w : std::as_const(m_thoughtWidgets))
        w->setVisible(visible);
}

void AcpMessageView::setAgentIconUrl(const QString &iconUrl)
{
    m_agentIconUrl = iconUrl;
}

void AcpMessageView::setPrompting(bool prompting)
{
    if (prompting) {
        m_elapsedTimer->restart();
        updateElapsedTimeLabel();
        m_progressUpdateTimer->start();
    } else {
        m_progressUpdateTimer->stop();
        for (auto it = m_toolCallDetailWidgets.constBegin();
             it != m_toolCallDetailWidgets.constEnd(); ++it) {
            // OPENMV-DIFF //
            // qDebug() << "Checking tool call" << it.key() << "status"
            //          << toString(it.value()->status());
            // OPENMV-DIFF //
            if (it.value()->status() == ToolCallStatus::in_progress
                || it.value()->status() == ToolCallStatus::pending) {
                it.value()->applyStatus(ToolCallStatus::failed);
                if (auto *group = m_toolCallGroups.value(it.key()))
                    group->trackStatus(it.key(), ToolCallStatus::failed);
            }
        }
    }
    m_progressIndicator->setVisible(prompting);
    m_elapsedLabel->setVisible(prompting);
}

void AcpMessageView::clear()
{
    // Remove all widgets except the trailing elapsed label and bottom stretch
    while (m_layout->count() > 2) {
        QLayoutItem *item = m_layout->takeAt(0);
        if (item->widget())
            delete item->widget();
        delete item;
    }

    m_currentAgentWidget = nullptr;
    m_currentThoughtWidget = nullptr;
    m_currentToolCallGroup = nullptr;
    m_currentAuthWidget = nullptr;
    m_lastUserMessage = nullptr; // OPENMV-DIFF
    m_thoughtWidgets.clear();
    m_toolCallDetailWidgets.clear();
    m_toolCallGroups.clear();
    m_autoScroll = true;
}

// OPENMV-DIFF //
// void AcpMessageView::addUserMessage(const QString &text)
void AcpMessageView::addUserMessage(const QString &text, const QList<QImage> &images,
                                    const QList<Utils::FilePath> &files)
// OPENMV-DIFF //
{
    finishAgentMessage();
    finishToolCallGroup();
    auto *msg = new UserMessageWidget(m_container);
    msg->setText(text);
    // OPENMV-DIFF //
    for (const Utils::FilePath &file : files)
        msg->addFile(file);
    for (const QImage &image : images)
        msg->addImage(image);
    // OPENMV-DIFF //
    // OPENMV-DIFF //
    // addWidget(wrapWithSpacer(msg, Qt::AlignRight));
    addWidget(wrapWithSpacer(msg, Qt::AlignRight, [msg] { return msg->text(); }));
    m_lastUserMessage = msg;
    // OPENMV-DIFF //
    m_autoScroll = true; // reset auto-scroll when user sends a message to ensure they see the response
    scrollToBottom();
}

// OPENMV-DIFF //
// A replayed message's images arrive after its text, as separate updates.
void AcpMessageView::addUserImage(const QImage &image)
{
    if (m_lastUserMessage)
        m_lastUserMessage->addImage(image);
    else
        addUserMessage({}, {image});
}

void AcpMessageView::addUserFile(const Utils::FilePath &file)
{
    if (m_lastUserMessage)
        m_lastUserMessage->addFile(file);
    else
        addUserMessage({}, {}, {file});
}

// A note in the conversation that isn't anyone's message, e.g. that the user
// stopped the agent: small, muted, centered.
void AcpMessageView::addStatusMessage(const QString &text)
{
    finishAgentMessage();
    finishToolCallGroup();
    auto *label = new QLabel(text, m_container);
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    QFont f = label->font();
    f.setItalic(true);
    f.setPointSizeF(f.pointSizeF() * 0.9);
    label->setFont(f);
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, Utils::creatorColor(Utils::Theme::Token_Text_Muted));
    label->setPalette(pal);
    addWidget(label);
}
// OPENMV-DIFF //

void AcpMessageView::appendAgentText(const QString &text)
{
    if (m_currentThoughtWidget)
        m_currentThoughtWidget->setCollapsed(true);
    m_currentThoughtWidget = nullptr;
    finishToolCallGroup();
    if (!m_currentAgentWidget) {
        m_currentAgentWidget = new AgentMessageWidget(m_container);
        m_currentAgentWidget->appendText(text);
        // OPENMV-DIFF //
        // addWidget(wrapWithSpacer(m_currentAgentWidget, Qt::AlignLeft));
        AgentMessageWidget *agentMessage = m_currentAgentWidget;
        addWidget(wrapWithSpacer(agentMessage, Qt::AlignLeft,
                                 [agentMessage] { return agentMessage->rawText(); }));
        // OPENMV-DIFF //
    } else {
        m_currentAgentWidget->appendText(text);
    }
}

void AcpMessageView::appendAgentThought(const QString &text)
{
    if (text.isEmpty())
        return;
    if (!m_currentThoughtWidget) {
        finishAgentMessage();
        finishToolCallGroup();
        m_currentThoughtWidget = new ThoughtWidget(text, m_container);
        m_currentThoughtWidget->setVisible(m_thoughtsVisible);
        m_thoughtWidgets.append(m_currentThoughtWidget);
        addWidget(m_currentThoughtWidget);
    } else {
        m_currentThoughtWidget->appendText(text);
    }
}

ToolCallGroupWidget *AcpMessageView::ensureToolCallGroup()
{
    if (!m_currentToolCallGroup) {
        m_currentToolCallGroup = new ToolCallGroupWidget(m_container);
        addWidget(m_currentToolCallGroup);
    }
    return m_currentToolCallGroup;
}

void AcpMessageView::finishToolCallGroup()
{
    if (m_currentToolCallGroup) {
        m_currentToolCallGroup->updateSummary();
        m_currentToolCallGroup = nullptr;
    }
}

void AcpMessageView::addToolCall(const ToolCall &toolCall)
{
    finishAgentMessage();

    auto *group = ensureToolCallGroup();
    const ToolCallStatus status = toolCall.status().value_or(ToolCallStatus::in_progress);
    group->trackStatus(toolCall.toolCallId(), status);
    group->trackTitle(toolCall.toolCallId(), toolCall.title());
    m_toolCallGroups[toolCall.toolCallId()] = group;

    auto *detail = new ToolCallDetailWidget(toolCall, group);
    connect(detail, &ToolCallDetailWidget::scriptFound, group,
            [group](const QString &script, const QString &name) {
                group->addScriptChip(script, name);
            }); // OPENMV-DIFF
    if (!detail->script().isEmpty()) // OPENMV-DIFF: found while it was built
        group->addScriptChip(detail->script(), detail->scriptName());
    connect(detail, &ToolCallDetailWidget::imageFound, group,
            [group, detail](const QImage &image) {
                group->setLatestImage(image, detail);
            }); // OPENMV-DIFF
    detail->setContentMaxWidth(contentMaxWidth());
    group->addChildWidget(detail);
    m_toolCallDetailWidgets[toolCall.toolCallId()] = detail;
}

void AcpMessageView::updateToolCall(const ToolCallUpdate &update)
{
    auto updateGroup = [&](const QString &id, std::optional<ToolCallStatus> status,
                           std::optional<QString> title) {
        if (auto *group = m_toolCallGroups.value(id)) {
            if (status)
                group->trackStatus(id, *status);
            if (title)
                group->trackTitle(id, *title);
        }
    };

    ToolCallDetailWidget *detail = m_toolCallDetailWidgets.value(update.toolCallId());
    if (!detail) {
        const QString title = update.title().value_or(QStringLiteral("Tool Call"));
        ToolCall tc;
        tc.toolCallId(update.toolCallId());
        tc.title(title);
        tc.status(update.status().value_or(ToolCallStatus::in_progress));
        tc.kind(update.kind());
        auto *group = ensureToolCallGroup();
        group->trackStatus(update.toolCallId(),
                           update.status().value_or(ToolCallStatus::in_progress));
        group->trackTitle(update.toolCallId(), title);
        m_toolCallGroups[update.toolCallId()] = group;
        detail = new ToolCallDetailWidget(tc, group);
        connect(detail, &ToolCallDetailWidget::scriptFound, group,
                [group](const QString &script, const QString &name) {
                    group->addScriptChip(script, name);
                }); // OPENMV-DIFF
        if (!detail->script().isEmpty()) // OPENMV-DIFF: found while it was built
            group->addScriptChip(detail->script(), detail->scriptName());
        connect(detail, &ToolCallDetailWidget::imageFound, group,
                [group, detail](const QImage &image) {
                    group->setLatestImage(image, detail);
                }); // OPENMV-DIFF
        detail->setContentMaxWidth(contentMaxWidth());
        group->addChildWidget(detail);
        m_toolCallDetailWidgets[update.toolCallId()] = detail;
    }
    detail->updateContent(update);
    updateGroup(update.toolCallId(), update.status(), update.title());
}

void AcpMessageView::addPlan(const Plan &plan)
{
    finishAgentMessage();
    finishToolCallGroup();
    addWidget(new PlanWidget(plan, m_container));
}

void AcpMessageView::addPermissionRequest(const QJsonValue &id,
                                           const RequestPermissionRequest &request)
{
    finishAgentMessage();
    finishToolCallGroup();

    const ToolCallUpdate &toolCall = request.toolCall();
    const QString toolCallId = toolCall.toolCallId();
    const QString title = toolCall.title().value_or(QString());
    const QString kindText = toolCall.kind() ? toString(*toolCall.kind()) : QString();

    QString command;
    if (const auto &rawInput = toolCall.rawInput(); rawInput && rawInput->isObject()) {
        const QJsonValue cmd = rawInput->toObject().value("command");
        if (cmd.isString())
            command = cmd.toString();
        if (title.contains(command))
            command.clear();
    }

    auto *widget = new PermissionRequestWidget(title, kindText, command, m_container);

    auto markToolCallFailed = [this, toolCallId] {
        if (auto *detail = m_toolCallDetailWidgets.value(toolCallId))
            detail->applyStatus(ToolCallStatus::failed);
        if (auto *group = m_toolCallGroups.value(toolCallId))
            group->trackStatus(toolCallId, ToolCallStatus::failed);
    };

    const QList<PermissionOption> options = request.options();
    for (const PermissionOption &option : options) {
        auto *button = widget->addOptionButton(option.name(), option.kind());
        connect(button, &QAbstractButton::clicked, this,
                [this, id, option, widget, markToolCallFailed] {
            const bool accepted = option.kind() == PermissionOptionKind::allow_once
                                  || option.kind() == PermissionOptionKind::allow_always;
            widget->setResolved(option.name(), accepted);
            if (!accepted)
                markToolCallFailed();
            emit permissionOptionSelected(id, option.optionId());
        });
    }

    const bool hasRejectOption = Utils::anyOf(
        options, Utils::equal(&PermissionOption::kind, PermissionOptionKind::reject_once));
    if (!hasRejectOption) {
        // Add a deny/cancel button if not already present, to allow user to reject the request without selecting an option
        auto *cancelButton
            = widget->addOptionButton(Tr::tr("Deny"), PermissionOptionKind::reject_once);
        connect(cancelButton, &QAbstractButton::clicked, this, [this, id, widget, markToolCallFailed] {
            widget->setResolved(Tr::tr("Denied"), false);
            markToolCallFailed();
            emit permissionCancelled(id);
        });
    }

    addWidget(widget);
}

void AcpMessageView::addAuthenticationRequest(const QList<Acp::AuthMethod> &methods)
{
    finishAgentMessage();
    finishToolCallGroup();

    m_currentAuthWidget = new AuthenticationWidget(methods, m_container);
    connect(m_currentAuthWidget, &AuthenticationWidget::authenticateRequested,
            this, &AcpMessageView::authenticateRequested);
    addWidget(m_currentAuthWidget);
}

void AcpMessageView::showAuthenticationError(const QString &error)
{
    if (m_currentAuthWidget)
        m_currentAuthWidget->showError(error);
}

void AcpMessageView::resolveAuthentication()
{
    if (m_currentAuthWidget) {
        m_currentAuthWidget->setResolved();
        m_currentAuthWidget = nullptr;
    }
}

SessionPickerWidget *AcpMessageView::addSessionPicker()
{
    finishAgentMessage();
    finishToolCallGroup();

    m_autoScroll = false;
    auto *picker = new SessionPickerWidget(m_container);
    addWidget(picker);
    return picker;
}

void AcpMessageView::addErrorMessage(const QString &text)
{
    finishAgentMessage();
    finishToolCallGroup();

    auto *widget = new CollapsibleFrame(m_container);
    widget->setFrameShape(QFrame::NoFrame);
    widget->setCollapsible(false);

    auto *icon = new Utils::QtcIconDisplay(widget);
    icon->setIcon(Utils::Icons::CRITICAL);
    widget->headerLayout()->addWidget(icon);

    auto *label = new QLabel(QStringLiteral("<b>Error:</b> %1").arg(text.toHtmlEscaped()), widget);
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, QColor(0xfc, 0x8c, 0x8c));
    label->setPalette(pal);
    widget->headerLayout()->addWidget(label, 1);

    addWidget(widget);
}

void AcpMessageView::finishAgentMessage()
{
    if (m_currentAgentWidget)
        m_currentAgentWidget->flush();
    m_currentAgentWidget = nullptr;
    if (m_currentThoughtWidget)
        m_currentThoughtWidget->setCollapsed(true);
    m_currentThoughtWidget = nullptr;
}

void AcpMessageView::scrollToBottom()
{
    if (m_autoScroll)
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
}

// OPENMV-DIFF //
// QWidget *AcpMessageView::wrapWithSpacer(QWidget *widget, Qt::Alignment side)
// {
//     auto *row = new QWidget(m_container);
//     auto *hbox = new QHBoxLayout(row);
QWidget *AcpMessageView::wrapWithSpacer(QWidget *widget, Qt::Alignment side,
                                        const std::function<QString()> &copyText)
{
    auto *row = new MessageRow(m_container);
    auto *column = new QVBoxLayout(row);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(GapVXs);
    auto *hbox = new QHBoxLayout;
    column->addLayout(hbox);
    if (copyText) {
        auto *actions = new QWidget(row);
        auto *actionsLayout = new QHBoxLayout(actions);
        actionsLayout->setContentsMargins(0, 0, 0, 0);
        actionsLayout->setSpacing(GapHXs);
        if (side == Qt::AlignRight)
            actionsLayout->addStretch();
        else // under the text, past the avatar
            actionsLayout->addSpacing(28 /* AvatarWidget */ + GapHM);
        actionsLayout->addWidget(new CopyMessageButton(copyText, actions));
        if (side != Qt::AlignRight)
            actionsLayout->addStretch();
        column->addWidget(actions);
        row->setActions(actions);
    }
    // OPENMV-DIFF //
    hbox->setContentsMargins(0, 0, 0, 0);
    hbox->setSpacing(GapHM);
    // Widget takes as much space as it needs (Preferred) but won't grow
    // beyond its sizeHint (Maximum). The expanding spacer absorbs the rest.
    widget->setSizePolicy(QSizePolicy::Maximum, widget->sizePolicy().verticalPolicy());
    if (side == Qt::AlignRight) {
        hbox->addSpacerItem(new QSpacerItem(60, 0, QSizePolicy::MinimumExpanding));
        hbox->addWidget(widget);
    } else {
        auto *avatar = new AvatarWidget(row);
        Utils::onResultReady(AcpSettings::iconForUrl(m_agentIconUrl), this, [avatar](const QIcon &icon){
            avatar->setIcon(icon);
        });
        hbox->addWidget(avatar, 0, Qt::AlignTop);
        hbox->addWidget(widget);
        hbox->addSpacerItem(new QSpacerItem(60, 0, QSizePolicy::MinimumExpanding));
    }
    return row;
}

void AcpMessageView::addWidget(QWidget *widget)
{
    m_lastUserMessage = nullptr; // OPENMV-DIFF: set again by addUserMessage
    // Insert before the trailing elapsed label and the bottom stretch
    m_layout->insertWidget(m_layout->count() - 2, widget);
    widget->installEventFilter(this); // OPENMV-DIFF: moves of the scroll anchor
}

// OPENMV-DIFF //
// The message at the top of the view, and where it sits there.
void AcpMessageView::captureAnchor()
{
    const int top = verticalScrollBar()->value();
    m_anchor = nullptr;
    for (int i = 0; i < m_layout->count(); ++i) {
        QWidget *widget = m_layout->itemAt(i)->widget();
        if (widget && widget->isVisible() && widget->geometry().bottom() >= top) {
            m_anchor = widget;
            m_anchorOffset = widget->y() - top;
            // Partway into a message, its re-wrapped text moves within it: keep the
            // same proportion of the way through it rather than the same pixels.
            m_anchorFraction = (m_anchorOffset < 0 && widget->height() > 0)
                                   ? double(top - widget->y()) / widget->height()
                                   : -1;
            return;
        }
    }
}

// After a re-layout: at the end, stay at the end; scrolled up, put the message
// that was at the top back where it was.
void AcpMessageView::restoreAnchor()
{
    if (m_autoScroll || !m_anchor) {
        scrollToBottom();
        return;
    }
    const QScopedValueRollback guard(m_restoringAnchor, true);
    if (m_anchorFraction >= 0)
        verticalScrollBar()->setValue(m_anchor->y() + qRound(m_anchorFraction * m_anchor->height()));
    else
        verticalScrollBar()->setValue(m_anchor->y() - m_anchorOffset);
}

bool AcpMessageView::eventFilter(QObject *watched, QEvent *event)
{
    if ((event->type() == QEvent::Move || event->type() == QEvent::Resize)
        && watched == m_anchor && !m_autoScroll) {
        restoreAnchor();
    }
    return QScrollArea::eventFilter(watched, event);
}
// OPENMV-DIFF //

int AcpMessageView::contentMaxWidth() const
{
    const QMargins m = m_layout->contentsMargins();
    // Subtract ToolCallGroupWidget body margins (one CollapsibleFrame level);
    // ToolCallDetailWidget subtracts its own body margins internally.
    constexpr int groupMargins = CollapsibleFrame::BodyMarginLeft
                                 + CollapsibleFrame::BodyMarginRight;
    return viewport()->width() - m.left() - m.right() - groupMargins;
}

void AcpMessageView::resizeEvent(QResizeEvent *event)
{
    QScrollArea::resizeEvent(event);
    const int maxW = contentMaxWidth();
    for (ToolCallDetailWidget *detail : std::as_const(m_toolCallDetailWidgets))
        detail->setContentMaxWidth(maxW);
    // OPENMV-DIFF //
    // A new width re-wraps the messages, which take their new heights right away,
    // but the list's height (and the scroll range) only follows through queued
    // layout requests, a pass or more later; a frame painted in between showed the
    // chat shifted or with a gap, a flicker while dragging a splitter. Settle them
    // here (each pass can queue the next level up), then keep the view on what it
    // showed: the end, or the message that was at the top.
    if (event->size().width() != event->oldSize().width()) {
        for (int pass = 0; pass < 4; ++pass)
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
        restoreAnchor();
    }
    // OPENMV-DIFF //
}

void AcpMessageView::updateElapsedTimeLabel()
{
    const qint64 elapsedMS = m_elapsedTimer->elapsed();
    const int secs = elapsedMS / 1000;
    const int minutes = secs / 60;
    const QString text = minutes ? Tr::tr("%1:%2 minutes")
                                       .arg(minutes)
                                       .arg(QString::number(secs % 60).rightJustified(2, '0'))
                                 : Tr::tr("%1 seconds").arg(secs);
    m_elapsedLabel->setText(text);
}

} // namespace AcpClient::Internal

#include "acpmessageview.moc"
