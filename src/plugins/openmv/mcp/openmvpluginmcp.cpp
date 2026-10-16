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

// The OpenMV MCP tools. Each tool runs on the GUI thread inside the server's
// callback, enters a OMVMCP::Scope, invokes the existing plugin slot, and waits
// (nested event loop, wall-clock bounded) for the flow to finish. The result is
// the call's structured error ledger plus whatever the tool reads back.

#include "openmvplugin.h"
#include "openmvpluginio.h"
#include "openmvromfs.h"
#include "views/openmvpluginfb.h"
#include "mcp/omvmcpcontext.h"
#include "mcp/omvui.h"

#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/actionmanager/command.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/messagemanager.h>
#include <coreplugin/outputwindow.h>
#include <mcp/server/toolregistry.h>
#include <utils/qtcassert.h>

#include <QSerialPortInfo>

#include <algorithm>

namespace OpenMV {
namespace Internal {

namespace {

// Block (pumping the event loop) until `signal` fires or `timeoutMs` elapses.
template <typename Sender, typename Signal>
bool awaitSignal(Sender *sender, Signal signal, int timeoutMs)
{
    QEventLoop loop;
    bool fired = false;

    QMetaObject::Connection c = QObject::connect(sender, signal, &loop, [&fired, &loop] {
        fired = true;
        loop.quit();
    });

    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(qMax(1, timeoutMs));

    loop.exec();
    QObject::disconnect(c);
    return fired;
}

// Issue one IO poll and wait for its reply, which `onReply` receives. Replies
// are absent on failure, so the outcome is read from the completion queue:
// queueEmpty with `queued()` false means the camera did not answer.
template <typename Signal, typename Callable>
bool awaitPoll(OpenMVPluginIO *io, Signal signal, Callable onReply, const std::function<bool()> &queued,
               const std::function<void()> &invoke, int timeoutMs)
{
    QEventLoop loop;
    bool got = false;

    QMetaObject::Connection c1 = QObject::connect(io, signal, &loop, onReply);
    QMetaObject::Connection c2 = QObject::connect(io, signal, &loop, [&got, &loop] {
        got = true;
        loop.quit();
    });
    QMetaObject::Connection c3 = QObject::connect(io, &OpenMVPluginIO::queueEmpty, &loop, [&queued, &loop] {
        if(!queued()) loop.quit();
    });

    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    timer.start(qMax(1, timeoutMs));

    invoke();
    loop.exec(); // the V1 fallbacks reply through a 0 ms timer, so never before this
    QObject::disconnect(c1);
    QObject::disconnect(c2);
    QObject::disconnect(c3);
    return got;
}

// Installed by runTool() for the duration of a body so the call it creates
// streams its log to the client (GUI thread only, one tool body at a time).
std::function<void(const QString &)> s_progressHook;

OMVMCP::Call makeCall(const QJsonObject &args)
{
    OMVMCP::Call call;
    call.wall.start();
    call.progress = s_progressHook;

    const QString policy = args.value(QStringLiteral("policy")).toString();
    if(policy == QStringLiteral("auto")) call.policy = OMVMCP::Policy::AutoAnswer;
    else if(policy == QStringLiteral("fail")) call.policy = OMVMCP::Policy::Fail;
    else call.policy = OMVMCP::Policy::Elicit;

    call.answers = args.value(QStringLiteral("answers")).toObject();
    call.deadlineMs = qBound(1000, args.value(QStringLiteral("deadlineMs")).toInt(300000), 3600000);
    return call;
}

Mcp::Schema::CallToolResult okResult(QJsonObject content)
{
    content.insert(QStringLiteral("ok"), true);
    return Mcp::Schema::CallToolResult{}.isError(false).structuredContent(content);
}

Mcp::Schema::CallToolResult errResult(const QString &code, const QString &message, QJsonObject extra = QJsonObject())
{
    extra.insert(QStringLiteral("ok"), false);
    extra.insert(QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), code}, {QStringLiteral("message"), message}});
    return Mcp::Schema::CallToolResult{}.isError(true).structuredContent(extra);
}

// --- rich content ------------------------------------------------------------
// Images go back as MCP image blocks, which the client shows to the model as a
// picture rather than a base64 string; files as embedded resources.

struct ImageArgs {
    bool jpeg = false;
    int quality = 90;
    int maxWidth = 0;
};

ImageArgs imageArgs(const QJsonObject &args)
{
    ImageArgs a;
    a.jpeg = args.value(QStringLiteral("format")).toString() == QStringLiteral("jpeg");
    a.quality = qBound(1, args.value(QStringLiteral("quality")).toInt(90), 100);
    a.maxWidth = qBound(0, args.value(QStringLiteral("maxWidth")).toInt(0), 8192);
    return a;
}

Mcp::Schema::Tool::InputSchema &addImageArgs(Mcp::Schema::Tool::InputSchema &schema)
{
    schema.addProperty(QStringLiteral("format"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("enum"), QJsonArray{QStringLiteral("png"), QStringLiteral("jpeg")}},
        {QStringLiteral("description"), QStringLiteral("Encoding of the returned image (default png; jpeg is much smaller for camera frames).")}});
    schema.addProperty(QStringLiteral("quality"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("integer")},
        {QStringLiteral("description"), QStringLiteral("JPEG quality 1-100 (default 90).")}});
    schema.addProperty(QStringLiteral("maxWidth"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("integer")},
        {QStringLiteral("description"), QStringLiteral("Downscale so the width is at most this many pixels (default 0: native size). A smaller image costs far fewer tokens.")}});
    return schema;
}

// Encodes a pixmap as an image block; `meta` (if given) receives its
// dimensions, mime type and encoded size.
Mcp::Schema::ImageContent imageContent(QPixmap pixmap, const ImageArgs &a, QJsonObject *meta = Q_NULLPTR)
{
    if((a.maxWidth > 0) && (pixmap.width() > a.maxWidth)) pixmap = pixmap.scaledToWidth(a.maxWidth, Qt::SmoothTransformation);

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    pixmap.save(&buffer, a.jpeg ? "JPEG" : "PNG", a.jpeg ? a.quality : -1);

    const QString mime = a.jpeg ? QStringLiteral("image/jpeg") : QStringLiteral("image/png");

    if(meta)
    {
        meta->insert(QStringLiteral("width"), pixmap.width());
        meta->insert(QStringLiteral("height"), pixmap.height());
        meta->insert(QStringLiteral("mimeType"), mime);
        meta->insert(QStringLiteral("bytes"), bytes.size());
    }

    return Mcp::Schema::ImageContent().data(QString::fromLatin1(bytes.toBase64())).mimeType(mime);
}

// A produced file as an embedded resource (base64 blob) when it is small
// enough to be worth inlining; `meta` says whether it was.
const qint64 EMBED_MAX_BYTES = 8 << 20;

std::optional<Mcp::Schema::EmbeddedResource> fileResource(const QString &path, QJsonObject *meta)
{
    QFile file(path);

    if(!file.open(QIODevice::ReadOnly) || (file.size() > EMBED_MAX_BYTES))
    {
        meta->insert(QStringLiteral("embedded"), false);
        return std::nullopt;
    }

    const QByteArray bytes = file.readAll();
    const QString mime = QMimeDatabase().mimeTypeForFileNameAndData(path, bytes).name();
    meta->insert(QStringLiteral("embedded"), true);
    meta->insert(QStringLiteral("bytes"), bytes.size());

    return Mcp::Schema::EmbeddedResource().resource(
        Mcp::Schema::BlobResourceContents().uri(QUrl::fromLocalFile(path).toString()).mimeType(mime).blob(QString::fromLatin1(bytes.toBase64())));
}

// okResult() for a tool that produced a file, embedding it on request.
Mcp::Schema::CallToolResult fileResult(QJsonObject out, const QString &path, bool embed)
{
    std::optional<Mcp::Schema::EmbeddedResource> resource;
    if(embed) resource = fileResource(path, &out);

    Mcp::Schema::CallToolResult result = okResult(out);
    if(resource) result.addContent(*resource);
    return result;
}

Mcp::Schema::Tool::InputSchema &addEmbed(Mcp::Schema::Tool::InputSchema &schema)
{
    schema.addProperty(QStringLiteral("embed"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("boolean")},
        {QStringLiteral("description"), QStringLiteral("Also return the produced file's contents as an embedded resource (files up to 8 MiB). Default false: only the path.")}});
    return schema;
}

// Every tool shares these optional controls.
Mcp::Schema::Tool::InputSchema &addCallControls(Mcp::Schema::Tool::InputSchema &schema)
{
    schema.addProperty(QStringLiteral("policy"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("enum"), QJsonArray{QStringLiteral("elicit"), QStringLiteral("auto"), QStringLiteral("fail")}},
        {QStringLiteral("description"), QStringLiteral("What to do when the IDE would have asked the user a question and no answer was supplied: elicit (ask the user through the client; falls back to auto), auto (safe default: Cancel/No), fail (safe default and report needs_input). Default elicit.")}});
    schema.addProperty(QStringLiteral("answers"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("description"), QStringLiteral("Pre-supplied answers keyed by dialog site id (reported in errors as `site`): a button name such as Yes/No/Ok/Cancel, an item text or index, a text, an integer, or a file path.")}});
    schema.addProperty(QStringLiteral("deadlineMs"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("integer")},
        {QStringLiteral("description"), QStringLiteral("Wall-clock limit for the whole operation in milliseconds (default 300000).")}});
    schema.addProperty(QStringLiteral("persist"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("boolean")},
        {QStringLiteral("description"), QStringLiteral("Keep the IDE settings this operation changes (last used paths/options, \"don't ask again\" choices). Default false: they are rolled back when the call ends, so the user's preferences are untouched.")}});
    return schema;
}

// Runs a tool body. When the client supports MCP tasks the call is upgraded to
// one first, so the client sees the call's log lines as progress instead of
// waiting blind; otherwise the body simply runs and the result is sent. The
// body runs synchronously either way (nested event loops keep the server
// answering tasks/get meanwhile).
struct TaskState {
    bool finished = false;
    bool failed = false;
    QString message;
    std::optional<Mcp::Schema::CallToolResult> result;
    QElapsedTimer timer;
    qint64 lastNotifyMs = -1000;
};

Utils::Result<> runTool(const Mcp::ToolInterface &toolInterface, const Mcp::Schema::CallToolRequestParams &params,
                        const std::function<Mcp::Schema::CallToolResult()> &body)
{
    using namespace std::chrono_literals;
    using Mcp::Schema::TaskStatus;

    const OMVMCP::SettingsGuard guard(params.argumentsAsObject().value(QStringLiteral("persist")).toBool());

    auto state = std::make_shared<TaskState>();
    state->timer.start();

    std::optional<Mcp::Schema::ProgressToken> token;
    if(params._meta()) token = params._meta()->progressToken();

    Utils::Result<Mcp::ToolInterface::TaskProgressNotify> notify = toolInterface.startTask(
        1s,
        [state](Mcp::Schema::Task task) -> Mcp::Schema::Task {
            if(state->finished)
            {
                task.status(state->failed ? TaskStatus::failed : TaskStatus::completed);
                task.statusMessage(state->message);
                Mcp::letTaskDieIn(task, 1min);
                return task;
            }

            task.status(TaskStatus::working);
            if(!state->message.isEmpty()) task.statusMessage(state->message);
            return task;
        },
        [state]() -> Utils::Result<Mcp::Schema::CallToolResult> {
            if(state->result) return *state->result;
            return Utils::ResultError(QStringLiteral("The operation has not finished."));
        },
        std::nullopt, // cancelling a flash half-way can brick a camera: not offered
        token);

    if(!notify)
    {
        // Could not become a task at all: plain synchronous call.
        s_progressHook = {};
        toolInterface.finish(body());
        return Utils::ResultOk;
    }

    // For a client that did not ask for a task the server keeps the request
    // open and polls onUpdateTask itself (sending notifications/progress and,
    // once finished, the result); the notify functor is then empty.
    const Mcp::ToolInterface::TaskProgressNotify push = *notify;

    s_progressHook = [state, push](const QString &line) {
        state->message = line;
        if(!push) return;
        if((state->timer.elapsed() - state->lastNotifyMs) < 250) return; // a flash log is chatty
        state->lastNotifyMs = state->timer.elapsed();
        push(TaskStatus::working, line, std::nullopt);
    };

    Mcp::Schema::CallToolResult result = body();
    s_progressHook = {};

    state->result = result;
    state->failed = result.isError().value_or(false);
    state->finished = true;
    if(push) push(state->failed ? TaskStatus::failed : TaskStatus::completed, state->message, std::nullopt);
    return Utils::ResultOk;
}

