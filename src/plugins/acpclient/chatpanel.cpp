// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "chatpanel.h"
#include "acpclienttr.h"
#include "acpmessageview.h"
#include "acpsettings.h"
#include "chatinputedit.h"
#include "sessionpickerwidget.h"

#include <coreplugin/findplaceholder.h>

#include <utils/algorithm.h> // OPENMV-DIFF
#include <utils/elidinglabel.h>
#include <utils/fileutils.h>
#include <utils/qtcwidgets.h>
#include <utils/styledbar.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <texteditor/textdocument.h>

#include <QApplication>
#include <QComboBox>
// OPENMV-DIFF //
#include <QBuffer>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QScopeGuard>
#include <QScopedValueRollback>
#include <QAbstractButton>
#include <QLocale>
#include <QPainterPath>
#include <QShortcut>
#include <QHelpEvent>
#include <utils/tooltip/tooltip.h>
#include <QPointer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QStyledItemDelegate>
// OPENMV-DIFF //
// OPENMV-DIFF //
#include <QScrollBar>
// OPENMV-DIFF //
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QToolButton>
#include <QVBoxLayout>

using namespace Acp;
using namespace Utils;
using namespace Utils::StyleHelper::SpacingTokens;

namespace AcpClient::Internal {

class InputContainerWidget : public QWidget
{
public:
    explicit InputContainerWidget(QWidget *parent = nullptr) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        StyleHelper::drawCardBg(
            &p,
            rect(),
            palette().color(QPalette::Base),
            QPen(palette().color(QPalette::Mid), 1),
            RadiusM);
    }
};

// OPENMV-DIFF //
// The round buttons in the input box: send (an arrow; a stop square while the
// agent works) and commands ("/").
class ChatActionButton : public QAbstractButton
{
public:
    enum Kind { Send, Stop, Commands };

    explicit ChatActionButton(Kind kind)
        : m_kind(kind)
    {
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(24, 24);
    }

    void setKind(Kind kind)
    {
        m_kind = kind;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const QPointF c = r.center();
        const bool hover = underMouse() && isEnabled();

        QColor fill = Qt::transparent;
        QColor fg = creatorColor(isEnabled() ? Theme::Token_Text_Default : Theme::Token_Text_Muted);
        if (m_kind == Send && isEnabled()) {
            fill = creatorColor(Theme::Token_Accent_Default);
            fg = creatorColor(Theme::Token_Basic_White);
        } else if (m_kind == Stop) {
            fill = creatorColor(Theme::Token_Notification_Danger_Default);
            fg = creatorColor(Theme::Token_Basic_White);
        } else if (m_kind == Send) {
            fill = creatorColor(Theme::Token_Foreground_Muted);
        }
        if (hover)
            fill = fill.alpha() ? fill.lighter(120) : creatorColor(Theme::Token_Foreground_Muted);
        p.setPen(Qt::NoPen);
        p.setBrush(fill);
        p.drawEllipse(r);

        if (m_kind == Send) {
            QPen pen(fg, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawLine(QPointF(c.x(), c.y() + 5), QPointF(c.x(), c.y() - 5));
            QPainterPath head;
            head.moveTo(c.x() - 4, c.y() - 1);
            head.lineTo(c.x(), c.y() - 5);
            head.lineTo(c.x() + 4, c.y() - 1);
            p.drawPath(head);
        } else if (m_kind == Stop) {
            p.setBrush(fg);
            p.drawRoundedRect(QRectF(c.x() - 4, c.y() - 4, 8, 8), 1.5, 1.5);
        } else {
            QFont f = font();
            f.setBold(true);
            p.setFont(f);
            p.setPen(fg);
            p.drawText(r, Qt::AlignCenter, QStringLiteral("/"));
        }
    }

private:
    Kind m_kind;
};

// How full the session's context is: a ring and a percentage in the session
// bar. Clicking it compacts the conversation when the agent can.
class ContextGauge : public QAbstractButton
{
public:
    ContextGauge()
    {
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
        hide(); // until the agent reports usage
    }

    void setFraction(double fraction)
    {
        m_fraction = qBound(0.0, fraction, 1.0);
        updateGeometry();
        update();
    }

    void setClickable(bool clickable)
    {
        m_clickable = clickable;
        setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }

    bool isClickable() const { return m_clickable; }

