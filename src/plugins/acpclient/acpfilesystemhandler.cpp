// Copyright (C) 2025 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "acpfilesystemhandler.h"
#include "acpclientobject.h"

#include <utils/filepath.h>
#include <utils/result.h>

// OPENMV-DIFF //
#include <coreplugin/documentmanager.h>
#include <coreplugin/editormanager/documentmodel.h>

#include <texteditor/textdocument.h>

#include <QTextCursor>
#include <QTextDocument>
// OPENMV-DIFF //

#include <QLoggingCategory>

static Q_LOGGING_CATEGORY(logFs, "qtc.acpclient.filesystem", QtWarningMsg);

using namespace Acp;
using namespace Utils;

namespace AcpClient::Internal {

// OPENMV-DIFF //
// Agents that read and write through the IDE (instead of the disk) work on what
// the editors show: unsaved edits and untitled scripts included.
static TextEditor::TextDocument *openTextDocument(const FilePath &filePath)
{
    return qobject_cast<TextEditor::TextDocument *>(
        Core::DocumentModel::documentForFilePath(filePath));
}
// OPENMV-DIFF //

AcpFilesystemHandler::AcpFilesystemHandler(AcpClientObject *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    connect(client, &AcpClientObject::readTextFileRequested,
            this, &AcpFilesystemHandler::handleReadTextFile);
    connect(client, &AcpClientObject::writeTextFileRequested,
            this, &AcpFilesystemHandler::handleWriteTextFile);
}

void AcpFilesystemHandler::handleReadTextFile(const QJsonValue &id, const ReadTextFileRequest &request)
{
    const FilePath filePath = FilePath::fromUserInput(request.path());
    qCDebug(logFs) << "Reading file:" << filePath;

    // OPENMV-DIFF //
    // const Result<QByteArray> contents = filePath.fileContents();
    // if (!contents) {
    //     m_client->sendErrorResponse(id, ErrorCode::Resource_not_found,
    //                                 QStringLiteral("Cannot read file: %1").arg(filePath.toUserOutput()));
    //     return;
    // }
    //
    // QString text = QString::fromUtf8(*contents);
    QString text;
    if (TextEditor::TextDocument *document = openTextDocument(filePath)) {
        text = document->plainText();
    } else {
        const Result<QByteArray> contents = filePath.fileContents();
        if (!contents) {
            m_client->sendErrorResponse(id, ErrorCode::Resource_not_found,
                                        QStringLiteral("Cannot read file: %1").arg(filePath.toUserOutput()));
            return;
        }
        text = QString::fromUtf8(*contents);
    }
    // OPENMV-DIFF //

    // Apply line offset and limit if specified
    const auto startLine = request.line();
    const auto lineLimit = request.limit();

    if (startLine || lineLimit) {
        const QStringList lines = text.split('\n');
        const int start = startLine.value_or(0);
        const int count = lineLimit.value_or(lines.size() - start);
        const QStringList subset = lines.mid(start, count);
        text = subset.join('\n');
    }

    ReadTextFileResponse response;
    response.content(text);
    m_client->sendResponse(id, Acp::toJson(response));
}

void AcpFilesystemHandler::handleWriteTextFile(const QJsonValue &id, const WriteTextFileRequest &request)
{
    const FilePath filePath = FilePath::fromUserInput(request.path());
    qCDebug(logFs) << "Writing file:" << filePath;

    // OPENMV-DIFF //
    // A script open in an editor gets the text there, as one change the user can
    // undo. It is saved too when it had no unsaved changes, so the disk matches;
    // otherwise (unsaved edits, an untitled script) only the editor changes, and
    // saving stays the user's call.
    if (TextEditor::TextDocument *document = openTextDocument(filePath)) {
        const bool hadUnsavedChanges = document->isModified() || document->isTemporary();
        QTextCursor cursor(document->document());
        cursor.beginEditBlock();
        cursor.select(QTextCursor::Document);
        cursor.insertText(request.content());
        cursor.endEditBlock();
        if (!hadUnsavedChanges && !Core::DocumentManager::saveDocument(document)) {
            m_client->sendErrorResponse(id, ErrorCode::Internal_error,
                                        QStringLiteral("Cannot write file: %1").arg(filePath.toUserOutput()));
            return;
        }
        emit editorTextWritten(filePath, document->plainText());
        m_client->sendResponse(id, Acp::toJson(WriteTextFileResponse()));
        return;
    }
    // OPENMV-DIFF //

    const Result<qint64> result = filePath.writeFileContents(request.content().toUtf8());
    if (!result) {
        m_client->sendErrorResponse(id, ErrorCode::Internal_error,
                                    QStringLiteral("Cannot write file: %1").arg(filePath.toUserOutput()));
        return;
    }

    WriteTextFileResponse response;
    m_client->sendResponse(id, Acp::toJson(response));
}

} // namespace AcpClient::Internal
