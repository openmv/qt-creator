// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// OPENMV-DIFF //
// Rewritten for the OpenMV IDE (see sessionpickerwidget.h).
// OPENMV-DIFF //

#include "sessionpickerwidget.h"

#include "acpclienttr.h"
#include "acpsettings.h"

#include <coreplugin/icore.h>

#include <utils/fileutils.h>
#include <utils/qtcsettings.h>
#include <utils/layoutbuilder.h>
#include <utils/qtcwidgets.h>
#include <utils/stylehelper.h>
#include <utils/theme/theme.h>
#include <utils/utilsicons.h>

#include <QAbstractButton>

#include <QContextMenuEvent>
#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QVBoxLayout>

using namespace Acp;
using namespace Utils;
using namespace Utils::StyleHelper::SpacingTokens;

namespace AcpClient::Internal {

// The list gets a filter field once it has more chats than this.
static constexpr int kFilterThreshold = 8;
// The newest this many show at first.
static constexpr int kRecentCount = 30;
// The folder "New Chat in a Folder..." last started a chat in.
const char kLastFolderKey[] = "AcpClient/SessionPicker/LastFolder";

static QString relativeTime(const QString &isoTimestamp)
{
    const QDateTime dt = QDateTime::fromString(isoTimestamp, Qt::ISODate).toLocalTime();
    if (!dt.isValid())
        return isoTimestamp;

    const qint64 secs = dt.secsTo(QDateTime::currentDateTime());
    const auto count = [](qint64 n, const QString &one, const QString &many) {
        return n == 1 ? one : many.arg(n);
    };
    if (secs < 60)
        return Tr::tr("just now");
    if (secs < 3600)
        return count(secs / 60, Tr::tr("1 minute ago"), Tr::tr("%1 minutes ago"));
    if (secs < 86400)
        return count(secs / 3600, Tr::tr("1 hour ago"), Tr::tr("%1 hours ago"));
    if (secs < 7 * 86400)
        return count(secs / 86400, Tr::tr("yesterday"), Tr::tr("%1 days ago"));
    return dt.toString(QStringLiteral("MMM d, yyyy"));
}

// A folder for display: "~" for the home folder, else as the user writes it.
static QString displayFolder(const QString &path)
{
    if (path.isEmpty())
        return {};
    QString shown = FilePath::fromUserInput(path).toUserOutput();
    const QString home = QDir::toNativeSeparators(QDir::homePath());
    if (shown.startsWith(home, Qt::CaseInsensitive))
        shown = QStringLiteral("~") + shown.mid(home.size());
    return shown;
}

static QColor muted()
{
    return creatorColor(Theme::Token_Text_Muted);
}

// ---------------------------------------------------------------------------
// SessionRow: one previous chat. Its title, then when and where (muted); a
// rounded highlight on hover. Right-click renames it.
// ---------------------------------------------------------------------------

class SessionRow : public QAbstractButton
{
public:
    SessionRow(const SessionInfo &session, QWidget *parent)
        : QAbstractButton(parent)
        , m_sessionId(session.sessionId())
        , m_cwd(FilePath::fromUserInput(session.cwd()))
        , m_agentTitle(session.title().value_or(Tr::tr("Untitled chat")).simplified())
        , m_when(session.updatedAt() ? relativeTime(*session.updatedAt()) : QString())
        , m_updated(session.updatedAt()
                        ? QDateTime::fromString(*session.updatedAt(), Qt::ISODate).toLocalTime()
                        : QDateTime())
        , m_folder(displayFolder(session.cwd()))
    {
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip(m_cwd.toUserOutput());
        updateTitle();
    }

    QString sessionId() const { return m_sessionId; }
    FilePath cwd() const { return m_cwd; }
    QDateTime updated() const { return m_updated; }

    bool matches(const QString &needle) const
    {
        return text().contains(needle, Qt::CaseInsensitive)
               || m_folder.contains(needle, Qt::CaseInsensitive);
    }