    QSize sizeHint() const override
    {
        return QSize(kPadding + kRing + kGap + fontMetrics().horizontalAdvance("100%") + kPadding,
                     qMax(kRing, fontMetrics().height()) + 6);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (m_clickable && underMouse()) { // square, like the bar's other buttons
            QColor c = creatorColor(Theme::TextColorDisabled);
            c.setAlphaF(c.alphaF() * .5);
            StyleHelper::drawPanelBgRect(&p, rect(), c);
        }

        const QRectF ring(kPadding + 1, (height() - kRing) / 2.0 + 1, kRing - 2, kRing - 2);
        p.setPen(QPen(creatorColor(Theme::Token_Stroke_Subtle), 2.5));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(ring);
        const QColor arc = creatorColor(m_fraction >= 0.95   ? Theme::Token_Notification_Danger_Default
                                        : m_fraction >= 0.80 ? Theme::Token_Notification_Alert_Default
                                                             : Theme::Token_Accent_Default);
        p.setPen(QPen(arc, 2.5, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(ring, 90 * 16, -qRound(m_fraction * 360 * 16));

        p.setPen(creatorColor(Theme::Token_Text_Muted));
        const QRect text(kPadding + kRing + kGap, 0, width() - kPadding - kRing - kGap, height());
        p.drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(qRound(m_fraction * 100)) + QLatin1Char('%'));
    }

private:
    static constexpr int kPadding = 6;
    static constexpr int kRing = 14;
    static constexpr int kGap = 4;
    double m_fraction = 0;
    bool m_clickable = false;
};

// The chip's remove button: its hover highlight is rounded like the chip.
class ChipCloseButton : public QtcIconButton
{
public:
    using QtcIconButton::QtcIconButton;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (underMouse() && isEnabled()) {
            QColor c = creatorColor(Theme::TextColorDisabled);
            c.setAlphaF(c.alphaF() * .5);
            StyleHelper::drawCardBg(&p, rect(), c, Qt::NoPen, RadiusS);
        }
        icon().paint(&p, rect(), Qt::AlignCenter);
    }
};
// OPENMV-DIFF //

// OPENMV-DIFF //
// The agent's commands, from the "/" button: a filter field over a list of
// names with their descriptions, popping up over the input box. Typing filters
// (name or description), arrows/PgUp/PgDn move, Enter or a click picks.
class CommandPalette : public QFrame
{
public:
    // An overlay in the chat panel (not a popup window), so its rounded corners
    // are drawn smoothly over the panel behind it.
    CommandPalette(const QList<QPair<QString, QString>> &commands,
                   const std::function<void(const QString &)> &onPicked, QWidget *parent)
        : QFrame(parent)
        , m_onPicked(onPicked)
    {
        setAttribute(Qt::WA_DeleteOnClose);
        setFrameShape(QFrame::NoFrame);
        setAutoFillBackground(false);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(PaddingHS, PaddingVS, PaddingHS, PaddingVS);
        layout->setSpacing(GapVS);

        // The filter field: the size of a row, its text lined up with theirs.
        m_filter = new QLineEdit;
        m_filter->setPlaceholderText(Tr::tr("Filter commands..."));
        m_filter->setFrame(false);
        m_filter->setFixedHeight(rowHeight(fontMetrics()));
        m_filter->setStyleSheet(QString("QLineEdit { background: %1; border-radius: %2px;"
                                        " padding-left: %3px; padding-right: %3px; }")
                                    .arg(creatorColor(Theme::Token_Background_Muted).name())
                                    .arg(RadiusS)
                                    .arg(PaddingHM - 2));
        m_filter->installEventFilter(this);
        layout->addWidget(m_filter);

        m_list = new QListWidget;
        m_list->setFrameShape(QFrame::NoFrame);
        m_list->setUniformItemSizes(true);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_list->setTextElideMode(Qt::ElideRight);
        m_list->setItemDelegate(new CommandDelegate(m_list));
        m_list->setFocusPolicy(Qt::NoFocus); // typing stays in the filter
        // One highlight, the row Enter picks: the mouse moves it as the arrows do.
        m_list->setMouseTracking(true);
        connect(m_list, &QListWidget::itemEntered, m_list,
                [this](QListWidgetItem *item) { m_list->setCurrentItem(item); });
        m_list->viewport()->setAutoFillBackground(false);
        // A slim scroll bar, as popups have: a rounded thumb, no arrows or track.
        m_list->verticalScrollBar()->setStyleSheet(
            QString("QScrollBar:vertical { background: transparent; width: 8px; margin: 0; }"
                    "QScrollBar::handle:vertical { background: %1; border-radius: 4px;"
                    " min-height: 24px; }"
                    "QScrollBar::handle:vertical:hover { background: %2; }"
                    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical"
                    " { height: 0; border: none; }"
                    "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical"
                    " { background: none; }")
                .arg(creatorColor(Theme::Token_Foreground_Muted).name(),
                     creatorColor(Theme::Token_Text_Muted).name()));
        for (const auto &[name, description] : commands) {
            auto *item = new QListWidgetItem(QLatin1Char('/') + name, m_list);
            item->setData(Qt::UserRole, name);
            item->setData(Qt::UserRole + 1, description);
        }
        layout->addWidget(m_list);
        m_list->viewport()->installEventFilter(this); // the descriptions' tooltips
        connect(m_filter, &QLineEdit::textChanged, this, [this](const QString &text) {
            const QString needle = text.trimmed().remove(QLatin1Char('/'));
            QListWidgetItem *first = nullptr;
            for (int i = 0; i < m_list->count(); ++i) {
                QListWidgetItem *item = m_list->item(i);
                const bool match = needle.isEmpty()
                                   || item->data(Qt::UserRole).toString().contains(needle, Qt::CaseInsensitive)
                                   || item->data(Qt::UserRole + 1).toString().contains(needle, Qt::CaseInsensitive);
                item->setHidden(!match);
                if (match && !first)
                    first = item;
            }
            m_list->setCurrentItem(first);
        });
        connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) { pick(item); });
        m_list->setCurrentRow(0);
    }

    ~CommandPalette() override { qApp->removeEventFilter(this); }

    // Rows shown before the list scrolls.
    static constexpr int kVisibleRows = 10;
    // Room between the rows and the scroll bar.
    static constexpr int kScrollBarGap = 6;

    static int rowHeight(const QFontMetrics &fm) { return fm.height() + 2 * PaddingVS + 2; }

    // The button that opened it: a click there closes the palette and stops,
    // rather than also reopening it.
    void setToggleButton(QWidget *button) { m_toggleButton = button; }

    // Over the input box, as wide as it, in the parent (the chat panel).
    void popupAbove(QWidget *anchor)
    {
        const int rows = qMin(kVisibleRows, m_list->count());
        m_list->setFixedHeight(rows * m_list->sizeHintForRow(0) + 2);
        setFixedWidth(anchor->width());
        resize(width(), sizeHint().height());
        const QPoint topLeft = anchor->mapTo(parentWidget(), QPoint(0, 0));
        move(topLeft.x(), qMax(0, topLeft.y() - height() - GapVXs));
        raise();
        show();
        m_filter->setFocus();
        qApp->installEventFilter(this); // closes on a click elsewhere
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_filter && event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            switch (key->key()) {
            case Qt::Key_Up:
            case Qt::Key_Down:
            case Qt::Key_PageUp:
            case Qt::Key_PageDown:
                QCoreApplication::sendEvent(m_list, event);
                return true;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                pick(m_list->currentItem());
                return true;
            case Qt::Key_Escape:
                close();
                return true;
            default:
                break;
            }
        }
        // A click outside closes it, like a popup; one on the button that opened
        // it goes no further (it would reopen it).
        if (event->type() == QEvent::MouseButtonPress && watched->isWidgetType()) {
            auto *widget = static_cast<QWidget *>(watched);
            if (widget != this && !isAncestorOf(widget)) {
                const bool onToggle = m_toggleButton
                                      && (widget == m_toggleButton
                                          || m_toggleButton->isAncestorOf(widget));
                close();
                return onToggle;
            }
        }
        if (event->type() == QEvent::ApplicationDeactivate)
            close();
        // A row's whole description (the row cuts it short), in the theme's tooltip
        // laid out like the documentation hovers (which wraps long text itself).
        if (m_list && watched == m_list->viewport() && event->type() == QEvent::ToolTip) {
            auto *help = static_cast<QHelpEvent *>(event);
            if (QListWidgetItem *item = m_list->itemAt(help->pos())) {
                const QString text
                    = item->data(Qt::UserRole + 1).toString().simplified().toHtmlEscaped();
                Utils::ToolTip::show(help->globalPos(),
                                     "<table><tr><td style=\"padding:6px;\">" + text
                                         + "</td></tr></table>",
                                     m_list->viewport(), {}, m_list->visualItemRect(item));
            } else {
                Utils::ToolTip::hide();
            }
            return true;
        }
        return QFrame::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        StyleHelper::drawCardBg(&p, QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                creatorColor(Theme::Token_Background_Default),
                                QPen(creatorColor(Theme::Token_Stroke_Subtle), 1), RadiusM);
    }