// Destructive (G2) tools must be told `confirm:true` explicitly.
Mcp::Schema::Tool::InputSchema &addConfirm(Mcp::Schema::Tool::InputSchema &schema)
{
    schema.addProperty(QStringLiteral("confirm"), QJsonObject{
        {QStringLiteral("type"), QStringLiteral("boolean")},
        {QStringLiteral("description"), QStringLiteral("Must be true: this operation is destructive. When the client supports elicitation the user is asked to confirm as well.")}});
    schema.addRequired(QStringLiteral("confirm"));
    return schema;
}

QJsonObject outcome(const OMVMCP::Call &call)
{
    return call.toJson();
}

// The terminal ring: enough for a chatty script's recent history without
// letting a runaway print loop grow the process.
const int TERMINAL_RING_BYTES = 1 << 20;
const int TERMINAL_READ_DEFAULT = 64 << 10;

} // namespace

void OpenMVPlugin::mcpTerminalData(const QByteArray &data)
{
    m_mcpTerminalLog.append(data);

    if(m_mcpTerminalLog.size() > TERMINAL_RING_BYTES)
    {
        const int drop = m_mcpTerminalLog.size() - TERMINAL_RING_BYTES;
        m_mcpTerminalLog.remove(0, drop);
        m_mcpTerminalDropped += drop;
    }
}

