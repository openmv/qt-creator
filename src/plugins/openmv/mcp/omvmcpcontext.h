/* Copyright (C) 2026 OpenMV, LLC.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Any redistribution, use, or modification in source or binary form
 *    is done solely for personal benefit and not for any commercial
 *    purpose or for monetary gain. For commercial licensing options,
 *    please contact openmv@openmv.io
 *
 * THIS SOFTWARE IS PROVIDED BY THE LICENSOR AND COPYRIGHT OWNER "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE LICENSOR OR COPYRIGHT
 * OWNER BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef OMVMCPCONTEXT_H
#define OMVMCPCONTEXT_H

#include <QtCore>

#include <mcp/server/mcpserver.h>

#include <optional>

// The ambient "MCP call" context.
//
// An MCP tool call creates a OMVMCP::Call, enters a OMVMCP::Scope, and then invokes
// the *existing* IDE slot (connectClicked(), bootloaderClicked(), ...). Every
// OMVUI::* dialog primitive consults OMVMCP::currentCall(): when it is null the
// primitive forwards to Qt byte-for-byte (interactive mode); when it is set the
// primitive records what a human would have seen into the call and returns a
// policy-chosen answer instead of blocking on a modal. When the slot returns (or
// workingDone fires) the tool builds its CallToolResult from the call's log and
// errors, so the many `void` flows in the plugin need no signature change.
//
// Everything here is GUI-thread only: the MCP server dispatches tool callbacks on
// the GUI thread, and the OpenMV plugin is not thread-safe.

namespace OpenMV {
namespace Internal {
namespace OMVMCP {

// What a primitive does when the agent supplied no answer for a question.
enum class Policy {
    Elicit,     // ask the agent's client to ask the user (falls back to AutoAnswer until wired)
    AutoAnswer, // pick the safe default (Cancel > No > defaultButton)
    Fail        // record needs_input and pick the safe default
};

struct Error {
    QString code;         // machine code: "critical", "warning", "needs_input", "invalid_path", ...
    QString site;         // OMV_UI_SITE id of the dialog site, or a synthesized one
    QString title;        // the dialog title a human would have seen
    QString text;         // the dialog text
    QString detailedText; // QMessageBox::setDetailedText payload (tool stdout/stderr), if any
    int level = 2;        // 0 information, 1 warning, 2 critical

    QJsonObject toJson() const;
};

struct Call {
    std::optional<Mcp::ToolInterface> tool; // copyable handle; keeps the call open
    Policy policy = Policy::Elicit;
    QJsonObject answers;                    // site id -> pre-supplied answer (button name, item, text, path)
    QStringList log;                        // everything a human would have seen, in order
    QList<Error> errors;                    // structured ledger; the tool's outcome
    QElapsedTimer wall;
    int deadlineMs = 300000;                // wall-clock guard for the whole call
    bool cancelled = false;                 // set by the task-cancel path
    std::function<void(const QString &)> progress; // every log line, for streaming progress to the client

    bool expired() const { return wall.isValid() && (wall.elapsed() > deadlineMs); }
    int remainingMs() const { return wall.isValid() ? qMax(0, deadlineMs - int(wall.elapsed())) : deadlineMs; }
    bool supportsElicitation() const;
    bool hasErrors(int minLevel = 2) const;

    void note(const QString &line);         // append to log
    void fail(const Error &e);              // append to errors (+ log)
    std::optional<QJsonValue> answer(const QString &site) const;

    QJsonObject toJson() const;             // {log:[], errors:[]}
};

// The call in progress on this (GUI) thread, or nullptr in interactive mode.
Call *currentCall();

// RAII: makes `call` the current call for its lifetime. Nesting is allowed (a tool
// that triggers another tool-driven flow); the outer call is restored on exit.
class Scope
{
public:
    explicit Scope(Call &call);
    ~Scope();

private:
    Call *m_previous;
};

// RAII: unless `persist` is set, every OpenMV setting a flow changed while the
// guard lived is rolled back when it dies. Flows remember "last used" paths,
// options and "don't ask again" choices as they go; those are a person's
// preferences, not an agent's, so a tool call leaves them as it found them.
class SettingsGuard
{
public:
    explicit SettingsGuard(bool persist);
    ~SettingsGuard();

private:
    bool m_persist;
    QMap<QString, QVariant> m_before;
};

// A stable id for a dialog site. The site id is the key the agent uses to
// pre-supply an answer, and the key a recorded Error carries. Sites without a
// tag get one synthesized from their title and text.
#define OMV_UI_SITE(id) QStringLiteral(id)

QString synthesizeSite(const QString &title, const QString &text);

} // namespace OMVMCP
} // namespace Internal
} // namespace OpenMV

#endif // OMVMCPCONTEXT_H