private:
    // A command's name, then its description in the muted color, on one line.
    class CommandDelegate : public QStyledItemDelegate
    {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;

        void paint(QPainter *painter, const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
        {
            QStyleOptionViewItem opt = option;
            initStyleOption(&opt, index);
            painter->save();
            const QRect row = opt.rect.adjusted(0, 1, -kScrollBarGap, -1);
            if (opt.state & QStyle::State_Selected) {
                painter->setRenderHint(QPainter::Antialiasing);
                StyleHelper::drawCardBg(painter, row,
                                        creatorColor(Theme::Token_Foreground_Subtle),
                                        Qt::NoPen, RadiusS);
            }
            const QRect text = row.adjusted(PaddingHM, 0, -PaddingHM, 0);
            const QString name = opt.text;
            painter->setFont(opt.font);
            painter->setPen(creatorColor(Theme::Token_Text_Default));
            const int nameWidth = qMin(opt.fontMetrics.horizontalAdvance(name), text.width());
            painter->drawText(text, Qt::AlignLeft | Qt::AlignVCenter,
                              opt.fontMetrics.elidedText(name, Qt::ElideRight, text.width()));
            const QRect rest = text.adjusted(nameWidth + PaddingHL, 0, 0, 0);
            if (rest.width() > 20) {
                painter->setPen(creatorColor(Theme::Token_Text_Muted));
                const QString description = index.data(Qt::UserRole + 1).toString().simplified();
                painter->drawText(rest, Qt::AlignLeft | Qt::AlignVCenter,
                                  opt.fontMetrics.elidedText(description, Qt::ElideRight, rest.width()));
            }
            painter->restore();
        }

        QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
        {
            QSize size = QStyledItemDelegate::sizeHint(option, index);
            size.setHeight(rowHeight(option.fontMetrics));
            return size;
        }
    };

    void pick(QListWidgetItem *item)
    {
        if (!item || item->isHidden())
            return;
        const QString name = item->data(Qt::UserRole).toString();
        close();
        m_onPicked(name);
    }

    QLineEdit *m_filter = nullptr;
    QListWidget *m_list = nullptr;
    QPointer<QWidget> m_toggleButton;
    std::function<void(const QString &)> m_onPicked;
};

// The strip above the input showing a queued message.
class QueueBar : public QWidget
{
protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        StyleHelper::drawCardBg(
            &p, rect(), creatorColor(Theme::Token_Foreground_Subtle), QPen(Qt::NoPen), RadiusS);
    }
};
// OPENMV-DIFF //

class ContextItem : public QWidget
{
    Q_OBJECT
public:
    // OPENMV-DIFF //
    // explicit ContextItem(const QString &text, QWidget *parent = nullptr)
    explicit ContextItem(const QString &text, QWidget *parent = nullptr,
                         const QPixmap &thumbnail = {})
    // OPENMV-DIFF //
        : QWidget(parent)
    {
        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(PaddingHM, 0, 0, 0);
        layout->setSpacing(0);
        // OPENMV-DIFF //
        if (!thumbnail.isNull()) {
            auto *image = new QLabel;
            image->setPixmap(thumbnail);
            layout->addWidget(image);
            layout->addSpacing(GapHS);
        }
        // OPENMV-DIFF //

        auto *label = new QLabel(text);
        layout->addWidget(label);

        // OPENMV-DIFF //
        // auto *closeBtn = new QtcIconButton();
        auto *closeBtn = new ChipCloseButton();
        // OPENMV-DIFF //
        closeBtn->setIcon(Icons::CLOSE_TOOLBAR.icon());
        closeBtn->setContentsMargins(0, 0, 0, 0);
        closeBtn->setToolTip(Tr::tr("Remove Context"));
        connect(closeBtn, &QToolButton::clicked, this, &ContextItem::removeRequested);
        layout->addWidget(closeBtn);
    }

signals:
    void removeRequested();

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        StyleHelper::drawCardBg(
            &p, rect(), creatorColor(Theme::Token_Foreground_Subtle), QPen(Qt::NoPen), RadiusS);
    }
};

// OPENMV-DIFF //
// The message view's scroll bar, shown beside the whole panel (messages and input
// area) so it runs the panel's full height and ends at the bottom. It mirrors the
// view's own scroll bar, which is hidden but still drives auto-scrolling.
class PanelScrollBar : public QScrollBar
{
public:
    explicit PanelScrollBar(QScrollBar *source)
        : QScrollBar(Qt::Vertical)
        , m_source(source)
    {
        // Follows the view's bar without echoing back: copying its value before its
        // new range clamped it to the old one, and passing that back scrolled the
        // chat a few pixels off for a frame on every resize (a flicker).
        connect(source, &QScrollBar::rangeChanged, this, [this] { sync(); });
        connect(source, &QScrollBar::valueChanged, this, [this] { sync(); });
        // Only the user's own moves (drag, click, wheel) go the other way.
        connect(this, &QScrollBar::valueChanged, this, [this](int value) {
            if (!m_syncing)
                m_source->setValue(value);
        });
        sync();
    }

private:
    void sync()
    {
        const QScopedValueRollback guard(m_syncing, true);
        setRange(m_source->minimum(), m_source->maximum());
        setPageStep(m_source->pageStep());
        setSingleStep(m_source->singleStep());
        setValue(m_source->value());
        setVisible(m_source->maximum() > m_source->minimum());
    }

    bool m_syncing = false;

    QScrollBar *m_source;
};
// OPENMV-DIFF //

