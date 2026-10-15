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
// callback, enters a McpUi::Scope, invokes the existing plugin slot, and waits
// (nested event loop, wall-clock bounded) for the flow to finish. The result is
// the call's structured error ledger plus whatever the tool reads back.

#include "openmvplugin.h"
#include "openmvpluginio.h"
#include "openmvromfs.h"
#include "views/openmvpluginfb.h"
#include "mcp/omvmcpcontext.h"
#include "mcp/omvui.h"

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <mcp/server/toolregistry.h>
#include <utils/qtcassert.h>

#include <QSerialPortInfo>

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

McpUi::Call makeCall(const QJsonObject &args)
{
    McpUi::Call call;
    call.wall.start();

    const QString policy = args.value(QStringLiteral("policy")).toString();
    if(policy == QStringLiteral("auto")) call.policy = McpUi::Policy::AutoAnswer;
    else if(policy == QStringLiteral("fail")) call.policy = McpUi::Policy::Fail;
    else call.policy = McpUi::Policy::Elicit;

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
    return schema;
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

QJsonObject outcome(const McpUi::Call &call)
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
                McpUi::Call call = makeCall(args);
                call.tool = toolInterface;

                if(args.contains(QStringLiteral("port")))
                {
                    call.answers.insert(OMV_UI_SITE("connect.port"), args.value(QStringLiteral("port")).toString());
                }

                {
                    McpUi::Scope scope(call);
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

                toolInterface.finish(body());
                return Utils::ResultOk;
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

                McpUi::Call call = makeCall(params.argumentsAsObject());
                call.tool = toolInterface;

                {
                    McpUi::Scope scope(call);
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

                toolInterface.finish(body());
                return Utils::ResultOk;
            });
    }

    // ---- openmv_script_run --------------------------------------------------

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("text"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("description"), QStringLiteral("MicroPython source to run on the camera. Omitted: runs the script in the current editor, exactly like the Start button.")}});
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

                McpUi::Call call = makeCall(args);
                call.tool = toolInterface;
                m_lastScriptError = QString();

                if(args.contains(QStringLiteral("text")))
                {
                    McpUi::Scope scope(call);
                    m_iodevice->scriptExec(args.value(QStringLiteral("text")).toString().toUtf8());
                    bool done = awaitSignal(m_iodevice, &OpenMVPluginIO::scriptExecDone, call.remainingMs());
                    bool timedOut = m_iodevice->getTimeout();

                    QJsonObject out = outcome(call);
                    if(!done) return errResult(QStringLiteral("timeout"), QStringLiteral("The camera did not acknowledge the script."), out);
                    if(timedOut) return errResult(QStringLiteral("camera_timeout"), QStringLiteral("The camera timed out while receiving the script."), out);
                    return okResult(out);
                }

                {
                    McpUi::Scope scope(call);
                    startClicked();

                    while(m_working && !call.expired())
                    {
                        awaitSignal(this, &OpenMVPlugin::workingDone, call.remainingMs());
                    }
                }

                QJsonObject out = outcome(call);
                if(call.expired() && m_working) return errResult(QStringLiteral("timeout"), QStringLiteral("Start did not finish within the deadline."), out);
                if(call.hasErrors()) return errResult(QStringLiteral("failed"), QStringLiteral("Start reported an error; see errors."), out);
                return okResult(out);
                };

                toolInterface.finish(body());
                return Utils::ResultOk;
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

                McpUi::Call call = makeCall(params.argumentsAsObject());
                call.tool = toolInterface;

                {
                    McpUi::Scope scope(call);
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

                toolInterface.finish(body());
                return Utils::ResultOk;
            });
    }

    // ---- openmv_framebuffer_get ---------------------------------------------

    {
        Tool::InputSchema in;
        in.addProperty(QStringLiteral("format"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("string")},
            {QStringLiteral("enum"), QJsonArray{QStringLiteral("png"), QStringLiteral("jpeg")}},
            {QStringLiteral("description"), QStringLiteral("Encoding of the returned image (default png).")}});
        in.addProperty(QStringLiteral("quality"), QJsonObject{
            {QStringLiteral("type"), QStringLiteral("integer")},
            {QStringLiteral("description"), QStringLiteral("JPEG quality 1-100 (default 90).")}});

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_framebuffer_get"))
                .title(QStringLiteral("Get the camera frame buffer"))
                .description(QStringLiteral("Returns the most recent frame the IDE received from the camera, base64-encoded, with its dimensions. Fails if no frame has been received."))
                .annotations(ToolAnnotations{}.readOnlyHint(true))
                .inputSchema(in)
                .outputSchema(Tool::OutputSchema{}
                    .addProperty(QStringLiteral("width"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}})
                    .addProperty(QStringLiteral("height"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}})
                    .addProperty(QStringLiteral("mimeType"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})
                    .addProperty(QStringLiteral("base64"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})),
            [this](const CallToolRequestParams &params) -> Utils::Result<CallToolResult> {
                if(!m_frameBuffer || !m_frameBuffer->pixmapValid()) return errResult(QStringLiteral("not_available"), QStringLiteral("No frame has been received from the camera."));

                const QJsonObject args = params.argumentsAsObject();
                const bool jpeg = args.value(QStringLiteral("format")).toString() == QStringLiteral("jpeg");
                const int quality = qBound(1, args.value(QStringLiteral("quality")).toInt(90), 100);

                const QPixmap pixmap = m_frameBuffer->pixmap();
                QByteArray bytes;
                QBuffer buffer(&bytes);
                buffer.open(QIODevice::WriteOnly);
                pixmap.save(&buffer, jpeg ? "JPEG" : "PNG", jpeg ? quality : -1);

                return okResult(QJsonObject{
                    {QStringLiteral("width"), pixmap.width()},
                    {QStringLiteral("height"), pixmap.height()},
                    {QStringLiteral("mimeType"), jpeg ? QStringLiteral("image/jpeg") : QStringLiteral("image/png")},
                    {QStringLiteral("base64"), QString::fromLatin1(bytes.toBase64())}});
            });
    }

    // Runs a plugin flow inside the call's scope and waits until the plugin is
    // idle again or the deadline passes. Firmware flows tear m_working down and
    // back up between their stages, so the sticky flag is waited on as well.
    auto runFlow = [this](McpUi::Call &call, const std::function<void()> &invoke) {
        McpUi::Scope scope(call);
        invoke();

        while((m_working || m_firmwareUpdateInProgress) && !call.expired())
        {
            awaitSignal(this, &OpenMVPlugin::workingDone, qMin(call.remainingMs(), 250));
        }
    };

    auto flowBusy = [this](const McpUi::Call &call) {
        return call.expired() && (m_working || m_firmwareUpdateInProgress);
    };

    // G2 gate: the agent must pass confirm:true and, when the client can put a
    // question to the user, the user confirms too (whatever the call's policy
    // and whatever `answers` say).
    auto gateDestructive = [](McpUi::Call &call, const QJsonObject &args, const QString &title, const QString &text) -> std::optional<CallToolResult> {
        if(!args.value(QStringLiteral("confirm")).toBool())
        {
            return errResult(QStringLiteral("confirm_required"), QStringLiteral("This operation is destructive; pass confirm:true to run it. %1").arg(text));
        }

        if(!call.supportsElicitation())
        {
            call.note(QStringLiteral("[mcp.confirm] %1 -> Yes (confirm:true; the client cannot ask the user)").arg(title));
            return std::nullopt;
        }

        const McpUi::Policy policy = call.policy;
        call.policy = McpUi::Policy::Elicit;
        call.answers.remove(OMV_UI_SITE("mcp.confirm"));

        QMessageBox::StandardButton answer;
        {
            McpUi::Scope scope(call);
            answer = OmvUi::question(Core::ICore::dialogParent(), title, text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No, OMV_UI_SITE("mcp.confirm"));
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

                McpUi::Call call = makeCall(args);
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

                toolInterface.finish(body());
                return Utils::ResultOk;
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

                McpUi::Call call = makeCall(args);
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

                toolInterface.finish(body());
                return Utils::ResultOk;
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

                McpUi::Call call = makeCall(params.argumentsAsObject());
                call.tool = toolInterface;

                runFlow(call, [this] { disconnectClicked(true); });

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("connected"), m_connected);

                if(flowBusy(call)) return errResult(QStringLiteral("timeout"), QStringLiteral("The reset did not finish within the deadline."), out);
                if(call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("The reset reported an error; see errors."), out);
                return okResult(out);
                };

                toolInterface.finish(body());
                return Utils::ResultOk;
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

                McpUi::Call call = makeCall(params.argumentsAsObject());
                call.tool = toolInterface;

                runFlow(call, [this] { disconnectClicked(true, true); });

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("connected"), m_connected);

                if(flowBusy(call)) return errResult(QStringLiteral("timeout"), QStringLiteral("Entering the bootloader did not finish within the deadline."), out);
                if(call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("Entering the bootloader reported an error; see errors."), out);
                return okResult(out);
                };

                toolInterface.finish(body());
                return Utils::ResultOk;
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

                McpUi::Call call = makeCall(args);
                call.tool = toolInterface;
                call.answers.insert(OMV_UI_SITE("script.save_strip"), args.value(QStringLiteral("strip")).toBool() ? QStringLiteral("Yes") : QStringLiteral("No"));

                runFlow(call, [this] { saveScript(); });

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("path"), QString(m_portPath + QDir::separator() + QStringLiteral("main.py")));

                if(flowBusy(call)) return errResult(QStringLiteral("timeout"), QStringLiteral("Saving did not finish within the deadline."), out);
                if(call.hasErrors(2)) return errResult(QStringLiteral("failed"), QStringLiteral("Saving reported an error; see errors."), out);
                return okResult(out);
                };

                toolInterface.finish(body());
                return Utils::ResultOk;
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

                McpUi::Call call = makeCall(args);
                call.tool = toolInterface;
                if(args.contains(QStringLiteral("board"))) call.answers.insert(OMV_UI_SITE("connect.board_type"), args.value(QStringLiteral("board")).toString());
                call.answers.insert(OMV_UI_SITE("mpy.options"), QJsonObject{
                    {QStringLiteral("mode"), mode},
                    {QStringLiteral("advanced"), !options.isEmpty()},
                    {QStringLiteral("options"), options}});

                QString outputPath;
                bool noBoard = false;

                {
                    McpUi::Scope scope(call);
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
                return okResult(out);
                };

                toolInterface.finish(body());
                return Utils::ResultOk;
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

                McpUi::Call call = makeCall(args);
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
                    McpUi::Scope scope(call);
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
                return okResult(out);
                };

                toolInterface.finish(body());
                return Utils::ResultOk;
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

                McpUi::Call call = makeCall(args);
                call.tool = toolInterface;
                call.answers.insert(OMV_UI_SITE("fb.save_image_path"), path);

                {
                    McpUi::Scope scope(call);
                    saveImage(m_frameBuffer->pixmap());
                }

                QJsonObject out = outcome(call);
                out.insert(QStringLiteral("path"), path);
                if(call.hasErrors(1)) return errResult(QStringLiteral("failed"), QStringLiteral("Saving reported an error; see errors."), out);
                return okResult(out);
                };

                toolInterface.finish(body());
                return Utils::ResultOk;
            });
    }
}

} // namespace Internal
} // namespace OpenMV