void OpenMVPlugin::registerMcpTools()
{
    using namespace Mcp::Schema;
    using Mcp::ToolRegistry;

    // ---- openmv_guide -------------------------------------------------------
    // The conventions an agent needs and nothing else; read once per session.

    ToolRegistry::registerTool(
        Tool{}
            .name(QStringLiteral("openmv_guide"))
            .title(QStringLiteral("How to drive OpenMV IDE through these tools"))
            .description(QStringLiteral("Read this first: how the openmv_* tools report dialogs, how to answer them, how destructive operations are gated, and the usual workflow for working with a camera."))
            .annotations(ToolAnnotations{}.readOnlyHint(true)),
        [](const CallToolRequestParams &) -> Utils::Result<CallToolResult> {
            const QString guide = QStringLiteral(
                "OpenMV IDE MCP tools\n"
                "\n"
                "Workflow: openmv_list_ports -> openmv_camera_connect {port?} -> openmv_script_run {text, waitMs} -> "
                "openmv_terminal_read {cursor} / openmv_framebuffer_get {maxWidth, format:jpeg} / openmv_histogram_get / openmv_channels_get -> "
                "openmv_script_stop. openmv_camera_status tells you what is connected, whether a script is running and the last traceback. "
                "openmv_script_save_to_camera writes the editor's script to main.py so it runs at boot.\n"
                "\n"
                "Dialogs: the tools run the IDE's real flows. Anything the IDE would have shown a person comes back in `errors` "
                "(code, level 0-2, title, text, detailedText = tool output) and `log`; nothing blocks. A question or form that needed a person "
                "is reported as code needs_input with a `site` id and the choices/fields; call again with `answers: {\"<site>\": value}` "
                "(a button name such as Yes/No/Ok/Cancel, an item text or index, a text, an integer, a file path, or for forms true/false/{field: value}). "
                "Stable sites: connect.port, connect.board_type, connect.dfu_device, connect.bricked, connect.retry, connect.upgrade_firmware, "
                "connect.install_dev_release, script.save_strip, romfs.target, mpy.options, vela.options, stedgeai.options, fb.save_image_path. "
                "`policy`: elicit (default: ask the user through your client when it supports elicitation, else the safe default), auto (safe default, "
                "Cancel/No), fail (safe default and report). `deadlineMs` bounds the whole call; firmware work can take minutes.\n"
                "\n"
                "Destructive tools (openmv_firmware_install, openmv_camera_erase_filesystem) need confirm:true and, when your client supports "
                "elicitation, the user's confirmation. openmv_camera_reset / enter_bootloader / script_save_to_camera are marked destructive but need no confirm.\n"
                "\n"
                "Settings: a call leaves the user's IDE settings as it found them unless persist:true.\n"
                "\n"
                "Rich data: frames and view captures come back as image content blocks (use maxWidth/jpeg to keep them small); compile tools can embed "
                "their output file (embed:true). Long operations stream progress as task status messages when your client supports MCP tasks.\n"
                "\n"
                "Terminal: openmv_terminal_read uses absolute byte cursors; pass back the `cursor` you got to read only what is new. "
                "openmv_terminal_clear forgets the history without touching the IDE's terminal.\n"
                "\n"
                "The server listens on localhost only; there is no authentication beyond that.");

            return CallToolResult{}.isError(false).addContent(TextContent().text(guide));
        });

    // ---- openmv_camera_status ---------------------------------------------

    ToolRegistry::registerTool(
        Tool{}
            .name(QStringLiteral("openmv_camera_status"))
            .title(QStringLiteral("OpenMV camera status"))
            .description(QStringLiteral("Whether an OpenMV camera is connected and on which port, what board/firmware/sensor it reports, whether a script is running, whether the IDE is busy with a camera operation, and the last traceback the camera printed."))
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(Tool::OutputSchema{}
                .addProperty(QStringLiteral("connected"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}})
                .addProperty(QStringLiteral("working"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}})
                .addProperty(QStringLiteral("running"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}})
                .addProperty(QStringLiteral("port"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})
                .addProperty(QStringLiteral("drivePath"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})
                .addProperty(QStringLiteral("board"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})
                .addProperty(QStringLiteral("boardId"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})
                .addProperty(QStringLiteral("sensor"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})
                .addProperty(QStringLiteral("firmware"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})
                .addProperty(QStringLiteral("protocolV2"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}})
                .addProperty(QStringLiteral("lastError"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})
                .addProperty(QStringLiteral("terminalCursor"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}})
                .addRequired(QStringLiteral("connected")).addRequired(QStringLiteral("working"))),
        [this](const CallToolRequestParams &) -> Utils::Result<CallToolResult> {
            QJsonObject out{
                {QStringLiteral("connected"), m_connected},
                {QStringLiteral("working"), m_working},
                {QStringLiteral("running"), m_connected && m_running},
                {QStringLiteral("port"), m_portName},
                {QStringLiteral("protocolV2"), m_iodevice ? m_iodevice->v2ProtocolEnabled() : false},
                {QStringLiteral("lastError"), m_lastScriptError},
                {QStringLiteral("terminalCursor"), m_mcpTerminalDropped + m_mcpTerminalLog.size()}};

            if(m_connected)
            {
                out.insert(QStringLiteral("drivePath"), m_portPath);
                out.insert(QStringLiteral("board"), m_boardType);
                out.insert(QStringLiteral("boardId"), m_boardId);
                out.insert(QStringLiteral("sensor"), m_sensorType);
                out.insert(QStringLiteral("firmware"), QStringLiteral("%1.%2.%3").arg(m_major).arg(m_minor).arg(m_patch));
            }

            return okResult(out);
        });

    // ---- openmv_terminal_read -----------------------------------------------
    // Absolute byte cursors: a reader passes back the `cursor` it last got and
    // receives only what the camera printed since. The ring is bounded, so a
    // stale cursor is clamped and `truncated` is set.

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("cursor"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), QStringLiteral("Byte cursor from a previous read (or `terminalCursor` from openmv_camera_status). Omitted: the most recent `maxBytes`.")}});
        in.addProperty(QStringLiteral("maxBytes"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), QStringLiteral("At most this many bytes, from the cursor forward (default 65536).")}});

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_terminal_read"))
                .title(QStringLiteral("Read the camera's serial terminal"))
                .description(QStringLiteral("Returns text the camera printed (print() output, tracebacks, REPL) since `cursor`, plus the cursor to pass next time. Nothing is consumed: the IDE's own terminal still shows everything."))
                .annotations(ToolAnnotations{}.readOnlyHint(true))
                .inputSchema(in)
                .outputSchema(Tool::OutputSchema{}
                    .addProperty(QStringLiteral("text"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})
                    .addProperty(QStringLiteral("start"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}})
                    .addProperty(QStringLiteral("cursor"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}})
                    .addProperty(QStringLiteral("truncated"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}})
                    .addProperty(QStringLiteral("more"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}})
                    .addRequired(QStringLiteral("text")).addRequired(QStringLiteral("cursor"))),
            [this](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                const QJsonObject args = params.argumentsAsObject();
                const qint64 end = m_mcpTerminalDropped + m_mcpTerminalLog.size();
                const int maxBytes = qBound(1, args.value(QStringLiteral("maxBytes")).toInt(TERMINAL_READ_DEFAULT), TERMINAL_RING_BYTES);

                qint64 start = args.contains(QStringLiteral("cursor"))
                             ? qint64(args.value(QStringLiteral("cursor")).toDouble())
                             : qMax(m_mcpTerminalDropped, end - maxBytes);

                const bool truncated = start < m_mcpTerminalDropped;
                start = qBound(m_mcpTerminalDropped, start, end);

                const qint64 stop = qMin(end, start + maxBytes);
                const QByteArray slice = m_mcpTerminalLog.mid(int(start - m_mcpTerminalDropped), int(stop - start));

                return okResult(QJsonObject{
                    {QStringLiteral("text"), QString::fromUtf8(slice)},
                    {QStringLiteral("start"), start},
                    {QStringLiteral("cursor"), stop},
                    {QStringLiteral("truncated"), truncated},
                    {QStringLiteral("more"), stop < end}});
            });
    }

    // ---- openmv_list_ports --------------------------------------------------
    // Enumerates without probing: querySerialPorts() transiently opens every
    // candidate port as a side effect, which an agent polling status must not do.

    ToolRegistry::registerTool(
        Tool{}
            .name(QStringLiteral("openmv_list_ports"))
            .title(QStringLiteral("List serial ports"))
            .description(QStringLiteral("Lists the serial ports on this machine with their USB vendor/product ids, descriptions and serial numbers. OpenMV cameras enumerate with vendor id 0x1209."))
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(Tool::OutputSchema{}
                .addProperty(QStringLiteral("ports"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}})
                .addRequired(QStringLiteral("ports"))),
        [](const CallToolRequestParams &) -> Utils::Result<CallToolResult> {
            QJsonArray ports;

            for(const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
            {
                ports.append(QJsonObject{
                    {QStringLiteral("port"), info.portName()},
                    {QStringLiteral("systemLocation"), info.systemLocation()},
                    {QStringLiteral("description"), info.description()},
                    {QStringLiteral("manufacturer"), info.manufacturer()},
                    {QStringLiteral("serialNumber"), info.serialNumber()},
                    {QStringLiteral("vid"), info.hasVendorIdentifier() ? int(info.vendorIdentifier()) : -1},
                    {QStringLiteral("pid"), info.hasProductIdentifier() ? int(info.productIdentifier()) : -1}});
            }

            return okResult(QJsonObject{{QStringLiteral("ports"), ports}});
        });

    // ---- openmv_camera_connect ----------------------------------------------

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("port"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Serial port to use when more than one camera is attached (a `port` from openmv_list_ports). Omitted: the IDE's own choice, or a needs_input error at site connect.port.")}});
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_camera_connect"))
                .title(QStringLiteral("Connect to the OpenMV camera"))
                .description(QStringLiteral("Runs the IDE's Connect action and waits for it to finish. Any dialog the IDE would have shown (port picker, firmware prompts, errors) is returned in `errors` with its site id instead of blocking; supply `answers` keyed by site id to pre-answer them. Known sites: connect.port, connect.board_type, connect.dfu_device, connect.bricked, connect.retry, connect.upgrade_firmware, connect.install_dev_release."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false).idempotentHint(true))
                .inputSchema(in),
            // ToolInterfaceCallback: the call holds the ToolInterface so dialogs
            // can elicit answers from the user through the agent's client. The
            // flow is still synchronous (nested event loops), so finish() is
            // called before returning.
            [this](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                if(m_working) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));
                if(m_connected) return okResult(QJsonObject{{QStringLiteral("connected"), true}, {QStringLiteral("port"), m_portName}, {QStringLiteral("alreadyConnected"), true}});

                const QJsonObject args = params.argumentsAsObject();
                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;

                if(args.contains(QStringLiteral("port")))
                {
                    const QString port = args.value(QStringLiteral("port")).toString();
                    call.answers.insert(OMV_UI_SITE("connect.port"), port);

                    // A camera that was just reset (or plugged in) takes a moment
                    // to enumerate again; wait for the named port before scanning,
                    // or the scan finds a bootloader device / nothing instead.
                    auto present = [&port] {
                        for(const QSerialPortInfo &info : QSerialPortInfo::availablePorts())
                        {
                            if(info.portName() == port) return true;
                        }

                        return false;
                    };

                    const int waitMs = qMin(call.remainingMs(), 20000);

                    while(!present() && (call.wall.elapsed() < waitMs))
                    {
                        QEventLoop loop;
                        QTimer::singleShot(250, &loop, &QEventLoop::quit);
                        loop.exec();
                    }

                    if(!present()) return errResult(QStringLiteral("no_such_port"), QStringLiteral("Serial port \"%1\" did not appear within %2 ms; see openmv_list_ports.").arg(port).arg(waitMs));
                    if(call.wall.elapsed() > 250) call.note(QStringLiteral("[connect.port] waited %1 ms for %2 to enumerate").arg(call.wall.elapsed()).arg(port));
                }

                {
                    OMVMCP::Scope scope(call);
                    connectClicked();

                    // Every exit path schedules workingDone via a 0 ms timer, and the
                    // RECONNECT_* paths re-enter connectClicked() the same way, so wait
                    // until workingDone arrives with the flow genuinely idle.
                    while(m_working && !call.expired())
                    {
                        awaitSignal(this, &OpenMVPlugin::workingDone, call.remainingMs());
                    }
                }

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("connected"), m_connected);
                out.insert(QStringLiteral("port"), m_portName);

                if(call.expired() && m_working) return errResult(QStringLiteral("timeout"), QStringLiteral("The connect flow did not finish within the deadline."), out);
                if(!m_connected) return errResult(QStringLiteral("not_connected"), QStringLiteral("The IDE did not connect; see errors."), out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- openmv_camera_disconnect -------------------------------------------

    {
        Tool::InputSchema in;
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_camera_disconnect"))
                .title(QStringLiteral("Disconnect from the OpenMV camera"))
                .description(QStringLiteral("Runs the IDE's Disconnect action and waits for it to finish."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false).idempotentHint(true))
                .inputSchema(in),
            // ToolInterfaceCallback: the call holds the ToolInterface so dialogs
            // can elicit answers from the user through the agent's client. The
            // flow is still synchronous (nested event loops), so finish() is
            // called before returning.
            [this](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                if(m_working) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));
                if(!m_connected) return okResult(QJsonObject{{QStringLiteral("connected"), false}, {QStringLiteral("alreadyDisconnected"), true}});

                OMVMCP::Call call = makeCall(params.argumentsAsObject());
                call.tool = toolInterface;

                {
                    OMVMCP::Scope scope(call);
                    disconnectClicked();

                    while((m_working || m_connected) && !call.expired())
                    {
                        if(!awaitSignal(this, &OpenMVPlugin::workingDone, qMin(call.remainingMs(), 250))) continue;
                    }
                }

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("connected"), m_connected);

                if(m_connected) return errResult(call.expired() ? QStringLiteral("timeout") : QStringLiteral("still_connected"), QStringLiteral("The IDE did not disconnect; see errors."), out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- openmv_script_run --------------------------------------------------

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("text"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("MicroPython source to run on the camera. Omitted: runs the script in the current editor, exactly like the Start button.")}});
        in.addProperty(QStringLiteral("waitMs"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), QStringLiteral("After the camera accepts the script, keep collecting its output for this long and return it (`output`, `lastError`, `running`). Default 0: return at once and use openmv_terminal_read.")}});
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_script_run"))
                .title(QStringLiteral("Run a script on the OpenMV camera"))
                .description(QStringLiteral("Runs `text` on the connected camera, or the current editor's script when `text` is omitted. Replaces whatever script is running. Returns once the camera has accepted the script; use openmv_script_stop to halt it."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false))
                .inputSchema(in),
            // ToolInterfaceCallback: the call holds the ToolInterface so dialogs
            // can elicit answers from the user through the agent's client. The
            // flow is still synchronous (nested event loops), so finish() is
            // called before returning.
            [this](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                if(!m_connected || !m_iodevice) return errResult(QStringLiteral("not_connected"), QStringLiteral("No camera is connected."));
                if(m_working) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;
                m_lastScriptError = QString();

                // With `waitMs`, give the script that long to print, then
                // return everything it wrote since the run started.
                const qint64 cursor0 = m_mcpTerminalDropped + m_mcpTerminalLog.size();
                const int waitMs = qBound(0, args.value(QStringLiteral("waitMs")).toInt(0), 600000);

                auto runOutcome = [this, cursor0, waitMs](QJsonObject out) -> QJsonObject {
                    if(waitMs > 0)
                    {
                        QEventLoop loop;
                        QTimer::singleShot(waitMs, &loop, &QEventLoop::quit);
                        loop.exec();

                        const qint64 start = qMax(cursor0, m_mcpTerminalDropped);
                        out.insert(QStringLiteral("output"), QString::fromUtf8(m_mcpTerminalLog.mid(int(start - m_mcpTerminalDropped))));
                        out.insert(QStringLiteral("outputTruncated"), start != cursor0);
                        out.insert(QStringLiteral("lastError"), m_lastScriptError);
                        out.insert(QStringLiteral("running"), m_connected && m_running);
                    }

                    out.insert(QStringLiteral("terminalCursor"), m_mcpTerminalDropped + m_mcpTerminalLog.size());
                    return out;
                };

                if(args.contains(QStringLiteral("text")))
                {
                    OMVMCP::Scope scope(call);
                    m_iodevice->scriptExec(args.value(QStringLiteral("text")).toString().toUtf8());
                    bool done = awaitSignal(m_iodevice, &OpenMVPluginIO::scriptExecDone, call.remainingMs());
                    bool timedOut = m_iodevice->getTimeout();

                    QJsonObject out = outcome(call);
                    if(!done) return errResult(QStringLiteral("timeout"), QStringLiteral("The camera did not acknowledge the script."), out);
                    if(timedOut) return errResult(QStringLiteral("camera_timeout"), QStringLiteral("The camera timed out while receiving the script."), out);
                    return okResult(runOutcome(out));
                }

                {
                    OMVMCP::Scope scope(call);
                    startClicked();

                    while(m_working && !call.expired())
                    {
                        awaitSignal(this, &OpenMVPlugin::workingDone, call.remainingMs());
                    }
                }

                QJsonObject out = outcome(call);
                if(call.expired() && m_working) return errResult(QStringLiteral("timeout"), QStringLiteral("Start did not finish within the deadline."), out);
                if(call.hasErrors()) return errResult(QStringLiteral("failed"), QStringLiteral("Start reported an error; see errors."), out);
                return okResult(runOutcome(out));
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- openmv_script_stop -------------------------------------------------

    {
        Tool::InputSchema in;
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_script_stop"))
                .title(QStringLiteral("Stop the script on the OpenMV camera"))
                .description(QStringLiteral("Halts the running script, exactly like the Stop button."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false).idempotentHint(true))
                .inputSchema(in),
            // ToolInterfaceCallback: the call holds the ToolInterface so dialogs
            // can elicit answers from the user through the agent's client. The
            // flow is still synchronous (nested event loops), so finish() is
            // called before returning.
            [this](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                if(!m_connected || !m_iodevice) return errResult(QStringLiteral("not_connected"), QStringLiteral("No camera is connected."));
                if(m_working) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));

                OMVMCP::Call call = makeCall(params.argumentsAsObject());
                call.tool = toolInterface;

                {
                    OMVMCP::Scope scope(call);
                    stopClicked();

                    while(m_working && !call.expired())
                    {
                        awaitSignal(this, &OpenMVPlugin::workingDone, call.remainingMs());
                    }
                }

                QJsonObject out = outcome(call);
                if(call.expired() && m_working) return errResult(QStringLiteral("timeout"), QStringLiteral("Stop did not finish within the deadline."), out);
                if(call.hasErrors()) return errResult(QStringLiteral("failed"), QStringLiteral("Stop reported an error; see errors."), out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- openmv_framebuffer_get ---------------------------------------------

    {
        Tool::InputSchema in;
        addImageArgs(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_framebuffer_get"))
                .title(QStringLiteral("Get the camera frame buffer"))
                .description(QStringLiteral("Returns the most recent frame the IDE received from the camera as an image, with its dimensions. Fails if no frame has been received."))
                .annotations(ToolAnnotations{}.readOnlyHint(true))
                .inputSchema(in)
                .outputSchema(Tool::OutputSchema{}
                    .addProperty(QStringLiteral("width"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}})
                    .addProperty(QStringLiteral("height"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}})
                    .addProperty(QStringLiteral("mimeType"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})),
            [this](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                if(!m_frameBuffer || !m_frameBuffer->pixmapValid()) return errResult(QStringLiteral("not_available"), QStringLiteral("No frame has been received from the camera."));

                QJsonObject out;
                const ImageContent image = imageContent(m_frameBuffer->pixmap(), imageArgs(params.argumentsAsObject()), &out);

                // What the Frame Buffer title shows: the camera's resolution,
                // the user's selection rectangle, the focus metric and frame rates.
                out.insert(QStringLiteral("resolution"), QJsonObject{{QStringLiteral("width"), m_mcpFbResolution.width()}, {QStringLiteral("height"), m_mcpFbResolution.height()}});
                if(m_mcpFbRoi.isValid()) out.insert(QStringLiteral("roi"), QJsonObject{{QStringLiteral("x"), m_mcpFbRoi.x()}, {QStringLiteral("y"), m_mcpFbRoi.y()}, {QStringLiteral("width"), m_mcpFbRoi.width()}, {QStringLiteral("height"), m_mcpFbRoi.height()}});
                out.insert(QStringLiteral("focus"), m_mcpFbFocus);
                if(!m_frameFormatName.isEmpty()) out.insert(QStringLiteral("format"), m_frameFormatName);
                out.insert(QStringLiteral("fpsIde"), m_fpsIde);
                if(m_fpsCameraValid) out.insert(QStringLiteral("fpsCamera"), m_fpsCamera);

                return okResult(out).addContent(image);
            });
    }

    // ---- openmv_histogram_get -----------------------------------------------
    // The numbers behind the Histogram pane, computed on demand from the
    // current frame (the pane itself only computes while visible).

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("colorSpace"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"), QJsonArray{QStringLiteral("rgb"), QStringLiteral("grayscale"), QStringLiteral("lab"), QStringLiteral("yuv")}},
            {QStringLiteral("description"), QStringLiteral("Colour space to analyse (default: the one the Histogram pane is set to).")}});
        in.addProperty(QStringLiteral("bins"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), QStringLiteral("Also return each channel's bins as [value, count] pairs (default false: statistics only).")}});

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_histogram_get"))
                .title(QStringLiteral("Get the frame buffer histogram"))
                .description(QStringLiteral("Per-channel statistics (mean, median, mode, standard deviation, min, max, quartiles) of the current frame in the chosen colour space, as the Histogram pane computes them, with the bins on request. Useful for picking colour-tracking thresholds."))
                .annotations(ToolAnnotations{}.readOnlyHint(true))
                .inputSchema(in),
            [this](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                if(!m_frameBuffer || !m_frameBuffer->pixmapValid()) return errResult(QStringLiteral("not_available"), QStringLiteral("No frame has been received from the camera."));

                const QJsonObject args = params.argumentsAsObject();
                const QStringList spaces{QStringLiteral("rgb"), QStringLiteral("grayscale"), QStringLiteral("lab"), QStringLiteral("yuv")};
                int colorSpace = m_histogram ? m_histogram->colorSpace() : 0;
                if(args.contains(QStringLiteral("colorSpace"))) colorSpace = qMax(0, spaces.indexOf(args.value(QStringLiteral("colorSpace")).toString()));
                const bool bins = args.value(QStringLiteral("bins")).toBool();

                const QImage image = m_frameBuffer->pixmap().scaledToWidth(160, Qt::SmoothTransformation).toImage();
                QJsonArray channels;

                for(int channel : OpenMVPluginHistogram::channels(colorSpace))
                {
                    OpenMVPluginHistogram::Stats s;
                    const QVector<long long> counts = OpenMVPluginHistogram::histogram(image, channel, &s);

                    QJsonObject o{
                        {QStringLiteral("name"), OpenMVPluginHistogram::channelName(channel)},
                        {QStringLiteral("mean"), s.mean}, {QStringLiteral("median"), s.median}, {QStringLiteral("mode"), s.mode},
                        {QStringLiteral("standardDeviation"), s.standardDeviation}, {QStringLiteral("min"), s.min}, {QStringLiteral("max"), s.max},
                        {QStringLiteral("lowerQuartile"), s.lowerQuartile}, {QStringLiteral("upperQuartile"), s.upperQuartile}};

                    if(bins)
                    {
                        QJsonArray pairs;
                        for(int i = 0; i < counts.size(); i++) pairs.append(QJsonArray{OpenMVPluginHistogram::binValue(i, channel), double(counts.at(i))});
                        o.insert(QStringLiteral("bins"), pairs);
                    }

                    channels.append(o);
                }

                return okResult(QJsonObject{
                    {QStringLiteral("colorSpace"), spaces.at(colorSpace)},
                    {QStringLiteral("sampledWidth"), image.width()},
                    {QStringLiteral("sampledHeight"), image.height()},
                    {QStringLiteral("channels"), channels}});
            });
    }

    // ---- openmv_channels_get / openmv_channel_write -------------------------
    // The script's published channels (the Channels pane): values, controls,
    // waveform chunks and depth maps, and writing a control back.

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("includeData"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), QStringLiteral("Include waveform samples and depth-map values (default false: metadata, values and control states only).")}});
        in.addProperty(QStringLiteral("maxSamples"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), QStringLiteral("At most this many newest samples per waveform series (default 256; 0 = the whole newest chunk).")}});

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_channels_get"))
                .title(QStringLiteral("Read the script's channels"))
                .description(QStringLiteral("Everything the Channels pane shows for the running script: each published record's channel, name, unit, current value, widget type and control state (sliders, toggles, selects...), plus waveform series and depth maps with their data on request."))
                .annotations(ToolAnnotations{}.readOnlyHint(true))
                .inputSchema(in),
            [this](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                if(!m_channelsView) return errResult(QStringLiteral("not_available"), QStringLiteral("The Channels pane does not exist yet."));

                const QJsonObject args = params.argumentsAsObject();
                const QJsonArray records = m_channelsView->snapshot(args.value(QStringLiteral("includeData")).toBool(),
                                                                    qBound(0, args.value(QStringLiteral("maxSamples")).toInt(256), 1 << 20));

                return okResult(QJsonObject{{QStringLiteral("connected"), m_connected}, {QStringLiteral("running"), m_connected && m_running}, {QStringLiteral("records"), records}});
            });

        Tool::InputSchema wr;
        wr.addProperty(QStringLiteral("channel"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("The channel name (as in openmv_channels_get).")}});
        wr.addProperty(QStringLiteral("record"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}, {QStringLiteral("description"), QStringLiteral("The record (control) name within the channel.")}});
        wr.addProperty(QStringLiteral("value"), QJsonObject{{QStringLiteral("description"), QStringLiteral("The new value: boolean for toggles, number for sliders/spin boxes, string for selects/radios/line edits, true for push buttons.")}});
        wr.addRequired(QStringLiteral("channel")).addRequired(QStringLiteral("record")).addRequired(QStringLiteral("value"));

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_channel_write"))
                .title(QStringLiteral("Write a script channel control"))
                .description(QStringLiteral("Changes a control the running script published (exactly like moving the slider or toggling the switch in the Channels pane); the script sees the new value on its next read."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false).idempotentHint(true))
                .inputSchema(wr),
            [this](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                if(!m_connected || !m_channelsView) return errResult(QStringLiteral("not_connected"), QStringLiteral("No camera is connected."));

                const QJsonObject args = params.argumentsAsObject();
                const QString channel = args.value(QStringLiteral("channel")).toString();
                const QString record = args.value(QStringLiteral("record")).toString();

                if(!m_channelsView->write(channel, record, args.value(QStringLiteral("value"))))
                {
                    return errResult(QStringLiteral("not_available"), QStringLiteral("No writable control \"%1/%2\" is published by a running script.").arg(channel, record));
                }

                return okResult(QJsonObject{{QStringLiteral("channel"), channel}, {QStringLiteral("record"), record}, {QStringLiteral("value"), args.value(QStringLiteral("value"))}});
            });
    }

    // ---- openmv_framebuffer_capture -----------------------------------------
    // Several successive frames so the model can see motion / a change over time.

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("frames"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), QStringLiteral("How many new frames to collect, 1-16 (default 4).")}});
        in.addProperty(QStringLiteral("intervalMs"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), QStringLiteral("Minimum time between captured frames in milliseconds (default 0: consecutive frames).")}});
        addImageArgs(in);
        in.addProperty(QStringLiteral("deadlineMs"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), QStringLiteral("Give up after this long if frames stop arriving (default 10000).")}});

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_framebuffer_capture"))
                .title(QStringLiteral("Capture a sequence of camera frames"))
                .description(QStringLiteral("Waits for new frames from the running script and returns them as a sequence of images with their capture times. Needs a connected camera streaming its frame buffer. Use maxWidth/jpeg to keep the sequence small."))
                .annotations(ToolAnnotations{}.readOnlyHint(true))
                .inputSchema(in)
                .outputSchema(Tool::OutputSchema{}
                    .addProperty(QStringLiteral("frames"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}})
                    .addRequired(QStringLiteral("frames"))),
            [this](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                if(!m_connected || !m_frameBuffer) return errResult(QStringLiteral("not_connected"), QStringLiteral("No camera is connected."));

                const QJsonObject args = params.argumentsAsObject();
                const int count = qBound(1, args.value(QStringLiteral("frames")).toInt(4), 16);
                const int intervalMs = qBound(0, args.value(QStringLiteral("intervalMs")).toInt(0), 60000);
                const int deadlineMs = qBound(1000, args.value(QStringLiteral("deadlineMs")).toInt(10000), 600000);
                const ImageArgs a = imageArgs(args);

                QElapsedTimer wall;
                wall.start();

                QList<ImageContent> images;
                QJsonArray frames;
                qint64 last = -1;

                while((images.size() < count) && (wall.elapsed() < deadlineMs))
                {
                    if((last >= 0) && (intervalMs > 0))
                    {
                        // Space the captures out; frames arriving meanwhile are skipped.
                        QEventLoop loop;
                        QTimer::singleShot(qMax<qint64>(0, last + intervalMs - wall.elapsed()), &loop, &QEventLoop::quit);
                        loop.exec();
                    }

                    if(!awaitSignal(m_frameBuffer, &OpenMVPluginFB::pixmapUpdate, int(deadlineMs - wall.elapsed()))) break;

                    QJsonObject meta;
                    images.append(imageContent(m_frameBuffer->pixmap(), a, &meta));
                    last = wall.elapsed();
                    meta.insert(QStringLiteral("index"), images.size() - 1);
                    meta.insert(QStringLiteral("timeMs"), last);
                    frames.append(meta);
                }

                if(images.isEmpty()) return errResult(QStringLiteral("timeout"), QStringLiteral("No frame arrived from the camera; is a script streaming the frame buffer?"));

                CallToolResult result = okResult(QJsonObject{{QStringLiteral("frames"), frames}, {QStringLiteral("complete"), images.size() == count}});
                for(const ImageContent &image : std::as_const(images)) result.addContent(image);
                return result;
            });
    }

    // ---- openmv_view_capture ------------------------------------------------
    // What the user sees: any pane, the terminal or the whole window as an image.

    {
        const QStringList views{QStringLiteral("frame_buffer"), QStringLiteral("histogram"), QStringLiteral("board_info"), QStringLiteral("memory"),
                                QStringLiteral("channels"), QStringLiteral("statistics"), QStringLiteral("terminal"), QStringLiteral("main_window"),
                                QStringLiteral("profiler")};

        Tool::InputSchema in;
        in.addProperty(QStringLiteral("view"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"), QJsonArray::fromStringList(views)},
            {QStringLiteral("description"), QStringLiteral("Which part of the IDE to capture: a pane as it is currently drawn (hidden panes show their last state), the serial terminal, or the whole main window.")}});
        in.addRequired(QStringLiteral("view"));
        addImageArgs(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_view_capture"))
                .title(QStringLiteral("Capture an IDE view as an image"))
                .description(QStringLiteral("Renders a pane (frame buffer, histogram, board info, memory, channels, statistics), the serial terminal or the whole main window to an image, exactly as the user sees it."))
                .annotations(ToolAnnotations{}.readOnlyHint(true))
                .inputSchema(in),
            [this, views](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                const QJsonObject args = params.argumentsAsObject();
                const QString view = args.value(QStringLiteral("view")).toString();

                QWidget *widget = Q_NULLPTR;
                switch(views.indexOf(view))
                {
                    case 0: widget = m_frameBuffer; break;
                    case 1: widget = m_histogram; break;
                    case 2: widget = m_boardInfoView; break;
                    case 3: widget = m_memoryView; break;
                    case 4: widget = m_channelsView; break;
                    case 5: widget = m_statisticsView; break;
                    case 6: widget = Core::MessageManager::outputWindow(); break;
                    case 7: widget = Core::ICore::mainWindow(); break;
                    case 8: widget = m_profile; break; // only while the Code Profiler is shown
                    default: return errResult(QStringLiteral("invalid_argument"), QStringLiteral("`view` must be one of: %1.").arg(views.join(QStringLiteral(", "))));
                }

                if(!widget) return errResult(QStringLiteral("not_available"), QStringLiteral("That view does not exist yet."));

                // A pane that was never shown has no useful size; give it one to render at.
                if((widget->width() < 64) || (widget->height() < 64)) widget->resize(800, 600);

                QJsonObject out{{QStringLiteral("view"), view}, {QStringLiteral("visible"), widget->isVisible()}};
                const ImageContent image = imageContent(widget->grab(), imageArgs(args), &out);
                return okResult(out).addContent(image);
            });
    }

    // Runs a plugin flow inside the call's scope and waits until the plugin is
    // idle again or the deadline passes. Firmware flows tear m_working down and
    // back up between their stages, so the sticky flag is waited on as well.
    auto runFlow = [this](OMVMCP::Call &call, const std::function<void()> &invoke) {
        OMVMCP::Scope scope(call);
        invoke();

        while((m_working || m_firmwareUpdateInProgress) && !call.expired())
        {
            awaitSignal(this, &OpenMVPlugin::workingDone, qMin(call.remainingMs(), 250));
        }
    };

    auto flowBusy = [this](const OMVMCP::Call &call) {
        return call.expired() && (m_working || m_firmwareUpdateInProgress);
    };

    // G2 gate: the agent must pass confirm:true and, when the client can put a
    // question to the user, the user confirms too (whatever the call's policy
    // and whatever `answers` say).
    auto gateDestructive = [](OMVMCP::Call &call, const QJsonObject &args, const QString &title, const QString &text) -> std::optional<CallToolResult> {
        if(!args.value(QStringLiteral("confirm")).toBool())
        {
            return errResult(QStringLiteral("confirm_required"), QStringLiteral("This operation is destructive; pass confirm:true to run it. %1").arg(text));
        }

        if(!call.supportsElicitation())
        {
            call.note(QStringLiteral("[mcp.confirm] %1 -> Yes (confirm:true; the client cannot ask the user)").arg(title));
            return std::nullopt;
        }

        const OMVMCP::Policy policy = call.policy;
        call.policy = OMVMCP::Policy::Elicit;
        call.answers.remove(OMV_UI_SITE("mcp.confirm"));

        QMessageBox::StandardButton answer;
        {
            OMVMCP::Scope scope(call);
            answer = OMVUI::question(Core::ICore::dialogParent(), title, text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No, OMV_UI_SITE("mcp.confirm"));
        }

        call.policy = policy;
        if(answer != QMessageBox::Yes) return errResult(QStringLiteral("declined"), QStringLiteral("The user did not confirm the operation."), outcome(call));
        return std::nullopt;
    };

    auto firmwareString = [this] {
        return QStringLiteral("%1.%2.%3").arg(m_major).arg(m_minor).arg(m_patch);
    };

    // ---- openmv_firmware_install (G2) ---------------------------------------

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("source"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"), QJsonArray{QStringLiteral("release"), QStringLiteral("development"), QStringLiteral("file")}},
            {QStringLiteral("description"), QStringLiteral("release: the firmware bundled with this IDE for the attached board. development: the latest development build (downloaded). file: a local .bin/.dfu/.img/.zip named by `path`.")}});
        in.addProperty(QStringLiteral("path"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Local firmware file when source is file.")}});
        in.addProperty(QStringLiteral("eraseFilesystem"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), QStringLiteral("Also erase the camera's internal FAT file system (default false).")}});
        in.addProperty(QStringLiteral("resetRomfs"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), QStringLiteral("Also reset the ROM file system to its default (default false).")}});
        addConfirm(in);
        addCallControls(in);
        in.addRequired(QStringLiteral("source"));

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_firmware_install"))
                .title(QStringLiteral("Install firmware on the OpenMV camera"))
                .description(QStringLiteral("Flashes firmware exactly like the IDE's firmware update and Load Custom Firmware actions: disconnects, enters the bootloader, writes the image(s) and reconnects. Destructive: requires confirm:true and, when the client supports it, the user's confirmation. Dialogs the flow would show (board type, DFU device, physical-action prompts) come back in `errors` with site ids to pre-answer via `answers`. Takes minutes: raise deadlineMs if needed."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(true).idempotentHint(false))
                .inputSchema(in),
            [this, runFlow, flowBusy, gateDestructive, firmwareString](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                if(m_working || m_firmwareUpdateInProgress) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));

                const QString source = args.value(QStringLiteral("source")).toString();
                const QString path = args.value(QStringLiteral("path")).toString();
                const bool erase = args.value(QStringLiteral("eraseFilesystem")).toBool();
                const bool resetRomfs = args.value(QStringLiteral("resetRomfs")).toBool();

                if((source != QStringLiteral("release")) && (source != QStringLiteral("development")) && (source != QStringLiteral("file")))
                {
                    return errResult(QStringLiteral("invalid_argument"), QStringLiteral("`source` must be release, development or file."));
                }

                if((source == QStringLiteral("file")) && (path.isEmpty() || !QFileInfo(path).isFile()))
                {
                    return errResult(QStringLiteral("invalid_path"), QStringLiteral("`path` must name an existing firmware file (.bin, .dfu, .img or .zip)."));
                }

                if(m_connected && (source == QStringLiteral("development")) && !m_developmentReleaseAction->isEnabled())
                {
                    return errResult(QStringLiteral("not_available"), QStringLiteral("The camera is reachable over the network only; plug it in over USB to install firmware."));
                }

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;

                const QString what = (source == QStringLiteral("file")) ? QFileInfo(path).fileName()
                                   : QStringLiteral("the latest %1 firmware").arg(source);
                const QString extras = QStringList{erase ? QStringLiteral("erase the internal file system") : QString(),
                                                   resetRomfs ? QStringLiteral("reset the ROM file system") : QString()}
                                       .filter(QRegularExpression(QStringLiteral("."))).join(QStringLiteral(" and "));

                if(auto refused = gateDestructive(call, args, QStringLiteral("Install Firmware"),
                                                  QStringLiteral("Install %1 on the OpenMV Cam%2?").arg(what, extras.isEmpty() ? QString() : (QStringLiteral(" and ") + extras))))
                {
                    return *refused;
                }

                const QString before = m_connected ? firmwareString() : QString();

                runFlow(call, [&] {
                    if(source == QStringLiteral("file"))
                    {
                        loadCustomFirmware(path, erase, resetRomfs);
                    }
                    else
                    {
                        connectClicked(true, QString(), erase, false, source == QStringLiteral("development"), false, QString(),
                                       resetRomfs ? OPENMV_ROMFS_RESET : OPENMV_ROMFS_NONE);
                    }
                });

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("connected"), m_connected);
                if(!before.isEmpty()) out.insert(QStringLiteral("firmwareBefore"), before);
                if(m_connected) out.insert(QStringLiteral("firmware"), firmwareString());

                if(flowBusy(call)) return errResult(QStringLiteral("timeout"), QStringLiteral("The firmware flow did not finish within the deadline; the IDE is still working on it."), out);
                if(call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("The firmware flow reported an error; see errors."), out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- openmv_camera_erase_filesystem (G2) --------------------------------

    {
        Tool::InputSchema in;
        addConfirm(in);
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_camera_erase_filesystem"))
                .title(QStringLiteral("Erase the camera's internal file system"))
                .description(QStringLiteral("Erases the camera's internal FAT file system (not an SD card), exactly like Tools > Erase Internal FAT File System: every file on it, including main.py, is deleted. Destructive: requires confirm:true and, when the client supports it, the user's confirmation."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(true).idempotentHint(true))
                .inputSchema(in),
            [this, runFlow, flowBusy, gateDestructive](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                if(m_working || m_firmwareUpdateInProgress) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;

                if(auto refused = gateDestructive(call, args, QStringLiteral("Erase Internal FAT File System"),
                                                  QStringLiteral("Are you sure you want to erase your OpenMV Cam's internal FAT file system?")))
                {
                    return *refused;
                }

                runFlow(call, [this] { connectClicked(true, QString(), true, true); });

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("connected"), m_connected);

                if(flowBusy(call)) return errResult(QStringLiteral("timeout"), QStringLiteral("The erase did not finish within the deadline; the IDE is still working on it."), out);
                if(call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("The erase reported an error; see errors."), out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- openmv_camera_reset / openmv_camera_enter_bootloader (G1) ----------

    {
        Tool::InputSchema in;
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_camera_reset"))
                .title(QStringLiteral("Reset the OpenMV camera"))
                .description(QStringLiteral("Soft-resets the connected camera (the running script stops, the camera reboots and disconnects), exactly like Tools > Reset OpenMV Cam. Reconnect afterwards with openmv_camera_connect."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(true).idempotentHint(true))
                .inputSchema(in),
            [this, runFlow, flowBusy](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                if(m_working) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));
                if(!m_connected || !m_resetAction->isEnabled()) return errResult(QStringLiteral("not_connected"), QStringLiteral("No camera is connected."));

                OMVMCP::Call call = makeCall(params.argumentsAsObject());
                call.tool = toolInterface;

                runFlow(call, [this] { disconnectClicked(true); });

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("connected"), m_connected);

                if(flowBusy(call)) return errResult(QStringLiteral("timeout"), QStringLiteral("The reset did not finish within the deadline."), out);
                if(call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("The reset reported an error; see errors."), out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_camera_enter_bootloader"))
                .title(QStringLiteral("Put the OpenMV camera in its bootloader"))
                .description(QStringLiteral("Resets the connected camera into its bootloader (DFU) mode and disconnects, exactly like Tools > Enter Bootloader. Only available over USB."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(true).idempotentHint(true))
                .inputSchema(in),
            [this, runFlow, flowBusy](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                if(m_working) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));
                if(!m_connected) return errResult(QStringLiteral("not_connected"), QStringLiteral("No camera is connected."));
                if(!m_enterBootloaderAction->isEnabled()) return errResult(QStringLiteral("not_available"), QStringLiteral("The camera is reachable over the network only; plug it in over USB to enter the bootloader."));

                OMVMCP::Call call = makeCall(params.argumentsAsObject());
                call.tool = toolInterface;

                runFlow(call, [this] { disconnectClicked(true, true); });

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("connected"), m_connected);

                if(flowBusy(call)) return errResult(QStringLiteral("timeout"), QStringLiteral("Entering the bootloader did not finish within the deadline."), out);
                if(call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("Entering the bootloader reported an error; see errors."), out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- openmv_script_save_to_camera (G1) ----------------------------------

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("strip"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), QStringLiteral("Strip comments and convert spaces to tabs before saving (default false).")}});
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_script_save_to_camera"))
                .title(QStringLiteral("Save the current script to the camera"))
                .description(QStringLiteral("Writes the current editor's script to main.py on the connected camera's drive (so it runs at boot), exactly like Tools > Save open script to OpenMV Cam. Overwrites the existing main.py. Needs a connected camera with a mounted drive and a non-empty editor."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(true).idempotentHint(true))
                .inputSchema(in),
            [this, runFlow, flowBusy](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                if(m_working) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));
                if(!m_connected) return errResult(QStringLiteral("not_connected"), QStringLiteral("No camera is connected."));
                if(!m_saveAction->isEnabled()) return errResult(QStringLiteral("not_available"), QStringLiteral("Saving needs the camera's drive mounted and a non-empty script in the current editor."));

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;
                call.answers.insert(OMV_UI_SITE("script.save_strip"), args.value(QStringLiteral("strip")).toBool() ? QStringLiteral("Yes") : QStringLiteral("No"));

                runFlow(call, [this] { saveScript(); });

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("path"), QString(m_portPath + QDir::separator() + QStringLiteral("main.py")));

                if(flowBusy(call)) return errResult(QStringLiteral("timeout"), QStringLiteral("Saving did not finish within the deadline."), out);
                if(call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("Saving reported an error; see errors."), out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- openmv_terminal_clear ----------------------------------------------

    ToolRegistry::registerTool(
        Tool{}
            .name(QStringLiteral("openmv_terminal_clear"))
            .title(QStringLiteral("Forget the camera terminal history"))
            .description(QStringLiteral("Drops the text openmv_terminal_read would return and advances its cursor, so later reads only show new output. The IDE's own terminal window is untouched."))
            .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false).idempotentHint(true))
            .outputSchema(Tool::OutputSchema{}
                .addProperty(QStringLiteral("cursor"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}})
                .addRequired(QStringLiteral("cursor"))),
        [this](const CallToolRequestParams &) -> Utils::Result<CallToolResult> {
            m_mcpTerminalDropped += m_mcpTerminalLog.size();
            m_mcpTerminalLog.clear();
            return okResult(QJsonObject{{QStringLiteral("cursor"), m_mcpTerminalDropped}});
        });

    // ---- openmv_examples_list -----------------------------------------------

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("filter"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Case-insensitive substring to match against the relative paths (e.g. \"blob\", \"02-Image-Processing\").")}});

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_examples_list"))
                .title(QStringLiteral("List the bundled example scripts"))
                .description(QStringLiteral("Lists the MicroPython example scripts shipped with this IDE (the File > Examples menu) as paths relative to `root`; read a script with the file tools. When a development-firmware camera is attached the matching development examples are listed instead."))
                .annotations(ToolAnnotations{}.readOnlyHint(true))
                .inputSchema(in)
                .outputSchema(Tool::OutputSchema{}
                    .addProperty(QStringLiteral("root"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})
                    .addProperty(QStringLiteral("examples"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}})
                    .addRequired(QStringLiteral("root")).addRequired(QStringLiteral("examples"))),
            [this](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                const QString filter = params.argumentsAsObject().value(QStringLiteral("filter")).toString();
                const Utils::FilePath root = Core::ICore::allUsersResourcePath(devResourceFolder(QStringLiteral("examples")));
                const QDir rootDir(root.toString());

                QJsonArray examples;
                QDirIterator it(root.toString(), QStringList() << QStringLiteral("*.py"), QDir::Files, QDirIterator::Subdirectories);

                while(it.hasNext())
                {
                    const QString relative = rootDir.relativeFilePath(it.next());
                    if(filter.isEmpty() || relative.contains(filter, Qt::CaseInsensitive)) examples.append(relative);
                }

                return okResult(QJsonObject{
                    {QStringLiteral("root"), root.toUserOutput()},
                    {QStringLiteral("development"), root.fileName() != QStringLiteral("examples")},
                    {QStringLiteral("examples"), examples}});
            });
    }

    // ---- openmv_ide_status --------------------------------------------------

    ToolRegistry::registerTool(
        Tool{}
            .name(QStringLiteral("openmv_ide_status"))
            .title(QStringLiteral("OpenMV IDE status"))
            .description(QStringLiteral("The IDE's version, its resource directories (examples, firmware, tools) and the file open in the current editor."))
            .annotations(ToolAnnotations{}.readOnlyHint(true)),
        [](const CallToolRequestParams &) -> Utils::Result<CallToolResult> {
            Core::IDocument *document = Core::EditorManager::currentDocument();

            return okResult(QJsonObject{
                {QStringLiteral("name"), QGuiApplication::applicationDisplayName()},
                {QStringLiteral("version"), QCoreApplication::applicationVersion()},
                {QStringLiteral("resourcePath"), Core::ICore::resourcePath().toUserOutput()},
                {QStringLiteral("userResourcePath"), Core::ICore::userResourcePath().toUserOutput()},
                {QStringLiteral("allUsersResourcePath"), Core::ICore::allUsersResourcePath(QString()).toUserOutput()},
                {QStringLiteral("currentFile"), document ? document->filePath().toUserOutput() : QString()},
                {QStringLiteral("currentFileModified"), document ? document->isModified() : false}});
        });

    // ---- camera-side polls: openmv_camera_info / memory_stats / protocol_stats

    auto pollTimeout = [](const QJsonObject &args) {
        return qBound(500, args.value(QStringLiteral("timeoutMs")).toInt(5000), 60000);
    };

    auto addPollTimeout = [](Tool::InputSchema &in) -> Tool::InputSchema & {
        in.addProperty(QStringLiteral("timeoutMs"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), QStringLiteral("How long to wait for the camera's reply in milliseconds (default 5000).")}});
        return in;
    };

    {
        Tool::InputSchema in;
        addPollTimeout(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_camera_info"))
                .title(QStringLiteral("Query the camera's system information"))
                .description(QStringLiteral("Asks the connected camera for its system information (firmware and protocol versions, board, CPU, memory sizes, sensor and so on; the Board Info view) and returns the raw key/value map the camera reports, plus what the IDE knows about the board."))
                .annotations(ToolAnnotations{}.readOnlyHint(true))
                .inputSchema(in),
            [this, pollTimeout](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                if(!m_connected || !m_iodevice) return errResult(QStringLiteral("not_connected"), QStringLiteral("No camera is connected."));
                if(m_working) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));

                QVariantMap info;
                OpenMVPluginIO *io = m_iodevice;
                const bool got = awaitPoll(io, &OpenMVPluginIO::systemInfo, [&info](const QVariantMap &m) { info = m; },
                                           [io] { return io->getSystemInfoQueued(); }, [io] { io->getSystemInfo(); },
                                           pollTimeout(params.argumentsAsObject()));

                if(!got) return errResult(QStringLiteral("camera_timeout"), QStringLiteral("The camera did not answer the system information request."));

                QJsonObject out = QJsonObject::fromVariantMap(info);
                out.insert(QStringLiteral("ide"), QJsonObject{
                    {QStringLiteral("board"), m_boardType},
                    {QStringLiteral("fullBoard"), m_fullBoardType},
                    {QStringLiteral("boardId"), m_boardId},
                    {QStringLiteral("sensor"), m_sensorType},
                    {QStringLiteral("port"), m_portName},
                    {QStringLiteral("protocolV2"), io->v2ProtocolEnabled()}});
                return okResult(out);
            });

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_memory_stats"))
                .title(QStringLiteral("Query the camera's memory statistics"))
                .description(QStringLiteral("Asks the connected camera for its memory statistics (the Memory view): one entry per region with its size and usage. Empty on cameras whose firmware does not report them (protocol V1)."))
                .annotations(ToolAnnotations{}.readOnlyHint(true))
                .inputSchema(in),
            [this, pollTimeout](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                if(!m_connected || !m_iodevice) return errResult(QStringLiteral("not_connected"), QStringLiteral("No camera is connected."));
                if(m_working) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));

                QVariantList entries;
                OpenMVPluginIO *io = m_iodevice;
                const bool got = awaitPoll(io, &OpenMVPluginIO::memoryStats, [&entries](const QVariantList &l) { entries = l; },
                                           [io] { return io->getMemoryStatsQueued(); }, [io] { io->getMemoryStats(); },
                                           pollTimeout(params.argumentsAsObject()));

                if(!got) return errResult(QStringLiteral("camera_timeout"), QStringLiteral("The camera did not answer the memory statistics request."));

                return okResult(QJsonObject{
                    {QStringLiteral("supported"), io->v2ProtocolEnabled()},
                    {QStringLiteral("regions"), QJsonArray::fromVariantList(entries)}});
            });

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_protocol_stats"))
                .title(QStringLiteral("Query the debug-protocol statistics"))
                .description(QStringLiteral("Returns the host-side and camera-side debug protocol counters (the Statistics view): bytes and packets moved, errors, retries, per-channel traffic. Protocol V2 cameras only."))
                .annotations(ToolAnnotations{}.readOnlyHint(true))
                .inputSchema(in),
            [this, pollTimeout](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                if(!m_connected || !m_iodevice) return errResult(QStringLiteral("not_connected"), QStringLiteral("No camera is connected."));
                if(m_working) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));
                if(!m_iodevice->v2ProtocolEnabled()) return errResult(QStringLiteral("unsupported_on_v1"), QStringLiteral("This camera's firmware uses the V1 debug protocol, which has no statistics."));

                QVariantMap host, device;
                QVariantList channels;
                OpenMVPluginIO *io = m_iodevice;
                const bool got = awaitPoll(io, &OpenMVPluginIO::protocolStats,
                                           [&](const QVariantMap &h, const QVariantMap &d, const QVariantList &c) { host = h; device = d; channels = c; },
                                           [io] { return io->getProtocolStatsQueued(); }, [io] { io->getProtocolStats(); },
                                           pollTimeout(params.argumentsAsObject()));

                if(!got) return errResult(QStringLiteral("camera_timeout"), QStringLiteral("The camera did not answer the protocol statistics request."));

                return okResult(QJsonObject{
                    {QStringLiteral("host"), QJsonObject::fromVariantMap(host)},
                    {QStringLiteral("device"), QJsonObject::fromVariantMap(device)},
                    {QStringLiteral("channels"), QJsonArray::fromVariantList(channels)}});
            });
    }

    // ---- openmv_script_compile ----------------------------------------------

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("path"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("The .py script to convert.")}});
        in.addProperty(QStringLiteral("mode"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"), QJsonArray{QStringLiteral("copy"), QStringLiteral("clean"), QStringLiteral("clean_comments"), QStringLiteral("bytecode")}},
            {QStringLiteral("description"), QStringLiteral("copy: leave the script as is; clean: normalise whitespace; clean_comments: also strip comments; bytecode: compile to a .mpy with mpy-cross for the target board (default).")}});
        in.addProperty(QStringLiteral("options"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Extra mpy-cross command-line options (bytecode mode only).")}});
        in.addProperty(QStringLiteral("board"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Board type to compile for when no camera is connected (answers site connect.board_type; the error lists the choices).")}});
        in.addRequired(QStringLiteral("path"));
        addEmbed(in);
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_script_compile"))
                .title(QStringLiteral("Compile or clean a script with mpy-cross"))
                .description(QStringLiteral("Runs the IDE's Copy/Convert Python File step on a script: cleans it or compiles it to MicroPython bytecode (.mpy) with the bundled mpy-cross using the target board's architecture flags. Returns the path of the converted file (in a temporary directory) and the compiler's console."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false).idempotentHint(true))
                .inputSchema(in)
                .outputSchema(Tool::OutputSchema{}
                    .addProperty(QStringLiteral("outputPath"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})),
            [this](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                const QString path = args.value(QStringLiteral("path")).toString();
                if(!QFileInfo(path).isFile() || !path.endsWith(QStringLiteral(".py"), Qt::CaseInsensitive)) return errResult(QStringLiteral("invalid_path"), QStringLiteral("`path` must name an existing .py file."));
                if(m_working) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));

                const QStringList modes{QStringLiteral("copy"), QStringLiteral("clean"), QStringLiteral("clean_comments"), QStringLiteral("bytecode")};
                const int mode = qMax(0, modes.indexOf(args.value(QStringLiteral("mode")).toString(QStringLiteral("bytecode"))));
                const QString options = args.value(QStringLiteral("options")).toString();

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;
                if(args.contains(QStringLiteral("board"))) call.answers.insert(OMV_UI_SITE("connect.board_type"), args.value(QStringLiteral("board")).toString());
                call.answers.insert(OMV_UI_SITE("mpy.options"), QJsonObject{
                    {QStringLiteral("mode"), mode},
                    {QStringLiteral("advanced"), !options.isEmpty()},
                    {QStringLiteral("options"), options}});

                QString outputPath;
                bool noBoard = false;

                {
                    OMVMCP::Scope scope(call);
                    Utils::QtcSettings *settings = Core::ICore::settings();

                    // The board settings carry the mpy-cross architecture flags.
                    const QJsonObject boardSettings = getBoardSettings(QStringLiteral("Compile Script"), settings, true);
                    if(boardSettings.isEmpty()) noBoard = true;
                    else outputPath = convertScript(boardSettings, path, settings);
                }

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("mode"), modes.at(mode));
                if(!outputPath.isEmpty()) out.insert(QStringLiteral("outputPath"), outputPath);

                if(noBoard) return errResult(QStringLiteral("needs_input"), QStringLiteral("No board type was chosen; pass `board` (see errors for the choices)."), out);
                if(outputPath.isEmpty() || call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("The conversion did not produce a file; see errors and log."), out);
                return fileResult(out, outputPath, args.value(QStringLiteral("embed")).toBool());
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- openmv_model_compile -----------------------------------------------

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("path"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("The model file (.tflite/.lite for Vela boards, .tflite/.onnx for STEdgeAI boards).")}});
        in.addProperty(QStringLiteral("optimize"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Optimisation preset by label or index: Vela boards offer \"Optimize for Performance\" / \"Optimize for Size\"; STEdgeAI boards \"Maximum\", \"Medium\", \"Low\" or \"No Optimization\". Default: the IDE's last choice.")}});
        in.addProperty(QStringLiteral("options"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Extra compiler command-line options (Vela, or STEdgeAI Core).")}});
        in.addProperty(QStringLiteral("neuralArtOptions"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Extra STEdgeAI Neural-ART compiler options (STEdgeAI boards only).")}});
        in.addProperty(QStringLiteral("board"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Board type to compile for when no camera is connected (answers site connect.board_type; the error lists the choices).")}});
        in.addProperty(QStringLiteral("target"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("ROMFS target on boards that offer more than one (answers site romfs.target).")}});
        in.addRequired(QStringLiteral("path"));
        addEmbed(in);
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_model_compile"))
                .title(QStringLiteral("Compile a model for the board's NPU"))
                .description(QStringLiteral("Converts a neural-network model for the target board the way the IDE does before adding it to a ROMFS image: Vela for Ethos-U boards (e.g. OpenMV AE3), STEdgeAI for Neural-ART boards (e.g. OpenMV N6). Boards without an accelerator, and models already converted, pass through unchanged. Returns the path of the converted file (in a temporary directory) and the compiler's console."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false).idempotentHint(true))
                .inputSchema(in)
                .outputSchema(Tool::OutputSchema{}
                    .addProperty(QStringLiteral("outputPath"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})
                    .addProperty(QStringLiteral("converted"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}})),
            [this](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                const QString path = args.value(QStringLiteral("path")).toString();
                if(!QFileInfo(path).isFile()) return errResult(QStringLiteral("invalid_path"), QStringLiteral("`path` must name an existing model file."));
                if(m_working) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));

                const QString options = args.value(QStringLiteral("options")).toString();
                const QString neuralArtOptions = args.value(QStringLiteral("neuralArtOptions")).toString();

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;
                if(args.contains(QStringLiteral("board"))) call.answers.insert(OMV_UI_SITE("connect.board_type"), args.value(QStringLiteral("board")).toString());
                if(args.contains(QStringLiteral("target"))) call.answers.insert(OMV_UI_SITE("romfs.target"), args.value(QStringLiteral("target")).toString());

                QJsonObject form{{QStringLiteral("advanced"), !(options.isEmpty() && neuralArtOptions.isEmpty())}, {QStringLiteral("options"), options}};
                if(args.contains(QStringLiteral("optimize"))) form.insert(QStringLiteral("optimize"), args.value(QStringLiteral("optimize")));
                call.answers.insert(OMV_UI_SITE("vela.options"), form);
                form.insert(QStringLiteral("neuralArtOptions"), neuralArtOptions);
                call.answers.insert(OMV_UI_SITE("stedgeai.options"), form);

                QString outputPath;
                QString stage;

                {
                    OMVMCP::Scope scope(call);
                    Utils::QtcSettings *settings = Core::ICore::settings();
                    const QString title = QStringLiteral("Compile Model");

                    QJsonObject boardSettings = getBoardSettings(title, settings, true);
                    if(boardSettings.isEmpty()) stage = QStringLiteral("board");
                    else
                    {
                        // Same shape the Model Zoo and ROMFS editor build: the
                        // chosen ROMFS target (with its NPU config) replaces the list.
                        if(boardSettings.contains(QStringLiteral("romfsConfig")))
                        {
                            const QJsonObject romfs = getROMFSConfig(title, boardSettings, settings);
                            if(romfs.isEmpty()) stage = QStringLiteral("target");
                            else boardSettings[QStringLiteral("romfsConfig")] = romfs;
                        }

                        if(stage.isEmpty()) outputPath = convertModel(boardSettings, path, settings);
                    }
                }

                QJsonObject out = outcome(call);
                if(!outputPath.isEmpty()) out.insert(QStringLiteral("outputPath"), outputPath);
                out.insert(QStringLiteral("converted"), !outputPath.isEmpty() && (outputPath != path));

                if(stage == QStringLiteral("board")) return errResult(QStringLiteral("needs_input"), QStringLiteral("No board type was chosen; pass `board` (see errors for the choices)."), out);
                if(stage == QStringLiteral("target")) return errResult(QStringLiteral("needs_input"), QStringLiteral("No ROMFS target was chosen; pass `target` (see errors for the choices)."), out);
                if(outputPath.isEmpty() || call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("The conversion did not produce a file; see errors and log."), out);
                return fileResult(out, outputPath, args.value(QStringLiteral("embed")).toBool());
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- openmv_actions_list / openmv_action --------------------------------
    // The whole menu surface: every OpenMV command, triggered the way a click
    // would be, with its dialogs answered through `answers`. The dedicated tools
    // above are the structured front doors; this is the long tail.

    {
        // Commands whose click changes the camera, its files or the user's data.
        const QStringList destructive{
            QStringLiteral("OpenMV.Erase"), QStringLiteral("OpenMV.Bootloader"), QStringLiteral("OpenMV.InstallTheLatestDevelopmentRelease"),
            QStringLiteral("OpenMV.ResetROMFS"), QStringLiteral("OpenMV.EditROMFS"), QStringLiteral("OpenMV.Save"), QStringLiteral("OpenMV.Reset"),
            QStringLiteral("OpenMV.EnterBootloader"), QStringLiteral("OpenMV.NewDataset"), QStringLiteral("OpenMV.CreateDefaultConfig"),
            QStringLiteral("OpenMV.EditWiFiDebugBootPy"), QStringLiteral("OpenMV.FirmwareUpdate")};

        auto describe = [destructive](Core::Command *command) {
            QAction *action = command->action();
            QJsonObject o{
                {QStringLiteral("id"), command->id().toString()},
                {QStringLiteral("text"), action->text().remove(QLatin1Char('&'))},
                {QStringLiteral("enabled"), action->isEnabled()},
                {QStringLiteral("visible"), action->isVisible()},
                {QStringLiteral("destructive"), destructive.contains(command->id().toString())}};
            if(!command->description().isEmpty() && (command->description() != action->text())) o.insert(QStringLiteral("description"), command->description());
            if(!action->toolTip().isEmpty() && (action->toolTip() != action->text())) o.insert(QStringLiteral("tooltip"), action->toolTip());
            if(action->isCheckable()) o.insert(QStringLiteral("checked"), action->isChecked());
            return o;
        };

        Tool::InputSchema ls;
        ls.addProperty(QStringLiteral("filter"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Case-insensitive substring to match against the id or text.")}});
        ls.addProperty(QStringLiteral("enabledOnly"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), QStringLiteral("Only commands that can be triggered right now (default false).")}});

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_actions_list"))
                .title(QStringLiteral("List the OpenMV IDE commands"))
                .description(QStringLiteral("Every OpenMV menu command (id, text, whether it is enabled right now, checkable state, whether it is destructive) for use with openmv_action. Prefer a dedicated openmv_* tool when one exists; this is the long tail (settings editor, model zoo, video tools, third-party packages, ROMFS, datasets, terminals...)."))
                .annotations(ToolAnnotations{}.readOnlyHint(true))
                .inputSchema(ls),
            [describe](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                const QJsonObject args = params.argumentsAsObject();
                const QString filter = args.value(QStringLiteral("filter")).toString();
                const bool enabledOnly = args.value(QStringLiteral("enabledOnly")).toBool();

                QJsonArray commands;

                for(Core::Command *command : Core::ActionManager::commands())
                {
                    if(!command->action() || !command->id().toString().startsWith(QStringLiteral("OpenMV."))) continue;
                    if(enabledOnly && !command->action()->isEnabled()) continue;

                    const QString text = command->action()->text().remove(QLatin1Char('&'));
                    if(!filter.isEmpty() && !command->id().toString().contains(filter, Qt::CaseInsensitive) && !text.contains(filter, Qt::CaseInsensitive)) continue;

                    commands.append(describe(command));
                }

                return okResult(QJsonObject{{QStringLiteral("commands"), commands}});
            });

        Tool::InputSchema in;
        in.addProperty(QStringLiteral("id"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("The command id from openmv_actions_list (e.g. OpenMV.OpenDataset).")}});
        in.addProperty(QStringLiteral("checked"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), QStringLiteral("For a checkable command: set it to this state instead of toggling it.")}});
        in.addProperty(QStringLiteral("confirm"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), QStringLiteral("Required (true) for commands listed as destructive; the user is also asked when the client supports elicitation.")}});
        in.addRequired(QStringLiteral("id"));
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_action"))
                .title(QStringLiteral("Trigger an OpenMV IDE command"))
                .description(QStringLiteral("Clicks an OpenMV menu command and waits for the flow it starts to finish. Every dialog it would have shown comes back in `errors` with a site id (or the form's fields) to answer through `answers` on the next call. Destructive commands need confirm:true."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(true))
                .inputSchema(in),
            [this, runFlow, flowBusy, gateDestructive, destructive, describe](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                const QString id = args.value(QStringLiteral("id")).toString();

                Core::Command *command = Core::ActionManager::command(Utils::Id::fromString(id));
                if(!command || !command->action() || !id.startsWith(QStringLiteral("OpenMV."))) return errResult(QStringLiteral("invalid_argument"), QStringLiteral("No OpenMV command \"%1\"; see openmv_actions_list.").arg(id));

                QAction *action = command->action();
                if(!action->isEnabled()) return errResult(QStringLiteral("not_available"), QStringLiteral("\"%1\" is disabled right now (it usually needs a connected camera, a mounted drive or an open dataset).").arg(action->text().remove(QLatin1Char('&'))));
                if(m_working || m_firmwareUpdateInProgress) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;

                if(destructive.contains(id))
                {
                    if(auto refused = gateDestructive(call, args, action->text().remove(QLatin1Char('&')), QStringLiteral("Run \"%1\"?").arg(action->text().remove(QLatin1Char('&'))))) return *refused;
                }

                const bool setChecked = action->isCheckable() && args.contains(QStringLiteral("checked"));
                const bool checked = args.value(QStringLiteral("checked")).toBool();

                runFlow(call, [action, setChecked, checked] {
                    if(setChecked) action->setChecked(checked); else action->trigger();
                });

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("command"), describe(command));

                if(flowBusy(call)) return errResult(QStringLiteral("timeout"), QStringLiteral("The command's flow did not finish within the deadline."), out);
                if(call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("The command reported an error; see errors."), out);
                if(call.hasErrors(1) && std::any_of(call.errors.cbegin(), call.errors.cend(), [](const OMVMCP::Error &e) { return e.code == QStringLiteral("needs_input"); }))
                {
                    return errResult(QStringLiteral("needs_input"), QStringLiteral("The command needed input it did not get; answer the sites in `errors` and call again."), out);
                }

                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- datasets -----------------------------------------------------------
    // The Dataset Editor dock: a folder of <class>.class folders of numbered
    // JPEG snapshots of the frame buffer, plus labels.txt and a capture script.

    auto triggerCommand = [this, runFlow](OMVMCP::Call &call, const char *id) -> bool {
        Core::Command *command = Core::ActionManager::command(Utils::Id::fromString(QString::fromLatin1(id)));
        if(!command || !command->action() || !command->action()->isEnabled()) return false;
        runFlow(call, [command] { command->action()->trigger(); });
        return true;
    };

    auto datasetInfo = [this] {
        const QString root = m_datasetEditor ? m_datasetEditor->rootPath() : QString();
        QJsonObject o{{QStringLiteral("open"), !root.isEmpty()}};
        if(root.isEmpty()) return o;

        QJsonArray classes;

        for(const QString &folder : m_datasetEditor->classFolderList())
        {
            QString name = folder;
            if(name.endsWith(QStringLiteral(".class"))) name.chop(6);
            classes.append(QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("folder"), folder}, {QStringLiteral("snapshots"), m_datasetEditor->snapshotList(folder).size()}});
        }

        o.insert(QStringLiteral("root"), root);
        o.insert(QStringLiteral("captureScript"), QString(root + QStringLiteral("/dataset_capture_script.py")));
        o.insert(QStringLiteral("classes"), classes);
        return o;
    };

    ToolRegistry::registerTool(
        Tool{}
            .name(QStringLiteral("openmv_dataset_info"))
            .title(QStringLiteral("Describe the open dataset"))
            .description(QStringLiteral("The dataset open in the Dataset Editor: its folder, capture script, classes and how many snapshots each has."))
            .annotations(ToolAnnotations{}.readOnlyHint(true)),
        [datasetInfo](const CallToolRequestParams &) -> Utils::Result<CallToolResult> {
            return okResult(datasetInfo());
        });

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("path"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("The folder to build the dataset in (created if missing).")}});
        in.addProperty(QStringLiteral("confirm"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), QStringLiteral("Required (true) to wipe the folder when it is not empty.")}});
        in.addRequired(QStringLiteral("path"));
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_dataset_new"))
                .title(QStringLiteral("Create a dataset"))
                .description(QStringLiteral("Creates a new dataset in `path` exactly like Tools > Dataset Editor > New Dataset: writes a dataset_capture_script.py, opens it in the editor and opens the Dataset Editor on the folder. A non-empty folder is wiped first, which needs confirm:true."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(true))
                .inputSchema(in),
            [this, triggerCommand, gateDestructive, datasetInfo](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                const QString path = args.value(QStringLiteral("path")).toString();
                if(path.isEmpty()) return errResult(QStringLiteral("invalid_path"), QStringLiteral("`path` is required."));
                if(!QDir(path).exists() && !QDir().mkpath(path)) return errResult(QStringLiteral("invalid_path"), QStringLiteral("Cannot create \"%1\".").arg(path));

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;

                const bool nonEmpty = !QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();
                if(nonEmpty)
                {
                    if(auto refused = gateDestructive(call, args, QStringLiteral("New Dataset"), QStringLiteral("\"%1\" is not empty and its contents will be deleted. Continue?").arg(path))) return *refused;
                }

                call.answers.insert(OMV_UI_SITE("dataset.new_path"), path);
                call.answers.insert(OMV_UI_SITE("dataset.new_clear"), QStringLiteral("Yes"));

                if(!triggerCommand(call, "OpenMV.NewDataset")) return errResult(QStringLiteral("not_available"), QStringLiteral("The New Dataset command is not available."));

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("dataset"), datasetInfo());
                if(call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("Creating the dataset reported an error; see errors."), out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("path"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("A dataset folder (one containing dataset_capture_script.py).")}});
        in.addRequired(QStringLiteral("path"));
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_dataset_open"))
                .title(QStringLiteral("Open a dataset"))
                .description(QStringLiteral("Opens an existing dataset folder in the Dataset Editor (and its capture script in the editor), like Tools > Dataset Editor > Open Dataset."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false).idempotentHint(true))
                .inputSchema(in),
            [this, triggerCommand, datasetInfo](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                const QString path = args.value(QStringLiteral("path")).toString();
                if(!QDir(path).exists()) return errResult(QStringLiteral("invalid_path"), QStringLiteral("\"%1\" does not exist.").arg(path));

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;
                call.answers.insert(OMV_UI_SITE("dataset.open_path"), path);

                if(!triggerCommand(call, "OpenMV.OpenDataset")) return errResult(QStringLiteral("not_available"), QStringLiteral("The Open Dataset command is not available."));

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("dataset"), datasetInfo());
                if(call.hasErrors(2) || (m_datasetEditor->rootPath().isEmpty())) return errResult(QStringLiteral("failed"), QStringLiteral("The dataset did not open; see errors."), out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    ToolRegistry::registerTool(
        Tool{}
            .name(QStringLiteral("openmv_dataset_close"))
            .title(QStringLiteral("Close the open dataset"))
            .description(QStringLiteral("Closes the dataset in the Dataset Editor (files are untouched)."))
            .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false).idempotentHint(true)),
        [this, datasetInfo](const CallToolRequestParams &) -> Utils::Result<CallToolResult> {
            if(m_datasetEditor && !m_datasetEditor->rootPath().isEmpty())
            {
                Core::Command *command = Core::ActionManager::command(Utils::Id("OpenMV.CloseDataset"));
                if(command && command->action() && command->action()->isEnabled()) command->action()->trigger();
                else m_datasetEditor->setRootPath(QString());
            }

            return okResult(datasetInfo());
        });

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("name"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("The class name (becomes the <name>.class folder and a labels.txt line).")}});
        in.addRequired(QStringLiteral("name"));
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_dataset_add_class"))
                .title(QStringLiteral("Add a class to the open dataset"))
                .description(QStringLiteral("Adds a class folder to the open dataset and updates labels.txt, like the Dataset Editor's New Class Folder button."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false))
                .inputSchema(in),
            [this, datasetInfo](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                const QString name = args.value(QStringLiteral("name")).toString().trimmed();
                if(name.isEmpty()) return errResult(QStringLiteral("invalid_argument"), QStringLiteral("`name` is required."));
                if(!m_datasetEditor || m_datasetEditor->rootPath().isEmpty()) return errResult(QStringLiteral("not_available"), QStringLiteral("No dataset is open; use openmv_dataset_open or openmv_dataset_new."));

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;
                call.answers.insert(OMV_UI_SITE("dataset.class_name"), name);

                {
                    OMVMCP::Scope scope(call);
                    m_datasetEditor->newClassFolder();
                }

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("dataset"), datasetInfo());
                if(call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("Adding the class reported an error; see errors."), out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("class"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("The class to capture into (name or <name>.class folder).")}});
        in.addProperty(QStringLiteral("count"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), QStringLiteral("How many new frames to save, 1-100 (default 1).")}});
        in.addProperty(QStringLiteral("intervalMs"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), QStringLiteral("Minimum time between saved frames in milliseconds (default 0: consecutive frames).")}});
        in.addProperty(QStringLiteral("deadlineMs"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), QStringLiteral("Give up after this long if frames stop arriving (default 30000).")}});
        in.addRequired(QStringLiteral("class"));

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_dataset_capture"))
                .title(QStringLiteral("Save camera frames into a dataset class"))
                .description(QStringLiteral("Saves the next frame(s) from the running script into a class of the open dataset as numbered JPEGs, exactly like the Dataset Editor's Capture Data button. Needs a connected camera streaming its frame buffer."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false))
                .inputSchema(in),
            [this](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                const QJsonObject args = params.argumentsAsObject();
                if(!m_datasetEditor || m_datasetEditor->rootPath().isEmpty()) return errResult(QStringLiteral("not_available"), QStringLiteral("No dataset is open."));
                if(!m_connected || !m_frameBuffer) return errResult(QStringLiteral("not_connected"), QStringLiteral("No camera is connected."));

                QString folder = args.value(QStringLiteral("class")).toString().trimmed();
                if(!folder.endsWith(QStringLiteral(".class"))) folder += QStringLiteral(".class");
                const QString path = m_datasetEditor->rootPath() + QLatin1Char('/') + folder;
                if(!QDir(path).exists()) return errResult(QStringLiteral("invalid_argument"), QStringLiteral("No class \"%1\" in the open dataset; see openmv_dataset_info.").arg(folder));

                const int count = qBound(1, args.value(QStringLiteral("count")).toInt(1), 100);
                const int intervalMs = qBound(0, args.value(QStringLiteral("intervalMs")).toInt(0), 60000);
                const int deadlineMs = qBound(1000, args.value(QStringLiteral("deadlineMs")).toInt(30000), 600000);

                QElapsedTimer wall;
                wall.start();

                QJsonArray saved;
                qint64 last = -1;

                while((saved.size() < count) && (wall.elapsed() < deadlineMs))
                {
                    if((last >= 0) && (intervalMs > 0))
                    {
                        QEventLoop loop;
                        QTimer::singleShot(qMax<qint64>(0, last + intervalMs - wall.elapsed()), &loop, &QEventLoop::quit);
                        loop.exec();
                    }

                    if(!awaitSignal(m_frameBuffer, &OpenMVPluginFB::pixmapUpdate, int(deadlineMs - wall.elapsed()))) break;
                    QCoreApplication::processEvents(); // let the editor take the same frame first

                    const QString file = m_datasetEditor->snapshotTo(path);
                    if(file.isEmpty()) return errResult(QStringLiteral("failed"), QStringLiteral("Saving a snapshot failed."), QJsonObject{{QStringLiteral("saved"), saved}});
                    saved.append(file);
                    last = wall.elapsed();
                }

                if(saved.isEmpty()) return errResult(QStringLiteral("timeout"), QStringLiteral("No frame arrived from the camera; is a script streaming the frame buffer?"));
                return okResult(QJsonObject{{QStringLiteral("saved"), saved}, {QStringLiteral("complete"), saved.size() == count}, {QStringLiteral("class"), folder}});
            });
    }

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("path"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("The .zip to write.")}});
        in.addRequired(QStringLiteral("path"));
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_dataset_export"))
                .title(QStringLiteral("Export the open dataset to a zip"))
                .description(QStringLiteral("Zips the open dataset, like Tools > Dataset Editor > Export > Export Dataset to Zip File."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false).idempotentHint(true))
                .inputSchema(in),
            [this, triggerCommand](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                const QString path = args.value(QStringLiteral("path")).toString();
                if(path.isEmpty() || !path.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) return errResult(QStringLiteral("invalid_path"), QStringLiteral("`path` must end in .zip."));
                if(!m_datasetEditor || m_datasetEditor->rootPath().isEmpty()) return errResult(QStringLiteral("not_available"), QStringLiteral("No dataset is open."));

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;
                call.answers.insert(OMV_UI_SITE("dataset.export_path"), path);

                if(!triggerCommand(call, "OpenMV.ExportDataset")) return errResult(QStringLiteral("not_available"), QStringLiteral("The Export Dataset command is not available (the Dataset Editor must be showing)."));

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("path"), path);
                if(call.hasErrors(2) || !QFileInfo::exists(path)) return errResult(QStringLiteral("failed"), QStringLiteral("The export did not produce a file; see errors."), out);
                return fileResult(out, path, args.value(QStringLiteral("embed")).toBool());
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- ROMFS --------------------------------------------------------------
    // The camera's read-only file system (models, assets): pull it into a
    // folder to inspect, edit the folder with file tools, write it back.

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("dir"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Folder to unpack into (created; default: a new temporary folder).")}});
        in.addProperty(QStringLiteral("target"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("ROMFS target on boards that have more than one (answers site romfs.target).")}});
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_romfs_read"))
                .title(QStringLiteral("Read the camera's ROMFS into a folder"))
                .description(QStringLiteral("Pulls the attached camera's ROM file system (through its bootloader, like Tools > ROMFS > Edit ROMFS on OpenMV Cam) and unpacks it into `dir`, then lists the files. The camera reconnects afterwards. Edit the folder with file tools and write it back with openmv_romfs_write."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false))
                .inputSchema(in),
            [this](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                if(m_working || m_firmwareUpdateInProgress) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));

                QString dir = args.value(QStringLiteral("dir")).toString();
                if(dir.isEmpty()) dir = QDir::tempPath() + QStringLiteral("/openmv-romfs-") + QString::number(QDateTime::currentMSecsSinceEpoch());
                if(!QDir(dir).exists() && !QDir().mkpath(dir)) return errResult(QStringLiteral("invalid_path"), QStringLiteral("Cannot create \"%1\".").arg(dir));

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;
                if(args.contains(QStringLiteral("target"))) call.answers.insert(OMV_UI_SITE("romfs.target"), args.value(QStringLiteral("target")).toString());

                QString error;
                bool ok = false;

                {
                    OMVMCP::Scope scope(call);
                    ok = romfsReadToDir(dir, &error);

                    while((m_working || m_firmwareUpdateInProgress) && !call.expired())
                    {
                        awaitSignal(this, &OpenMVPlugin::workingDone, qMin(call.remainingMs(), 250));
                    }
                }

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("dir"), dir);
                out.insert(QStringLiteral("connected"), m_connected);

                if(ok)
                {
                    QJsonArray files;
                    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);

                    while(it.hasNext())
                    {
                        it.next();
                        files.append(QJsonObject{{QStringLiteral("path"), QDir(dir).relativeFilePath(it.filePath())}, {QStringLiteral("bytes"), it.fileInfo().size()}});
                    }

                    out.insert(QStringLiteral("files"), files);
                    return okResult(out);
                }

                return errResult(QStringLiteral("failed"), error.isEmpty() ? QStringLiteral("Reading the ROMFS failed; see errors.") : error, out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("dir"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Folder whose contents become the ROMFS (as unpacked by openmv_romfs_read).")}});
        in.addProperty(QStringLiteral("target"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("ROMFS target on boards that have more than one (answers site romfs.target).")}});
        in.addRequired(QStringLiteral("dir"));
        addConfirm(in);
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_romfs_write"))
                .title(QStringLiteral("Write a folder to the camera's ROMFS"))
                .description(QStringLiteral("Packs `dir` into a ROMFS image for the attached board and writes it through the bootloader, like Commit in the ROMFS editor; the camera reconnects afterwards. Replaces the whole ROMFS. Destructive: requires confirm:true and, when the client supports it, the user's confirmation."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(true))
                .inputSchema(in),
            [this, gateDestructive](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                const QString dir = args.value(QStringLiteral("dir")).toString();
                if(!QDir(dir).exists()) return errResult(QStringLiteral("invalid_path"), QStringLiteral("\"%1\" does not exist.").arg(dir));
                if(m_working || m_firmwareUpdateInProgress) return errResult(QStringLiteral("busy"), QStringLiteral("The IDE is already performing a camera operation."));

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;
                if(args.contains(QStringLiteral("target"))) call.answers.insert(OMV_UI_SITE("romfs.target"), args.value(QStringLiteral("target")).toString());

                if(auto refused = gateDestructive(call, args, QStringLiteral("Commit ROMFS"), QStringLiteral("Replace the OpenMV Cam's ROM file system with the contents of \"%1\"?").arg(dir))) return *refused;

                QString error;
                bool ok = false;

                {
                    OMVMCP::Scope scope(call);
                    ok = romfsWriteFromDir(dir, &error);

                    while((m_working || m_firmwareUpdateInProgress) && !call.expired())
                    {
                        awaitSignal(this, &OpenMVPlugin::workingDone, qMin(call.remainingMs(), 250));
                    }
                }

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("dir"), dir);
                out.insert(QStringLiteral("connected"), m_connected);

                if(!ok) return errResult(QStringLiteral("failed"), error.isEmpty() ? QStringLiteral("Writing the ROMFS failed; see errors.") : error, out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }

    // ---- openmv_framebuffer_set ---------------------------------------------
    // The Frame Buffer bar's controls (combos, not commands).

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("source"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Stream source by its label as shown in the bar (\"Off\", \"On\", or a named stream) or index.")}});
        in.addProperty(QStringLiteral("jpeg"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), QStringLiteral("true = JPG Mode (compressed frames, fast), false = RAW Mode (exact pixels).")}});
        in.addProperty(QStringLiteral("fit"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("boolean")},
            {QStringLiteral("description"), QStringLiteral("Zoom the frame to fit the pane (true) or show it 1:1 (false).")}});

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_framebuffer_set"))
                .title(QStringLiteral("Set the Frame Buffer bar controls"))
                .description(QStringLiteral("Changes the frame buffer stream source (Off/On/named stream), JPG vs RAW transfer mode and fit-to-window zoom, like the controls above the Frame Buffer pane. Returns the resulting state and the available sources."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false).idempotentHint(true))
                .inputSchema(in),
            [this](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                const QJsonObject args = params.argumentsAsObject();
                if(!m_frameBufferSource || !m_jpgCompress) return errResult(QStringLiteral("not_available"), QStringLiteral("The Frame Buffer bar does not exist yet."));

                QStringList unknown;

                if(args.contains(QStringLiteral("source")))
                {
                    const QJsonValue v = args.value(QStringLiteral("source"));
                    int i = v.isDouble() ? v.toInt(-1) : m_frameBufferSource->findText(v.toString(), Qt::MatchFixedString);
                    if((i < 0) || (i >= m_frameBufferSource->count())) unknown.append(QStringLiteral("source"));
                    else m_frameBufferSource->setCurrentIndex(i);
                }

                if(args.contains(QStringLiteral("jpeg")))
                {
                    const int i = m_jpgCompress->findData(args.value(QStringLiteral("jpeg")).toBool());
                    if(i < 0) unknown.append(QStringLiteral("jpeg"));
                    else m_jpgCompress->setCurrentIndex(i);
                }

                if(args.contains(QStringLiteral("fit")) && m_frameBuffer) m_frameBuffer->enableFitInView(args.value(QStringLiteral("fit")).toBool());

                QJsonArray sources;
                for(int i = 0; i < m_frameBufferSource->count(); i++) sources.append(m_frameBufferSource->itemText(i));

                QJsonObject out{
                    {QStringLiteral("source"), m_frameBufferSource->currentText()},
                    {QStringLiteral("sources"), sources},
                    {QStringLiteral("jpeg"), m_jpgCompress->currentData().toBool()}};

                if(!unknown.isEmpty()) return errResult(QStringLiteral("invalid_argument"), QStringLiteral("Not a valid value for: %1.").arg(unknown.join(QStringLiteral(", "))), out);
                return okResult(out);
            });
    }

    // ---- openmv_framebuffer_save --------------------------------------------

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("path"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("Where to write the image; the extension picks the format (.bmp .jpg .jpeg .png .ppm).")}});
        in.addRequired(QStringLiteral("path"));
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_framebuffer_save"))
                .title(QStringLiteral("Save the camera frame buffer to a file"))
                .description(QStringLiteral("Writes the most recent frame the IDE received from the camera to `path`, exactly like right-click > Save Image on the frame buffer."))
                .annotations(ToolAnnotations{}.readOnlyHint(false).destructiveHint(false).idempotentHint(true))
                .inputSchema(in),
            [this](const CallToolRequestParams &params, const Mcp::ToolInterface &toolInterface) -> Utils::Result<> {
                auto body = [&]() -> CallToolResult {
                const QJsonObject args = params.argumentsAsObject();
                if(!m_frameBuffer || !m_frameBuffer->pixmapValid()) return errResult(QStringLiteral("not_available"), QStringLiteral("No frame has been received from the camera."));

                const QString path = args.value(QStringLiteral("path")).toString();
                if(path.isEmpty()) return errResult(QStringLiteral("invalid_path"), QStringLiteral("`path` is required."));

                OMVMCP::Call call = makeCall(args);
                call.tool = toolInterface;
                call.answers.insert(OMV_UI_SITE("fb.save_image_path"), path);

                {
                    OMVMCP::Scope scope(call);
                    saveImage(m_frameBuffer->pixmap());
                }

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("path"), path);
                if(call.hasErrors(1)) return errResult(QStringLiteral("failed"), QStringLiteral("Saving reported an error; see errors."), out);
                return okResult(out);
                };

                return runTool(toolInterface, params, body);
            });
    }
}

} // namespace Internal
} // namespace OpenMV