    QSize sizeHint() const override
    {
        return QSize(200, fontMetrics().height() + 2 * PaddingVS + 2);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (underMouse() && isEnabled()) {
            StyleHelper::drawCardBg(&p, rect(), creatorColor(Theme::Token_Foreground_Subtle),
                                    Qt::NoPen, RadiusS);
        }
        const QFontMetrics fm = fontMetrics();
        const QRect area = rect().adjusted(PaddingHM, 0, -PaddingHM, 0);
        const int gap = PaddingHL;

        // The title, then when (muted) beside it; where (muted) on the right in
        // what's left, shortened in the middle. A long title gives way only past
        // the folder's minimum.
        const int whenWidth = m_when.isEmpty() ? 0 : fm.horizontalAdvance(m_when) + gap;
        const int folderMin = m_folder.isEmpty() ? 0 : qMin(fm.horizontalAdvance(m_folder), 120) + gap;
        const int titleWidth = qMin(fm.horizontalAdvance(text()),
                                    area.width() - whenWidth - folderMin);
        // Shortened only when it doesn't fit (eliding to its exact width can cut
        // a title that fits).
        const QString title = fm.horizontalAdvance(text()) <= titleWidth
                                  ? text()
                                  : fm.elidedText(text(), Qt::ElideRight, qMax(0, titleWidth));

        p.setPen(creatorColor(Theme::Token_Text_Default));
        p.drawText(area, Qt::AlignLeft | Qt::AlignVCenter, title);
        p.setPen(muted());
        const int whenX = area.left() + fm.horizontalAdvance(title) + gap;
        if (!m_when.isEmpty())
            p.drawText(QRect(whenX, area.top(), area.right() - whenX, area.height()),
                       Qt::AlignLeft | Qt::AlignVCenter, m_when);
        const int folderLeft = whenX + (m_when.isEmpty() ? 0 : fm.horizontalAdvance(m_when) + gap);
        const int folderRoom = area.right() - folderLeft;
        if (!m_folder.isEmpty() && folderRoom > 0)
            p.drawText(QRect(folderLeft, area.top(), folderRoom, area.height()),
                       Qt::AlignRight | Qt::AlignVCenter,
                       fm.elidedText(m_folder, Qt::ElideMiddle, folderRoom));
    }

    // Rename from here too (the same names as the chat tabs use).
    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QMenu menu;
        QAction *rename = menu.addAction(Tr::tr("Rename Chat..."));
        if (menu.exec(event->globalPos()) != rename)
            return;
        bool ok = false;
        const QString current = AcpSettings::chatTitle(m_sessionId);
        const QString name = QInputDialog::getText(this, Tr::tr("Rename Chat"),
                                                   Tr::tr("Name (leave empty for automatic):"),
                                                   QLineEdit::Normal,
                                                   current.isEmpty() ? m_agentTitle : current,
                                                   &ok).simplified();
        if (!ok)
            return;
        AcpSettings::setChatTitle(m_sessionId, name == m_agentTitle ? QString() : name);
        updateTitle();
    }

private:
    // A name the user gave the chat wins over the agent's title.
    void updateTitle()
    {
        const QString custom = AcpSettings::chatTitle(m_sessionId);
        setText(custom.isEmpty() ? m_agentTitle : custom);
        update();
    }

    QString m_sessionId;
    FilePath m_cwd;
    QString m_agentTitle;
    QString m_when;
    QDateTime m_updated;
    QString m_folder;
};

class OlderChatsRow : public QAbstractButton
{
public:
    explicit OlderChatsRow(QWidget *parent)
        : QAbstractButton(parent)
    {
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        return QSize(200, fontMetrics().height() + 2 * PaddingVS + 2);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (underMouse()) {
            StyleHelper::drawCardBg(&p, rect(), creatorColor(Theme::Token_Foreground_Subtle),
                                    Qt::NoPen, RadiusS);
        }
        p.setPen(underMouse() ? creatorColor(Theme::Token_Text_Default) : muted());
        p.drawText(rect(), Qt::AlignCenter, text());
    }
};

// ---------------------------------------------------------------------------
// SessionPickerWidget
// ---------------------------------------------------------------------------

// A section heading in the agent page's style: muted text, then a hairline.
static QWidget *sectionHeader(QLabel *label)
{
    auto *header = new QWidget;
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 6, 0, 0);
    layout->setSpacing(6);
    QPalette pal = label->palette();
    pal.setColor(QPalette::WindowText, muted());
    label->setPalette(pal);
    layout->addWidget(label);
    layout->addWidget(Layouting::createHr(), 1);
    return header;
}