ChatPanel::ChatPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // --- Session toolbar (StyledBar) ---
    auto *toolbar = new StyledBar;
    auto *toolbarLayout = new QHBoxLayout(toolbar);
    // OPENMV-DIFF //
    // toolbarLayout->setContentsMargins(PaddingHM, 0, PaddingHM, 0);
    // OPENMV-DIFF //
    // The last combo sits flush with the right edge, like the Frame Buffer bar's.
    toolbarLayout->setContentsMargins(PaddingHM, 0, 0, 0);
    // OPENMV-DIFF //
    toolbarLayout->setSpacing(GapHM);

    m_agentLabel = new QLabel;
    m_agentLabel->setTextFormat(Qt::RichText);
    toolbarLayout->addWidget(m_agentLabel);
    // OPENMV-DIFF //
    // The agent on the left, its settings on the right (like the editor toolbar's
    // symbol and Line/Col); the stretch moved here from before the overflow button.
    toolbarLayout->addStretch(1);
    m_sessionToolBar = toolbar;
    // OPENMV-DIFF //

    m_configOptionsWidget = new QWidget;
    m_configOptionsLayout = new QHBoxLayout(m_configOptionsWidget);
    m_configOptionsLayout->setContentsMargins(0, 0, 0, 0);
    m_configOptionsLayout->setSpacing(GapHS);
    toolbarLayout->addWidget(m_configOptionsWidget);

    m_configOverflowButton = new QToolButton;
    m_configOverflowButton->setIcon(Icons::SETTINGS_TOOLBAR.icon());
    m_configOverflowButton->setToolTip(Tr::tr("Configuration Options"));
    m_configOverflowButton->setAutoRaise(true);
    m_configOverflowButton->hide();
    connect(m_configOverflowButton, &QToolButton::clicked, this, [this] {
        auto *menu = new QMenu(m_configOverflowButton);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        for (const QString &id : std::as_const(m_configOrder)) {
            QComboBox *combo = m_configCombos.value(id);
            QLabel *label = m_configLabels.value(id);
            if (!combo || !label || combo->count() == 0)
                continue;
            QString title = label->text();
            if (title.endsWith(QLatin1Char(':')))
                title.chop(1);
            QMenu *sub = menu->addMenu(title);
            for (int i = 0; i < combo->count(); ++i) {
                QAction *a = sub->addAction(combo->itemText(i));
                a->setCheckable(true);
                a->setChecked(i == combo->currentIndex());
                const QString value = combo->itemData(i).toString();
                if (value.isEmpty()) {
                    a->setEnabled(false); // group header
                    continue;
                }
                connect(a, &QAction::triggered, this, [combo, i] {
                    combo->setCurrentIndex(i);
                    emit combo->activated(i);
                });
            }
        }
        menu->popup(QCursor::pos());
    });
    // OPENMV-DIFF //
    // toolbarLayout->addStretch(1);
    // OPENMV-DIFF //
    toolbarLayout->addWidget(m_configOverflowButton);
    // OPENMV-DIFF //
    m_contextGauge = new ContextGauge;
    connect(m_contextGauge, &QAbstractButton::clicked, this, [this] {
        if (m_contextGauge->isClickable() && !m_prompting)
            emit sendRequested(QStringLiteral("/compact"));
    });
    toolbarLayout->addWidget(m_contextGauge); // at the bar's right edge
    // OPENMV-DIFF //

    // --- Message view ---
    m_messageView = new AcpMessageView;
    m_messageView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_messageView, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        // OPENMV-DIFF //
        // Only agents that stream their reasoning have thoughts to show or hide.
        if (!m_messageView->hasThoughts())
            return;
        // OPENMV-DIFF //
        QMenu menu(m_messageView);
        QAction *action = menu.addAction(Tr::tr("Show Thoughts"));
        action->setCheckable(true);
        action->setChecked(m_messageView->thoughtsVisible());
        connect(action, &QAction::toggled, m_messageView, &AcpMessageView::setThoughtsVisible);
        menu.exec(m_messageView->mapToGlobal(pos));
    });
    // OPENMV-DIFF //
    // layout->addWidget(m_messageView, 1);
    // layout->addWidget(new Core::FindToolBarPlaceHolder(m_messageView));
    // Messages, find bar and input area form one column, with the message view's
    // scroll bar beside all of it (see PanelScrollBar).
    auto *column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(m_messageView, 1);
    column->addWidget(new Core::FindToolBarPlaceHolder(m_messageView));
    m_messageView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // OPENMV-DIFF //
    // OPENMV-DIFF //
    // layout->addWidget(toolbar);
    // OPENMV-DIFF //

    // --- Input bar (rounded container) ---
    auto *inputOuter = new QWidget;
    auto *inputOuterLayout = new QVBoxLayout(inputOuter);
    inputOuterLayout->setContentsMargins(PaddingHM, PaddingVXs, PaddingHM, PaddingVM);
    inputOuterLayout->setSpacing(GapVXs);

    auto *inputContainer = new InputContainerWidget;
    m_inputBox = inputContainer; // OPENMV-DIFF: the command palette pops up over it
    auto *inputContainerLayout = new QVBoxLayout(inputContainer);
    inputContainerLayout->setContentsMargins(PaddingHM, PaddingVXs, PaddingHM, PaddingVXs);
    inputContainerLayout->setSpacing(GapVXs);

    m_inputEdit = new ChatInputEdit;
    inputContainerLayout->addWidget(m_inputEdit);
    // OPENMV-DIFF: the attachments (context bar) go above the text, see below

    auto *bottomRow = new QWidget;
    auto *bottomRowLayout = new QHBoxLayout(bottomRow);
    bottomRowLayout->setContentsMargins(0, 0, 0, 0);
    bottomRowLayout->setSpacing(GapHS);

    m_contextBar = new QWidget;
    Layouting::Flow{}.attachTo(m_contextBar);
    m_contextBarLayout = m_contextBar->layout();
    m_contextBarLayout->setContentsMargins(0, 0, 0, 0);
    m_contextBarLayout->setSpacing(GapHS);

    connect(Core::EditorManager::instance(), &Core::EditorManager::currentEditorChanged, this, [this] {
        updateContextBar();
    });

    // OPENMV-DIFF //
    // QVBoxLayout *verticalCenter = new QVBoxLayout;
    // verticalCenter->addStretch();
    // verticalCenter->addWidget(m_contextBar);
    // verticalCenter->addStretch();
    // bottomRowLayout->addLayout(verticalCenter, 1);
    // Attachments sit inside the input box, above the text, and only take room
    // while there are some.
    inputContainerLayout->insertWidget(0, m_contextBar);
    bottomRowLayout->addStretch(1);
    // OPENMV-DIFF //

    auto addContextButton = new QtcButton("+", QtcButton::MediumTertiary);
    addContextButton->setToolTip(Tr::tr("Add Context"));
    connect(addContextButton, &QtcIconButton::released, this, [this, addContextButton] {
        auto menu = new QMenu(addContextButton);
        menu->setAttribute(Qt::WA_DeleteOnClose);

        menu->clear();

        auto *addFileAction = menu->addAction(Tr::tr("Add File..."));
        connect(addFileAction, &QAction::triggered, this, [this] {
            const FilePath fp = Utils::FileUtils::getOpenFilePath(
                Tr::tr("Add Context File"), {}, {}, nullptr, {}, false, false);
            if (fp.isEmpty())
                return;
            if (!m_manualContextFiles.contains(fp)) {
                m_manualContextFiles.append(fp);
                updateContextBar();
            }
        });
        auto *addRemoteFileAction = menu->addAction(Tr::tr("Add Remote File..."));
        connect(addRemoteFileAction, &QAction::triggered, this, [this] {
            const FilePath fp = Utils::FileUtils::getOpenFilePath(
                Tr::tr("Add Context File"), {}, {}, nullptr, {}, false, true);
            if (fp.isEmpty())
                return;
            if (!m_manualContextFiles.contains(fp)) {
                m_manualContextFiles.append(fp);
                updateContextBar();
            }
        });

        if (!m_includeCurrentEditorContext) {
            menu->addSeparator();
            auto *action = menu->addAction(Tr::tr("Current Editor"));
            connect(action, &QAction::triggered, this, [this] {
                m_includeCurrentEditorContext = true;
                updateContextBar();
            });
        }

        menu->popup(QCursor::pos());
    });

    auto bottomRowButtonLayout = new QHBoxLayout;
    bottomRowButtonLayout->addWidget(addContextButton);
    // OPENMV-DIFF //
    // Files, folders and images are attached by pasting or dropping them instead.
    addContextButton->hide();
    // OPENMV-DIFF //

    // OPENMV-DIFF //
    // m_commandsButton = new QtcButton("/", QtcButton::MediumTertiary);
    m_commandsButton = new ChatActionButton(ChatActionButton::Commands);
    // OPENMV-DIFF //
    m_commandsButton->setToolTip(Tr::tr("Insert Command"));
    m_commandsButton->hide();
    // OPENMV-DIFF //
    // connect(m_commandsButton, &QAbstractButton::clicked, this, [this] {
    //     if (!m_commandsMenu)
    //         return;
    //     const QPoint topLeft = m_commandsButton->mapToGlobal(QPoint(0, 0));
    //     const QSize menuSize = m_commandsMenu->sizeHint();
    //     m_commandsMenu->popup(QPoint(topLeft.x(), topLeft.y() - menuSize.height()));
    // });
    connect(m_commandsButton, &QAbstractButton::clicked, this, &ChatPanel::showCommandPalette);
    // OPENMV-DIFF //
    // OPENMV-DIFF //
    // bottomRowButtonLayout->addWidget(m_commandsButton);
    //
    // m_sendButton = new QtcButton(Tr::tr("Send"), QtcButton::MediumPrimary);
    // m_sendButton->setEnabled(false);
    // bottomRowButtonLayout->addWidget(m_sendButton);
    // auto alignTop = new QVBoxLayout;
    // alignTop->addLayout(bottomRowButtonLayout);
    // alignTop->addStretch();
    // bottomRowLayout->addLayout(alignTop);
    m_sendButton = new ChatActionButton(ChatActionButton::Send);
    m_sendButton->setToolTip(Tr::tr("Send (Enter)"));
    m_sendButton->setEnabled(false);
    // The buttons sit beside the text, at the bottom of the box as it grows; the
    // row under the box is gone.
    inputContainerLayout->removeWidget(m_inputEdit);
    auto *editRow = new QHBoxLayout;
    editRow->setContentsMargins(0, 0, 0, 0);
    editRow->setSpacing(GapHS);
    editRow->addWidget(m_inputEdit, 1, Qt::AlignVCenter);
    auto *buttonColumn = new QVBoxLayout;
    buttonColumn->setContentsMargins(0, 0, 0, 0);
    buttonColumn->addStretch();
    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(GapHXs);
    buttons->addWidget(m_commandsButton);
    buttons->addWidget(m_sendButton);
    buttonColumn->addLayout(buttons);
    editRow->addLayout(buttonColumn);
    inputContainerLayout->addLayout(editRow);
    bottomRow->hide();

    // Esc in the input stops the agent while it works (claimed there, ahead of the
    // IDE's own Esc action, only then).
    m_inputEdit->setEscapeHandler([this] {
        if (!m_prompting)
            return false;
        requestCancel();
        return true;
    });
    // OPENMV-DIFF //

    // OPENMV-DIFF //
    m_queueBar = new QueueBar;
    auto *queueLayout = new QVBoxLayout(m_queueBar);
    queueLayout->setContentsMargins(PaddingHM, PaddingVXs, PaddingHXs, PaddingVXs);
    queueLayout->setSpacing(0);
    m_queueBar->hide();
    inputOuterLayout->addWidget(m_queueBar);
    // OPENMV-DIFF //

    inputOuterLayout->addWidget(inputContainer);
    inputOuterLayout->addWidget(bottomRow);
    // OPENMV-DIFF //
    // layout->addWidget(inputOuter);
    // OPENMV-DIFF //
    column->addWidget(inputOuter);
    auto *body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    body->addLayout(column, 1);
    body->addWidget(new PanelScrollBar(m_messageView->verticalScrollBar()));
    layout->addLayout(body, 1);
    // OPENMV-DIFF //

    // --- Connections ---
    // OPENMV-DIFF //
    m_inputEdit->setAttachHandler([this](const QMimeData *mimeData) {
        return attachFromMimeData(mimeData);
    });
    setAcceptDrops(true);
    // OPENMV-DIFF //
    connect(m_inputEdit, &ChatInputEdit::sendRequested, this, [this] {
        const QString text = m_inputEdit->toPlainText().trimmed();
        // OPENMV-DIFF //
        // if (!text.isEmpty()) {
        // The input stays usable while the agent works: a message written then is
        // queued, and sent when it's done.
        if (!m_sendButton->isEnabled())
            return;
        if (m_prompting) {
            if (!text.isEmpty() || hasAttachments())
                queueMessage(text);
            return;
        }
        if (!text.isEmpty() || hasAttachments()) {
        // OPENMV-DIFF //
            m_inputEdit->clear();
            emit sendRequested(text);
        }
    });

    connect(m_sendButton, &QPushButton::clicked, this, [this] {
        if (m_prompting) {
            // OPENMV-DIFF //
            // emit cancelRequested();
            requestCancel();
            // OPENMV-DIFF //
        } else {
            const QString text = m_inputEdit->toPlainText().trimmed();
            // OPENMV-DIFF //
            // if (!text.isEmpty()) {
            if (!text.isEmpty() || hasAttachments()) {
            // OPENMV-DIFF //
                m_inputEdit->clear();
                emit sendRequested(text);
            }
        }
    });

    // Config option combo connections are made dynamically in updateConfigOptions()
    updateContextBar();
}

