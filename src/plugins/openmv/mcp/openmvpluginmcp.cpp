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
#include "views/openmvpluginfb.h"
#include "mcp/omvmcpcontext.h"

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

QJsonObject outcome(const McpUi::Call &call)
{
    return call.toJson();
}

} // namespace

void OpenMVPlugin::registerMcpTools()
{
    using namespace Mcp::Schema;
    using Mcp::ToolRegistry;

    // ---- openmv_camera_status ---------------------------------------------

    ToolRegistry::registerTool(
        Tool{}
            .name(QStringLiteral("openmv_camera_status"))
            .title(QStringLiteral("OpenMV camera status"))
            .description(QStringLiteral("Whether an OpenMV camera is connected, on which port, whether the IDE is busy with a camera operation, and which protocol is in use."))
            .annotations(ToolAnnotations{}.readOnlyHint(true))
            .outputSchema(Tool::OutputSchema{}
                .addProperty(QStringLiteral("connected"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}})
                .addProperty(QStringLiteral("working"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}})
                .addProperty(QStringLiteral("port"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}})
                .addProperty(QStringLiteral("protocolV2"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}})
                .addRequired(QStringLiteral("connected")).addRequired(QStringLiteral("working"))),
        [this](const CallToolRequestParams &) -> Utils::Result<CallToolResult> {
            return okResult(QJsonObject{
                {QStringLiteral("connected"), m_connected},
                {QStringLiteral("working"), m_working},
                {QStringLiteral("port"), m_portName},
                {QStringLiteral("protocolV2"), m_iodevice ? m_iodevice->v2ProtocolEnabled() : false}});
        });

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
        addCallControls(in);

        ToolRegistry::registerTool(
            Tool{}
                .name(QStringLiteral("openmv_camera_connect"))
                .title(QStringLiteral("Connect to the OpenMV camera"))
                .description(QStringLiteral("Runs the IDE's Connect action and waits for it to finish. Any dialog the IDE would have shown (port picker, firmware prompts, errors) is returned in `errors` with its site id instead of blocking; supply `answers` keyed by site id to pre-answer them."))
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

                McpUi::Call call = makeCall(params.argumentsAsObject());
                call.tool = toolInterface;

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
}

} // namespace Internal
} // namespace OpenMV