SessionPickerWidget::SessionPickerWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto *title = new QLabel(Tr::tr("Start a Chat"));
    QFont titleFont = title->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.3);
    titleFont.setBold(true);
    title->setFont(titleFont);
    title->setAlignment(Qt::AlignHCenter);
    layout->addWidget(title);
    layout->addWidget(Layouting::createHr());

    // New chat, here or in a folder of one's choice.
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    m_newButton = new QtcButton(Tr::tr("New Chat"), QtcButton::MediumTertiary);
    m_newButton->setPixmap(Icons::PLUS_TOOLBAR.pixmap());
    m_newButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(m_newButton, &QAbstractButton::clicked, this, [this] {
        if (m_firstNewSessionTarget)
            requestNewSession(m_firstNewSessionTarget->cwd);
    });
    buttons->addWidget(m_newButton, 1);
    auto *folderButton = new QtcButton(Tr::tr("New Chat in a Folder..."), QtcButton::MediumTertiary);
    folderButton->setPixmap(Icons::OPENFILE_TOOLBAR.pixmap());
    folderButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    connect(folderButton, &QAbstractButton::clicked,
            this, &SessionPickerWidget::requestCustomDirectorySession);
    buttons->addWidget(folderButton, 1);
    layout->addLayout(buttons);

    m_folderLabel = new QLabel;
    m_folderLabel->setAlignment(Qt::AlignHCenter);
    QPalette folderPal = m_folderLabel->palette();
    folderPal.setColor(QPalette::WindowText, muted());
    m_folderLabel->setPalette(folderPal);
    m_folderLabel->hide();
    layout->addWidget(m_folderLabel);

    // Previous chats: one list, newest first.
    m_listHeader = new QLabel(Tr::tr("Previous Chats"));
    layout->addWidget(sectionHeader(m_listHeader));

    // Styled like the command palette's filter: a row's height, rounded, its
    // text lined up with the rows'.
    m_filter = new QLineEdit;
    m_filter->setPlaceholderText(Tr::tr("Filter chats..."));
    m_filter->setClearButtonEnabled(true);
    m_filter->setFrame(false);
    m_filter->setFixedHeight(fontMetrics().height() + 2 * PaddingVS + 2);
    m_filter->setStyleSheet(QString("QLineEdit { background: %1; border-radius: %2px;"
                                    " padding-left: %3px; padding-right: %3px; }")
                                .arg(creatorColor(Theme::Token_Background_Muted).name())
                                .arg(RadiusS)
                                .arg(PaddingHM - 2));
    m_filter->hide();
    connect(m_filter, &QLineEdit::textChanged, this, &SessionPickerWidget::applyFilter);
    layout->addWidget(m_filter);

    m_rows = new QVBoxLayout;
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setSpacing(2);
    layout->addLayout(m_rows);

    // "Show N older chats": a row in the rows' style, muted, centered.
    m_olderRow = new OlderChatsRow(this);
    m_olderRow->hide();
    connect(m_olderRow, &QAbstractButton::clicked, this, [this] {
        m_showAll = true;
        applyFilter();
    });
    layout->addWidget(m_olderRow);

    m_emptyLabel = new QLabel(Tr::tr("Loading previous chats..."));
    m_emptyLabel->setAlignment(Qt::AlignHCenter);
    QPalette emptyPal = m_emptyLabel->palette();
    emptyPal.setColor(QPalette::WindowText, muted());
    m_emptyLabel->setPalette(emptyPal);
    layout->addWidget(m_emptyLabel);


    layout->addStretch();
}

void SessionPickerWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_newButton->setFocus(); // Enter starts a new chat
}

void SessionPickerWidget::setCurrentProjectDir(const FilePath &dir)
{
    m_currentProjectDir = dir;
}

void SessionPickerWidget::setNewSessionTargets(const QList<NewSessionTarget> &targets)
{
    m_firstNewSessionTarget.reset();
    if (!targets.isEmpty())
        m_firstNewSessionTarget = targets.first();
    m_newButton->setEnabled(m_firstNewSessionTarget.has_value());
    if (m_firstNewSessionTarget) {
        m_folderLabel->setText(Tr::tr("New chats start in %1")
                                   .arg(displayFolder(m_firstNewSessionTarget->cwd.toUserOutput())));
        m_folderLabel->show();
    } else {
        m_folderLabel->hide();
    }
}

void SessionPickerWidget::requestNewSession(const FilePath &cwd)
{
    if (m_resolved)
        return;
    m_resolved = true;
    setEnabled(false);
    emit newSessionRequested(cwd);
    deleteLater();
}