void ChatPanel::setAgentInfo(const QString &name, const QString &version,
                             const QString &iconUrl)
{
    m_messageView->setAgentIconUrl(iconUrl);

    QString html;
    if (!name.isEmpty()) {
        html = QStringLiteral("<b>%1</b>").arg(name.toHtmlEscaped());
        if (!version.isEmpty())
            html += QStringLiteral(" <small>v%1</small>").arg(version.toHtmlEscaped());
    }
    m_agentLabel->setText(html);
}

void ChatPanel::setPrompting(bool prompting)
{
    m_prompting = prompting;
    // OPENMV-DIFF //
    // m_sendButton->setText(prompting ? Tr::tr("Cancel") : Tr::tr("Send"));
    // m_sendButton->setRole(prompting ? QtcButton::MediumTertiary : QtcButton::MediumPrimary);
    // m_inputEdit->setEnabled(!prompting);
    m_sendButton->setKind(prompting ? ChatActionButton::Stop : ChatActionButton::Send);
    m_sendButton->setToolTip(prompting ? Tr::tr("Stop (Esc)") : Tr::tr("Send (Enter)"));
    m_contextGauge->setClickable(m_canCompact && !prompting);
    if (!prompting && !m_queue.isEmpty())
        QTimer::singleShot(0, this, [this] { sendQueuedMessages(); });
    // OPENMV-DIFF //
    m_messageView->setPrompting(prompting);
}

void ChatPanel::setSendEnabled(bool enabled)
{
    m_sendButton->setEnabled(enabled);
}

