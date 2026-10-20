// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// OPENMV-DIFF //
// Rewritten for the OpenMV IDE: a page of its own (not an item in the chat),
// styled like the agent page. "New Chat" (in the IDE's chats folder) and "New
// Chat in Folder..." at the top, then one list of the agent's previous chats,
// newest first, with a filter once it's long. The interface is unchanged.
// OPENMV-DIFF //

#pragma once

#include <acp/acp.h>

#include <utils/filepath.h>

#include <QWidget>

QT_BEGIN_NAMESPACE
class QAbstractButton;
class QLabel;
class QLineEdit;
class QVBoxLayout;
QT_END_NAMESPACE

namespace Utils { class QtcButton; }

namespace AcpClient::Internal {

class SessionRow;

class SessionPickerWidget : public QWidget
{
    Q_OBJECT

public:
    struct NewSessionTarget
    {
        QString label;
        Utils::FilePath cwd;
        QString tooltip;
    };

    explicit SessionPickerWidget(QWidget *parent = nullptr);

    void setCurrentProjectDir(const Utils::FilePath &dir);
    // The first target is where "New Chat" starts a chat.
    void setNewSessionTargets(const QList<NewSessionTarget> &targets);

    void setInitialSessions(const QList<Acp::SessionInfo> &sessions,
                            const std::optional<QString> &nextCursor);
    void appendSessions(const QList<Acp::SessionInfo> &sessions,
                        const std::optional<QString> &nextCursor);

    void setResolved(const QString &title);

    std::optional<NewSessionTarget> firstNewSessionTarget() const;

signals:
    void sessionSelected(const QString &sessionId, const Utils::FilePath &cwd);
    void loadMoreRequested(const QString &cursor);
    void newSessionRequested(const Utils::FilePath &cwd);

protected:
    void showEvent(QShowEvent *event) override;

private:
    void addSessionRows(const QList<Acp::SessionInfo> &sessions);
    void requestNewSession(const Utils::FilePath &cwd);
    void requestCustomDirectorySession();
    void applyFilter();
    void updateState();
    void fetchMore();

    Utils::QtcButton *m_newButton = nullptr;
    QLabel *m_folderLabel = nullptr;
    QLabel *m_listHeader = nullptr;
    QLineEdit *m_filter = nullptr;
    QVBoxLayout *m_rows = nullptr;
    QList<SessionRow *> m_sessionRows;
    // The newest few show; the rest behind one "Show N older chats" row (the
    // filter always searches them all).
    QAbstractButton *m_olderRow = nullptr;
    bool m_showAll = false;
    QLabel *m_emptyLabel = nullptr;
    Utils::FilePath m_currentProjectDir;
    QString m_nextCursor;
    bool m_listed = false;
    std::optional<NewSessionTarget> m_firstNewSessionTarget;
    bool m_resolved = false;
};

} // namespace AcpClient::Internal