void SessionPickerWidget::requestCustomDirectorySession()
{
    if (m_resolved)
        return;
    // Starts where the last one was chosen, else in the chats folder.
    const FilePath last = FilePath::fromSettings(Core::ICore::settings()->value(kLastFolderKey));
    const FilePath start = last.isDir() ? last
                           : m_firstNewSessionTarget ? m_firstNewSessionTarget->cwd
                                                     : m_currentProjectDir;
    const FilePath dir = FileUtils::getExistingDirectory(Tr::tr("Choose a Folder for the Chat"),
                                                         start);
    if (dir.isEmpty())
        return;
    Core::ICore::settings()->setValue(kLastFolderKey, dir.toSettings());
    requestNewSession(dir);
}

void SessionPickerWidget::setInitialSessions(const QList<SessionInfo> &sessions,
                                             const std::optional<QString> &nextCursor)
{
    for (SessionRow *row : std::as_const(m_sessionRows))
        delete row;
    m_sessionRows.clear();
    m_listed = true;
    m_nextCursor = nextCursor.value_or(QString());
    addSessionRows(sessions);
    fetchMore();
}

void SessionPickerWidget::appendSessions(const QList<SessionInfo> &sessions,
                                         const std::optional<QString> &nextCursor)
{
    m_nextCursor = nextCursor.value_or(QString());
    addSessionRows(sessions);
    fetchMore();
}

// All of them (a list entry is small; the filter finds one): the next page as
// soon as one arrives.
void SessionPickerWidget::fetchMore()
{
    if (!m_nextCursor.isEmpty() && !m_resolved)
        emit loadMoreRequested(m_nextCursor);
}

// Newest first: the agent lists them that way, and each page continues it.
void SessionPickerWidget::addSessionRows(const QList<SessionInfo> &sessions)
{
    for (const SessionInfo &session : sessions) {
        auto *row = new SessionRow(session, this);
        connect(row, &QAbstractButton::clicked, this, [this, row] {
            if (m_resolved)
                return;
            emit sessionSelected(row->sessionId(), row->cwd());
        });
        m_rows->addWidget(row);
        m_sessionRows.append(row);
    }
    applyFilter();
}

void SessionPickerWidget::applyFilter()
{
    const QString needle = m_filter->text().trimmed();
    // Filtering searches them all; otherwise the newest kRecentCount show until
    // "Show N older chats" is clicked.
    int shown = 0;
    for (SessionRow *row : std::as_const(m_sessionRows)) {
        const bool visible = needle.isEmpty() ? (m_showAll || shown < kRecentCount)
                                              : row->matches(needle);
        row->setVisible(visible);
        if (visible)
            ++shown;
    }
    const int older = needle.isEmpty() && !m_showAll
                          ? qMax(0, int(m_sessionRows.size()) - kRecentCount)
                          : 0;
    m_olderRow->setText(older == 1 ? Tr::tr("Show 1 older chat")
                                   : Tr::tr("Show %1 older chats").arg(older));
    m_olderRow->setVisible(older > 0);
    updateState();
}

void SessionPickerWidget::updateState()
{
    const int count = m_sessionRows.size();
    m_listHeader->setText(count > 0 ? Tr::tr("Previous Chats (%1%2)")
                                          .arg(count)
                                          .arg(m_nextCursor.isEmpty() ? QString() : QStringLiteral("+"))
                                    : Tr::tr("Previous Chats"));
    m_filter->setVisible(count > kFilterThreshold);
    const bool anyShown = std::any_of(m_sessionRows.cbegin(), m_sessionRows.cend(),
                                      [](SessionRow *row) { return !row->isHidden(); });
    if (!m_listed)
        m_emptyLabel->setText(Tr::tr("Loading previous chats..."));
    else if (count == 0)
        m_emptyLabel->setText(Tr::tr("No previous chats."));
    else
        m_emptyLabel->setText(Tr::tr("No chats match."));
    m_emptyLabel->setVisible(!anyShown);
}

void SessionPickerWidget::setResolved(const QString &)
{
    if (m_resolved)
        return;
    m_resolved = true;
    setEnabled(false);
    deleteLater();
}

std::optional<SessionPickerWidget::NewSessionTarget> SessionPickerWidget::firstNewSessionTarget() const
{
    return m_firstNewSessionTarget;
}

} // namespace AcpClient::Internal