// OPENMV-DIFF //
// void ChatPanel::updateConfigOptions(const QList<SessionConfigOption> &configOptions)
// {
//     for (const SessionConfigOption &option : configOptions) {
void ChatPanel::updateConfigOptions(const QList<SessionConfigOption> &agentOptions)
{
    // Model first, then Mode, then the rest in the agent's order.
    const auto rank = [](const SessionConfigOption &option) {
        const auto category = option.category();
        if (category ? *category == SessionConfigOptionCategory::model : option.id() == "model")
            return 0;
        if (category ? *category == SessionConfigOptionCategory::mode : option.id() == "mode")
            return 1;
        return 2;
    };
    QList<SessionConfigOption> configOptions = agentOptions;
    std::stable_sort(configOptions.begin(), configOptions.end(),
                     [rank](const SessionConfigOption &a, const SessionConfigOption &b) {
                         return rank(a) < rank(b);
                     });
    for (const SessionConfigOption &option : configOptions) {
    // OPENMV-DIFF //
        auto select = fromJson<SessionConfigSelect>(QJsonValue(toJson(option)));
        if (!select)
            continue;

        const QString configId = option.id();

        // Find or create a combo for this config option
        QComboBox *combo = m_configCombos.value(configId);
        if (!combo) {
            auto *label = new ElidingLabel(option.name() + QLatin1Char(':'));
            label->setSizePolicy(QSizePolicy::Preferred, label->sizePolicy().verticalPolicy());
            label->setMinimumWidth(0);
            m_configOptionsLayout->addWidget(label);
            m_configLabels.insert(configId, label);
            m_configOrder.append(configId);
            combo = new QComboBox;
            combo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
            combo->setMinimumContentsLength(3);
            QSizePolicy policy = combo->sizePolicy();
            policy.setHorizontalPolicy(QSizePolicy::Preferred);
            combo->setSizePolicy(policy);

            if (option.description().has_value())
                combo->setToolTip(*option.description());
            m_configOptionsLayout->addWidget(combo);
            m_configCombos.insert(configId, combo);

            connect(combo, &QComboBox::activated, this, [this, configId, combo](int index) {
                const QString value = combo->itemData(index).toString();
                if (!value.isEmpty())
                    emit configOptionChanged(configId, value);
            });
        }

        const QSignalBlocker blocker(combo);
        combo->clear();

        int currentIndex = 0;
        const QString currentValue = select->currentValue();

        const auto &opts = select->options();
        if (auto *flatOptions = std::get_if<QList<SessionConfigSelectOption>>(&opts)) {
            for (const SessionConfigSelectOption &opt : *flatOptions) {
                combo->addItem(opt.name(), opt.value());
                if (opt.value() == currentValue)
                    currentIndex = combo->count() - 1;
                if (auto tooltip = opt.description())
                    combo->setItemData(combo->count() - 1, *tooltip, Qt::ToolTipRole);
            }
        } else if (auto *groups = std::get_if<QList<SessionConfigSelectGroup>>(&opts)) {
            for (const SessionConfigSelectGroup &group : *groups) {
                combo->addItem(QStringLiteral("\u2014 %1 \u2014").arg(group.name()));
                combo->setItemData(combo->count() - 1, 0, Qt::UserRole - 1);
                for (const SessionConfigSelectOption &opt : group.options()) {
                    combo->addItem(QStringLiteral("  %1").arg(opt.name()), opt.value());
                    if (opt.value() == currentValue)
                        currentIndex = combo->count() - 1;
                    if (auto tooltip = opt.description())
                        combo->setItemData(combo->count() - 1, *tooltip, Qt::ToolTipRole);
                }
            }
        }

        combo->setCurrentIndex(currentIndex);
        combo->setVisible(combo->count() > 0);
    }
    // OPENMV-DIFF //
    updateComboDividers();
    // OPENMV-DIFF //
    updateConfigOverflow();
}

// OPENMV-DIFF //
// The toolbar style draws a divider on each combo's right edge: keep the ones
// between combos, and after the last one only while the context gauge follows it
// (otherwise it would sit at the bar's edge).
void ChatPanel::updateComboDividers()
{
    QComboBox *lastCombo = nullptr;
    for (const QString &id : std::as_const(m_configOrder)) {
        if (QComboBox *combo = m_configCombos.value(id); combo && combo->count() > 0)
            lastCombo = combo;
    }
    const bool gaugeShown = !m_contextGauge->isHidden();
    for (QComboBox *combo : std::as_const(m_configCombos)) {
        combo->setProperty(Utils::StyleHelper::C_HIDE_BORDER, combo == lastCombo && !gaugeShown);
        combo->update();
    }
}
// OPENMV-DIFF //

void ChatPanel::clear()
{
    unqueueAll(); // OPENMV-DIFF: not sent to the next session
    m_messageView->clear();
    // OPENMV-DIFF //
    m_contextGauge->hide(); // until the next session reports usage
    updateComboDividers();
    // OPENMV-DIFF //
}

// OPENMV-DIFF //
void ChatPanel::setUsage(qint64 used, qint64 size, double cost, const QString &currency)
{
    if (size <= 0) {
        m_contextGauge->hide();
        updateComboDividers();
        return;
    }
    const QLocale locale;
    const double fraction = double(used) / double(size);
    QString toolTip = Tr::tr("Context: %1 of %2 tokens used (%3%)")
                          .arg(locale.toString(used), locale.toString(size))
                          .arg(qRound(fraction * 100));
    if (cost >= 0) {
        toolTip += QLatin1Char('\n')
                   + Tr::tr("Session cost: %1")
                         .arg(currency == QLatin1String("USD")
                                  ? QString(QLatin1Char('$') + locale.toString(cost, 'f', 2))
                                  : QString(locale.toString(cost, 'f', 2) + QLatin1Char(' ') + currency));
    }
    if (m_canCompact)
        toolTip += QLatin1Char('\n') + Tr::tr("Click to compact the conversation.");
    m_contextGauge->setFraction(fraction);
    m_contextGauge->setToolTip(toolTip);
    m_contextGauge->setClickable(m_canCompact && !m_prompting);
    if (m_contextGauge->isHidden()) {
        m_contextGauge->show();
        updateComboDividers();
    }
}
// OPENMV-DIFF //

void ChatPanel::clearConfigOptions()
{
    qDeleteAll(m_configCombos);
    m_configCombos.clear();
    m_configLabels.clear();
    m_configOrder.clear();
    // Also remove any labels added to the config options layout
    while (QLayoutItem *item = m_configOptionsLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    updateConfigOverflow();
}

void ChatPanel::updateConfigOverflow()
{
    if (!m_configOptionsWidget || !m_configOverflowButton)
        return;
    const bool hasAny = !m_configCombos.isEmpty();
    if (!hasAny) {
        m_configOptionsWidget->setVisible(false);
        m_configOverflowButton->setVisible(false);
        return;
    }
    // Re-enable widget so its sizeHint reflects natural width
    m_configOptionsWidget->setVisible(true);
    const int agentW = m_agentLabel ? m_agentLabel->sizeHint().width() : 0;
    const int configW = m_configOptionsWidget->sizeHint().width();
    const int reserved = agentW + PaddingHM * 2 + GapHM * 3;
    const bool collapse = (configW + reserved) > width();
    m_configOptionsWidget->setVisible(!collapse);
    m_configOverflowButton->setVisible(collapse);
}

void ChatPanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateConfigOverflow();
}

// OPENMV-DIFF //
// void ChatPanel::addUserMessage(const QString &text)
// {
//     m_messageView->addUserMessage(text);
// }
void ChatPanel::addUserMessage(const QString &text, const QList<QImage> &images,
                               const QList<FilePath> &files)
{
    m_messageView->addUserMessage(text, images, files);
}

void ChatPanel::queueMessage(const QString &text)
{
    m_queue.append({text, std::exchange(m_manualContextFiles, {}), std::exchange(m_images, {})});
    m_inputEdit->clear();
    updateContextBar();
    updateQueueBar();
}

void ChatPanel::unqueueMessage(int index)
{
    if (index < 0 || index >= m_queue.size())
        return;
    const OutgoingMessage queued = m_queue.takeAt(index);
    const QString draft = m_inputEdit->toPlainText().trimmed();
    m_inputEdit->setPlainText(draft.isEmpty() ? queued.text : queued.text + "\n\n" + draft);
    m_inputEdit->moveCursor(QTextCursor::End);
    for (const FilePath &file : queued.files) {
        if (!m_manualContextFiles.contains(file))
            m_manualContextFiles.append(file);
    }
    m_images = queued.images + m_images;
    updateContextBar();
    updateQueueBar();
}

void ChatPanel::unqueueAll()
{
    while (!m_queue.isEmpty())
        unqueueMessage(m_queue.size() - 1); // last first, so they end up in order
}

void ChatPanel::sendQueuedMessages()
{
    if (m_queue.isEmpty() || m_prompting || !m_sendButton->isEnabled())
        return;
    const QList<OutgoingMessage> queue = std::exchange(m_queue, {});
    updateQueueBar();

    // One prompt with everything, so the agent sees them all at once.
    QStringList texts;
    QList<FilePath> files;
    QList<PromptImage> images;
    for (const OutgoingMessage &message : queue) {
        if (!message.text.isEmpty())
            texts.append(message.text);
        for (const FilePath &file : message.files) {
            if (!files.contains(file))
                files.append(file);
        }
        images.append(message.images);
    }
    // The queued attachments go with it; whatever is attached in the input now
    // stays there for the next message.
    const QList<FilePath> draftFiles = std::exchange(m_manualContextFiles, files);
    const QList<PromptImage> draftImages = std::exchange(m_images, images);
    m_sentParts = queue.size() > 1 ? queue : QList<OutgoingMessage>();
    emit sendRequested(texts.join("\n\n")); // takes the attachments and the parts
    m_sentParts.clear();
    m_manualContextFiles = draftFiles;
    m_images = draftImages;
    updateContextBar();
}

void ChatPanel::updateQueueBar()
{
    QLayout *queueLayout = m_queueBar->layout();
    while (QLayoutItem *item = queueLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    for (int i = 0; i < m_queue.size(); ++i) {
        const OutgoingMessage &message = m_queue.at(i);
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(GapHS);

        QString text = message.text.simplified();
        const int attachments = message.files.size() + message.images.size();
        if (attachments > 0) {
            const QString count = Tr::tr("%n attachment(s)", nullptr, attachments);
            text = text.isEmpty() ? count : text + QLatin1String(" (") + count + QLatin1Char(')');
        }
        auto *label = new ElidingLabel(Tr::tr("Queued: %1").arg(text));
        label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        label->setToolTip(Tr::tr("Sent when the agent is done."));
        QFont font = label->font();
        font.setItalic(true);
        label->setFont(font);
        QPalette palette = label->palette();
        palette.setColor(QPalette::WindowText, creatorColor(Theme::Token_Text_Muted));
        label->setPalette(palette);
        rowLayout->addWidget(label, 1);

        auto *unqueueButton = new ChipCloseButton;
        unqueueButton->setIcon(Icons::CLOSE_TOOLBAR.icon());
        unqueueButton->setToolTip(Tr::tr("Edit Queued Message"));
        connect(unqueueButton, &QAbstractButton::clicked, this, [this, i] {
            QMetaObject::invokeMethod(this, [this, i] { unqueueMessage(i); }, Qt::QueuedConnection);
        });
        rowLayout->addWidget(unqueueButton);
        queueLayout->addWidget(row);
    }
    m_queueBar->setVisible(!m_queue.isEmpty());
}

// Stopping the agent doesn't send the queued message: it goes back into the input.
void ChatPanel::requestCancel()
{
    unqueueAll();
    emit cancelRequested();
}

void ChatPanel::addUserFile(const FilePath &file)
{
    m_messageView->addUserFile(file);
}

void ChatPanel::addStatusMessage(const QString &text)
{
    m_messageView->addStatusMessage(text);
}

void ChatPanel::addUserImage(const QImage &image)
{
    m_messageView->addUserImage(image);
}

QList<FilePath> ChatPanel::takeContextFiles()
{
    const QList<FilePath> files = std::exchange(m_manualContextFiles, {});
    updateContextBar();
    return files;
}

QList<PromptImage> ChatPanel::takeImages()
{
    const QList<PromptImage> images = std::exchange(m_images, {});
    updateContextBar();
    return images;
}

static bool isImageFile(const FilePath &file)
{
    static const QStringList suffixes = {"png", "jpg", "jpeg", "gif", "bmp", "webp"};
    return suffixes.contains(file.suffix().toLower());
}

// Whether a paste or drop carries something to attach: local files or folders,
// or image data (a screenshot, an image copied from a browser).
static bool isAttachable(const QMimeData *mimeData)
{
    if (!mimeData)
        return false;
    // Office apps put a picture of copied text beside the text: that's text.
    if (mimeData->hasImage() && mimeData->text().trimmed().isEmpty())
        return true;
    return Utils::anyOf(mimeData->urls(), &QUrl::isLocalFile);
}

bool ChatPanel::attachFromMimeData(const QMimeData *mimeData)
{
    if (!isAttachable(mimeData))
        return false;

    bool attached = false;
    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile())
            continue;
        const FilePath file = FilePath::fromUserInput(url.toLocalFile());
        if (!file.exists())
            continue;
        attached = true;
        if (file.isFile() && isImageFile(file)) {
            const QImage image(file.toFSPathString());
            if (!image.isNull()) {
                attachImage(image, file);
                continue;
            }
        }
        if (!m_manualContextFiles.contains(file))
            m_manualContextFiles.append(file);
    }

    if (!attached && mimeData->hasImage() && mimeData->text().trimmed().isEmpty()) {
        const QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (!image.isNull()) {
            attachImage(image, {});
            attached = true;
        }
    }

    if (attached)
        updateContextBar();
    return attached;
}

static QByteArray encodeImage(const QImage &image, const char *format, int quality = -1)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format, quality);
    return data;
}

void ChatPanel::attachImage(const QImage &image, const FilePath &source)
{
    // Agents scale big images down anyway; sending them at most 2000 px keeps the
    // prompt (and the session history) light. Photos that stay big as PNG go as JPEG.
    constexpr int maxEdge = 2000;
    const QImage sent = qMax(image.width(), image.height()) > maxEdge
                            ? image.scaled(maxEdge, maxEdge, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation)
                            : image;
    PromptImage prompt;
    prompt.data = encodeImage(sent, "PNG");
    prompt.mimeType = "image/png";
    if (prompt.data.size() > 3 * 1024 * 1024) {
        prompt.data = encodeImage(sent.convertToFormat(QImage::Format_RGB888), "JPEG", 85);
        prompt.mimeType = "image/jpeg";
    }
    prompt.source = source;
    prompt.preview = image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_images.append(prompt);
}

void ChatPanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (isAttachable(event->mimeData()) && (event->possibleActions() & Qt::CopyAction)) {
        event->setDropAction(Qt::CopyAction); // never a move: the source file stays put
        event->accept();
        return;
    }
    QWidget::dragEnterEvent(event);
}

void ChatPanel::dragMoveEvent(QDragMoveEvent *event)
{
    if (isAttachable(event->mimeData()) && (event->possibleActions() & Qt::CopyAction)) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    QWidget::dragMoveEvent(event);
}

void ChatPanel::dropEvent(QDropEvent *event)
{
    if ((event->possibleActions() & Qt::CopyAction) && attachFromMimeData(event->mimeData())) {
        event->setDropAction(Qt::CopyAction);
        event->accept();
        m_inputEdit->setFocus();
        return;
    }
    QWidget::dropEvent(event);
}
// OPENMV-DIFF //

void ChatPanel::appendAgentText(const QString &text)
{
    m_messageView->appendAgentText(text);
}

void ChatPanel::appendAgentThought(const QString &text)
{
    m_messageView->appendAgentThought(text);
}

void ChatPanel::addToolCall(const ToolCall &toolCall)
{
    m_messageView->addToolCall(toolCall);
}

void ChatPanel::updateToolCall(const ToolCallUpdate &update)
{
    m_messageView->updateToolCall(update);
}

void ChatPanel::addPlan(const Plan &plan)
{
    m_messageView->addPlan(plan);
}

void ChatPanel::addErrorMessage(const QString &text)
{
    m_messageView->addErrorMessage(text);
}

void ChatPanel::finishAgentMessage()
{
    m_messageView->finishAgentMessage();
}

void ChatPanel::addPermissionRequest(const QJsonValue &id,
                                     const Acp::RequestPermissionRequest &request)
{
    m_messageView->addPermissionRequest(id, request);
    QApplication::alert(m_messageView);

    connect(m_messageView, &AcpMessageView::permissionOptionSelected,
            this, &ChatPanel::permissionOptionSelected, Qt::UniqueConnection);
    connect(m_messageView, &AcpMessageView::permissionCancelled,
            this, &ChatPanel::permissionCancelled, Qt::UniqueConnection);
}

void ChatPanel::addAuthenticationRequest(const QList<Acp::AuthMethod> &methods)
{
    m_messageView->addAuthenticationRequest(methods);
    connect(m_messageView, &AcpMessageView::authenticateRequested,
            this, &ChatPanel::authenticateRequested, Qt::UniqueConnection);
}

void ChatPanel::showAuthenticationError(const QString &error)
{
    m_messageView->showAuthenticationError(error);
}

void ChatPanel::resolveAuthentication()
{
    m_messageView->resolveAuthentication();
}

SessionPickerWidget *ChatPanel::addSessionPicker()
{
    return m_messageView->addSessionPicker();
}

void ChatPanel::updateAvailableCommands(const QList<AvailableCommand> &commands)
{
    QList<CommandInfo> commandInfos;
    commandInfos.reserve(commands.size());
    for (const AvailableCommand &cmd : commands) {
        const QString hint = cmd.input() ? Acp::hint(*cmd.input()) : QString();
        commandInfos.append({cmd.name(), cmd.description(), hint});
    }
    m_inputEdit->setAvailableCommands(commandInfos);
    // OPENMV-DIFF //
    m_canCompact = Utils::anyOf(commands, [](const AvailableCommand &cmd) {
        return cmd.name() == QLatin1String("compact");
    });
    m_contextGauge->setClickable(m_canCompact && !m_prompting);
    // OPENMV-DIFF //

    // OPENMV-DIFF //
    // delete m_commandsMenu;
    // m_commandsMenu = nullptr;
    // m_commandsButton->setVisible(!commands.isEmpty());
    //
    // if (commands.isEmpty())
    //     return;
    //
    // m_commandsMenu = new QMenu(m_commandsButton);
    // m_commandsMenu->setToolTipsVisible(true);
    // for (const AvailableCommand &cmd : commands) {
    //     QAction *action = m_commandsMenu->addAction(cmd.name());
    //     action->setToolTip(cmd.description());
    //     connect(action, &QAction::triggered, this, [this, cmd] {
    //         m_inputEdit->setPlainText('/' + cmd.name() + QLatin1Char(' '));
    //         m_inputEdit->moveCursor(QTextCursor::End);
    //         m_inputEdit->setFocus();
    //     });
    // }
    // A menu of a hundred-odd commands ran off the screen in columns: they go in
    // a filterable palette instead (showCommandPalette()).
    m_commands.clear();
    for (const AvailableCommand &cmd : commands)
        m_commands.append({cmd.name(), cmd.description()});
    m_commandsButton->setVisible(!commands.isEmpty());
    // OPENMV-DIFF //
}

// OPENMV-DIFF //
void ChatPanel::showCommandPalette()
{
    if (m_commands.isEmpty())
        return;
    auto *palette = new CommandPalette(m_commands, [this](const QString &name) {
        m_inputEdit->setPlainText('/' + name + QLatin1Char(' '));
        m_inputEdit->moveCursor(QTextCursor::End);
        m_inputEdit->setFocus();
    }, this);
    palette->setToggleButton(m_commandsButton);
    palette->popupAbove(m_inputBox);
}
// OPENMV-DIFF //

void ChatPanel::updateContextBar()
{
    // OPENMV-DIFF //
    const auto showIfAny = qScopeGuard([this] { m_contextBar->setVisible(m_contextBarLayout->count() > 0); });
    // OPENMV-DIFF //
    while (QLayoutItem *item = m_contextBarLayout->takeAt(0)) {
        delete item->widget();
        delete item;
    }

    // OPENMV-DIFF //
    // The open scripts go to the agent silently (see AcpChatController::sendPrompt),
    // so they get no chip here.
    // if (m_includeCurrentEditorContext) {
    if (false) {
    // OPENMV-DIFF //
        if (auto editorWidget = TextEditor::TextEditorWidget::currentTextEditorWidget()) {
            Utils::FilePath filePath = editorWidget->textDocument()->filePath();
            if (!filePath.isEmpty()) {
                const QString name = filePath.fileName();
                auto *item = new ContextItem(name, m_contextBar);
                connect(item, &ContextItem::removeRequested, this, [this, name] {
                    m_includeCurrentEditorContext = false;
                    QMetaObject::invokeMethod(this, [this] { updateContextBar(); }, Qt::QueuedConnection);
                });
                m_contextBarLayout->addWidget(item);
            }
        }
    }

    for (const Utils::FilePath &file : std::as_const(m_manualContextFiles)) {
        // OPENMV-DIFF //
        // auto *item = new ContextItem(file.fileName(), m_contextBar);
        auto *item = new ContextItem(file.isDir() ? file.fileName() + '/' : file.fileName(),
                                     m_contextBar);
        item->setToolTip(file.toUserOutput());
        // OPENMV-DIFF //
        connect(item, &ContextItem::removeRequested, this, [this, file] {
            m_manualContextFiles.removeOne(file);
            QMetaObject::invokeMethod(this, [this] { updateContextBar(); }, Qt::QueuedConnection);
        });
        m_contextBarLayout->addWidget(item);
    }

    // OPENMV-DIFF //
    const qreal dpr = devicePixelRatioF();
    const int thumbHeight = fontMetrics().height();
    for (int i = 0; i < m_images.size(); ++i) {
        const PromptImage &image = m_images.at(i);
        QPixmap thumbnail = QPixmap::fromImage(
            image.preview.scaled(qRound(thumbHeight * 2 * dpr), qRound(thumbHeight * dpr),
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation));
        thumbnail.setDevicePixelRatio(dpr);
        const QString name = image.source.isEmpty() ? Tr::tr("Image") : image.source.fileName();
        auto *item = new ContextItem(name, m_contextBar, thumbnail);
        if (!image.source.isEmpty())
            item->setToolTip(image.source.toUserOutput());
        const QByteArray id = image.data.left(64) + QByteArray::number(image.data.size());
        connect(item, &ContextItem::removeRequested, this, [this, i, id] {
            if (i < m_images.size()
                && m_images.at(i).data.left(64) + QByteArray::number(m_images.at(i).data.size()) == id)
                m_images.removeAt(i);
            QMetaObject::invokeMethod(this, [this] { updateContextBar(); }, Qt::QueuedConnection);
        });
        m_contextBarLayout->addWidget(item);
    }
    // OPENMV-DIFF //
}

} // namespace AcpClient::Internal

#include "chatpanel.moc"
