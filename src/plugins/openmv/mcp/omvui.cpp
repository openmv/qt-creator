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

#include "omvui.h"
#include "omvmcpcontext.h"

namespace OpenMV {
namespace Internal {
namespace OmvUi {

using McpUi::Call;
using McpUi::Error;
using McpUi::Policy;

// --- helpers ---------------------------------------------------------------

static QString siteOr(const QString &site, const QString &title, const QString &text)
{
    return site.isEmpty() ? McpUi::synthesizeSite(title, text) : site;
}

static QString buttonName(QMessageBox::StandardButton button)
{
    return QString::fromLatin1(QMetaEnum::fromType<QMessageBox::StandardButton>().valueToKey(button));
}

static QMessageBox::StandardButton buttonFromName(const QString &name)
{
    bool ok = false;
    int v = QMetaEnum::fromType<QMessageBox::StandardButton>().keyToValue(name.trimmed().toLatin1().constData(), &ok);
    return ok ? QMessageBox::StandardButton(v) : QMessageBox::NoButton;
}

static QStringList buttonNames(QMessageBox::StandardButtons buttons)
{
    QStringList names;

    for(int b = QMessageBox::Ok; b <= QMessageBox::RestoreDefaults; b <<= 1)
    {
        if(buttons & b) names.append(buttonName(QMessageBox::StandardButton(b)));
    }

    return names;
}

// The answer a modal would have produced if nobody clicked anything a human
// would consider "go ahead": Cancel, else No, else the dialog's own default,
// else Ok, else the first button offered.
static QMessageBox::StandardButton safeDefault(QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton)
{
    if(buttons & QMessageBox::Cancel) return QMessageBox::Cancel;
    if(buttons & QMessageBox::No) return QMessageBox::No;
    if((defaultButton != QMessageBox::NoButton) && (buttons & defaultButton)) return defaultButton;
    if(buttons & QMessageBox::Ok) return QMessageBox::Ok;

    for(int b = QMessageBox::Ok; b <= QMessageBox::RestoreDefaults; b <<= 1)
    {
        if(buttons & b) return QMessageBox::StandardButton(b);
    }

    return QMessageBox::NoButton;
}

static void record(Call *call, int level, const QString &code, const QString &site,
                   const QString &title, const QString &text, const QString &detailedText = QString())
{
    Error e;
    e.code = code;
    e.site = site;
    e.title = title;
    e.text = text;
    e.detailedText = detailedText;
    e.level = level;
    call->fail(e);
}

// Ask the agent's client to put the question to the user (MCP elicitation, form
// mode, one single-select enum of the box's buttons). Blocks on a nested event
// loop until the client answers or the call's deadline passes. Returns the
// chosen button, or NoButton when the user declined/cancelled, the client
// errored, or we timed out. The callback may fire after a timeout returned,
// so its state is shared and marked abandoned rather than stack-owned.
static QMessageBox::StandardButton elicitChoice(Call *call, const QString &site, const QString &title, const QString &text,
                                                QMessageBox::StandardButtons buttons)
{
    using namespace Mcp::Schema;

    const QStringList names = buttonNames(buttons);

    ElicitRequestFormParams form;
    form.message(title.isEmpty() ? text : (title + QStringLiteral("\n\n") + text));

    ElicitRequestFormParams::RequestedSchema schema;
    schema.addProperty(QStringLiteral("choice"),
                       PrimitiveSchemaDefinition(UntitledSingleSelectEnumSchema{}.enum_(names).title(title)));
    schema.addRequired(QStringLiteral("choice"));
    form.requestedSchema(schema);

    struct State {
        bool done = false;
        bool abandoned = false;
        QMessageBox::StandardButton chosen = QMessageBox::NoButton;
        QString outcome;
        QEventLoop loop;
    };

    auto state = std::make_shared<State>();

    call->tool->elicit(ElicitRequestParams(form), [state, buttons](const Utils::Result<ElicitResult> &r) {
        if(state->abandoned) return;
        state->done = true;

        if(!r.has_value())
        {
            state->outcome = QStringLiteral("elicitation failed: %1").arg(r.error());
        }
        else if(r->action() == ElicitResult::Action::accept)
        {
            if(r->content())
            {
                auto it = r->content()->constFind(QStringLiteral("choice"));

                if(it != r->content()->constEnd())
                {
                    if(const QString *s = std::get_if<QString>(&it.value())) state->chosen = buttonFromName(*s);
                }
            }

            state->outcome = (state->chosen != QMessageBox::NoButton) ? QStringLiteral("accepted") : QStringLiteral("accepted without a valid choice");
        }
        else if(r->action() == ElicitResult::Action::cancel)
        {
            state->chosen = (buttons & QMessageBox::Cancel) ? QMessageBox::Cancel : QMessageBox::NoButton;
            state->outcome = QStringLiteral("cancelled by user");
        }
        else
        {
            state->chosen = (buttons & QMessageBox::No) ? QMessageBox::No
                          : ((buttons & QMessageBox::Cancel) ? QMessageBox::Cancel : QMessageBox::NoButton);
            state->outcome = QStringLiteral("declined by user");
        }

        state->loop.quit();
    });

    if(!state->done)
    {
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &state->loop, &QEventLoop::quit);
        timer.start(qMax(1, call->remainingMs()));
        state->loop.exec();
    }

    if(!state->done)
    {
        state->abandoned = true;
        record(call, 1, QStringLiteral("elicitation_timeout"), site, title, text);
        return QMessageBox::NoButton;
    }

    call->note(QStringLiteral("[%1] %2 -> %3 (elicited: %4)").arg(site, title, buttonName(state->chosen), state->outcome));
    return state->chosen;
}

// Resolve a multi-choice box inside an MCP call: a pre-supplied answer wins;
// then, if the policy is Elicit and the client supports it, the user is asked
// through the client; otherwise the policy's safe default is chosen and, unless
// the policy is AutoAnswer, the agent is told which choices it could have
// supplied.
static QMessageBox::StandardButton resolveChoice(Call *call, const QString &site, const QString &title, const QString &text,
                                                 QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton,
                                                 const QString &detailedText = QString())
{
    if(std::optional<QJsonValue> a = call->answer(site))
    {
        QMessageBox::StandardButton b = buttonFromName(a->toString());

        if((b != QMessageBox::NoButton) && (buttons & b))
        {
            call->note(QStringLiteral("[%1] %2 -> %3 (answered)").arg(site, title, buttonName(b)));
            return b;
        }

        record(call, 1, QStringLiteral("bad_answer"), site, title,
               QStringLiteral("Answer \"%1\" is not one of: %2").arg(a->toString(), buttonNames(buttons).join(QStringLiteral(", "))), detailedText);
    }

    if((call->policy == Policy::Elicit) && call->supportsElicitation() && !call->expired())
    {
        QMessageBox::StandardButton e = elicitChoice(call, site, title, text, buttons);
        if((e != QMessageBox::NoButton) && (buttons & e)) return e;
    }

    QMessageBox::StandardButton b = safeDefault(buttons, defaultButton);

    if(call->policy == Policy::AutoAnswer)
    {
        call->note(QStringLiteral("[%1] %2 -> %3 (auto)").arg(site, title, buttonName(b)));
    }
    else
    {
        record(call, 1, QStringLiteral("needs_input"), site, title, text,
               (detailedText.isEmpty() ? QString() : detailedText + QStringLiteral("\n")) +
               QStringLiteral("choices: %1; chose: %2").arg(buttonNames(buttons).join(QStringLiteral(", ")), buttonName(b)));
    }

    return b;
}

static QMessageBox::StandardButton messageBox(QMessageBox::Icon icon, int level, const QString &code,
                                              QWidget *parent, const QString &title, const QString &text,
                                              QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton,
                                              const QString &site)
{
    Call *call = McpUi::currentCall();

    if(!call)
    {
        switch(icon)
        {
            case QMessageBox::Critical: return QMessageBox::critical(parent, title, text, buttons, defaultButton);
            case QMessageBox::Warning: return QMessageBox::warning(parent, title, text, buttons, defaultButton);
            case QMessageBox::Question: return QMessageBox::question(parent, title, text, buttons, defaultButton);
            default: return QMessageBox::information(parent, title, text, buttons, defaultButton);
        }
    }

    const QString s = siteOr(site, title, text);

    // A one-button box is pure notification: record it at its level and move on.
    if(buttonNames(buttons).size() <= 1)
    {
        record(call, level, code, s, title, text);
        return safeDefault(buttons, defaultButton);
    }

    // A multi-button box is a decision, whatever icon it wears.
    if(level >= 1) record(call, level, code, s, title, text);
    return resolveChoice(call, s, title, text, buttons, defaultButton);
}

// --- QMessageBox statics ---------------------------------------------------

QMessageBox::StandardButton critical(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton,
                                     const QString &site)
{
    return messageBox(QMessageBox::Critical, 2, QStringLiteral("critical"), parent, title, text, buttons, defaultButton, site);
}

QMessageBox::StandardButton warning(QWidget *parent, const QString &title, const QString &text,
                                    QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton,
                                    const QString &site)
{
    return messageBox(QMessageBox::Warning, 1, QStringLiteral("warning"), parent, title, text, buttons, defaultButton, site);
}

QMessageBox::StandardButton information(QWidget *parent, const QString &title, const QString &text,
                                        QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton,
                                        const QString &site)
{
    return messageBox(QMessageBox::Information, 0, QStringLiteral("information"), parent, title, text, buttons, defaultButton, site);
}

QMessageBox::StandardButton question(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton,
                                     const QString &site)
{
    Call *call = McpUi::currentCall();
    if(!call) return QMessageBox::question(parent, title, text, buttons, defaultButton);
    return resolveChoice(call, siteOr(site, title, text), title, text, buttons, defaultButton);
}

QMessageBox::StandardButton detailedBox(QMessageBox::Icon icon, QWidget *parent, const QString &title,
                                        const QString &text, const QString &detailedText,
                                        QMessageBox::StandardButtons buttons, QMessageBox::StandardButton defaultButton,
                                        const QString &site)
{
    Call *call = McpUi::currentCall();

    if(!call)
    {
        QMessageBox box(icon, title, text, buttons, parent);
        box.setDetailedText(detailedText);
        if(defaultButton != QMessageBox::NoButton) box.setDefaultButton(defaultButton);
        return QMessageBox::StandardButton(box.exec());
    }

    const QString s = siteOr(site, title, text);
    const int level = (icon == QMessageBox::Critical) ? 2 : ((icon == QMessageBox::Warning) ? 1 : 0);
    const QString code = (icon == QMessageBox::Critical) ? QStringLiteral("critical")
                       : ((icon == QMessageBox::Warning) ? QStringLiteral("warning") : QStringLiteral("information"));

    if(buttonNames(buttons).size() <= 1)
    {
        record(call, level, code, s, title, text, detailedText);
        return safeDefault(buttons, defaultButton);
    }

    if(level >= 1) record(call, level, code, s, title, text, detailedText);
    return resolveChoice(call, s, title, text, buttons, defaultButton, detailedText);
}

void asyncBox(QMessageBox::Icon icon, const QString &title, const QString &text, const QString &site)
{
    Call *call = McpUi::currentCall();

    if(!call)
    {
        QMessageBox *box = new QMessageBox(icon, title, text, QMessageBox::Ok);
        box->setAttribute(Qt::WA_DeleteOnClose);
        QTimer::singleShot(0, box, qOverload<>(&QMessageBox::exec));
        return;
    }

    const int level = (icon == QMessageBox::Critical) ? 2 : ((icon == QMessageBox::Warning) ? 1 : 0);
    record(call, level, QStringLiteral("async_box"), siteOr(site, title, text), title, text);
}

// --- Utils::CheckableMessageBox --------------------------------------------

QMessageBox::StandardButton questionCheckable(const QString &title, const QString &question,
                                              const Utils::CheckableDecider &decider,
                                              QMessageBox::StandardButtons buttons,
                                              QMessageBox::StandardButton defaultButton,
                                              QMessageBox::StandardButton acceptButton,
                                              QMap<QMessageBox::StandardButton, QString> buttonTextOverrides,
                                              const QString &msg,
                                              QMessageBox::StandardButton acceptButton2,
                                              const QString &site)
{
    Call *call = McpUi::currentCall();

    if(!call)
    {
        return Utils::CheckableMessageBox::question(title, question, decider, buttons, defaultButton, acceptButton,
                                                    buttonTextOverrides, msg, acceptButton2);
    }

    // The "don't ask again" decision is a human preference: never persisted
    // from an agent's answer.
    return resolveChoice(call, siteOr(site, title, question), title, question, buttons, defaultButton);
}

QMessageBox::StandardButton informationCheckable(const QString &title, const QString &text,
                                                 const Utils::CheckableDecider &decider,
                                                 QMessageBox::StandardButtons buttons,
                                                 QMessageBox::StandardButton defaultButton,
                                                 const QString &site)
{
    Call *call = McpUi::currentCall();
    if(!call) return Utils::CheckableMessageBox::information(title, text, decider, buttons, defaultButton);

    record(call, 0, QStringLiteral("information"), siteOr(site, title, text), title, text);
    return safeDefault(buttons, defaultButton);
}

// --- QInputDialog statics ---------------------------------------------------

QString getText(QWidget *parent, const QString &title, const QString &label,
                QLineEdit::EchoMode echo, const QString &text, bool *ok, Qt::WindowFlags flags,
                Qt::InputMethodHints inputMethodHints, const QString &site)
{
    Call *call = McpUi::currentCall();
    if(!call) return QInputDialog::getText(parent, title, label, echo, text, ok, flags, inputMethodHints);

    const QString s = siteOr(site, title, label);

    if(std::optional<QJsonValue> a = call->answer(s))
    {
        if(ok) *ok = true;
        call->note(QStringLiteral("[%1] %2 -> (answered)").arg(s, title));
        return a->toString();
    }

    if(ok) *ok = false;
    record(call, 1, QStringLiteral("needs_input"), s, title, label, QStringLiteral("type: text; default: %1").arg(text));
    return QString();
}

QString getItem(QWidget *parent, const QString &title, const QString &label, const QStringList &items,
                int current, bool editable, bool *ok, Qt::WindowFlags flags, Qt::InputMethodHints inputMethodHints,
                const QString &site)
{
    Call *call = McpUi::currentCall();
    if(!call) return QInputDialog::getItem(parent, title, label, items, current, editable, ok, flags, inputMethodHints);

    const QString s = siteOr(site, title, label);

    if(std::optional<QJsonValue> a = call->answer(s))
    {
        if(a->isDouble())
        {
            int i = a->toInt(-1);

            if((i >= 0) && (i < items.size()))
            {
                if(ok) *ok = true;
                call->note(QStringLiteral("[%1] %2 -> %3 (answered)").arg(s, title, items.at(i)));
                return items.at(i);
            }
        }
        else
        {
            const QString v = a->toString();

            if(items.contains(v) || editable)
            {
                if(ok) *ok = true;
                call->note(QStringLiteral("[%1] %2 -> %3 (answered)").arg(s, title, v));
                return v;
            }
        }

        record(call, 1, QStringLiteral("bad_answer"), s, title,
               QStringLiteral("Answer is not one of: %1").arg(items.join(QStringLiteral(", "))));
    }

    // Mirrors the pickers that skip the dialog when there is exactly one choice.
    if(items.size() == 1)
    {
        if(ok) *ok = true;
        call->note(QStringLiteral("[%1] %2 -> %3 (only choice)").arg(s, title, items.first()));
        return items.first();
    }

    if(ok) *ok = false;
    record(call, 1, QStringLiteral("needs_input"), s, title, label,
           QStringLiteral("type: item; choices: %1").arg(items.join(QStringLiteral(", "))));
    return QString();
}

int getInt(QWidget *parent, const QString &title, const QString &label, int value,
           int minValue, int maxValue, int step, bool *ok, Qt::WindowFlags flags, const QString &site)
{
    Call *call = McpUi::currentCall();
    if(!call) return QInputDialog::getInt(parent, title, label, value, minValue, maxValue, step, ok, flags);

    const QString s = siteOr(site, title, label);

    if(std::optional<QJsonValue> a = call->answer(s))
    {
        if(a->isDouble())
        {
            if(ok) *ok = true;
            int v = qBound(minValue, a->toInt(value), maxValue);
            call->note(QStringLiteral("[%1] %2 -> %3 (answered)").arg(s, title).arg(v));
            return v;
        }

        record(call, 1, QStringLiteral("bad_answer"), s, title, QStringLiteral("Answer is not an integer"));
    }

    if(ok) *ok = false;
    record(call, 1, QStringLiteral("needs_input"), s, title, label,
           QStringLiteral("type: int; min: %1; max: %2; default: %3").arg(minValue).arg(maxValue).arg(value));
    return value;
}

// --- QFileDialog statics ----------------------------------------------------

static QString answeredPath(Call *call, const QString &s, const QString &caption, const QString &kind, bool requireSuffix)
{
    if(std::optional<QJsonValue> a = call->answer(s))
    {
        const QString path = a->toString();

        if(requireSuffix && QFileInfo(path).completeSuffix().isEmpty())
        {
            record(call, 1, QStringLiteral("invalid_path"), s, caption, QStringLiteral("Please add a file extension!"));
            return QString();
        }

        call->note(QStringLiteral("[%1] %2 -> %3 (answered)").arg(s, caption, path));
        return path;
    }

    record(call, 1, QStringLiteral("needs_input"), s, caption, QStringLiteral("A path is required"), QStringLiteral("type: %1").arg(kind));
    return QString();
}

QString getOpenFileName(QWidget *parent, const QString &caption, const QString &dir, const QString &filter,
                        QString *selectedFilter, QFileDialog::Options options, const QString &site)
{
    Call *call = McpUi::currentCall();
    if(!call) return QFileDialog::getOpenFileName(parent, caption, dir, filter, selectedFilter, options);
    return answeredPath(call, siteOr(site, caption, filter), caption, QStringLiteral("open-file"), false);
}

QStringList getOpenFileNames(QWidget *parent, const QString &caption, const QString &dir, const QString &filter,
                             QString *selectedFilter, QFileDialog::Options options, const QString &site)
{
    Call *call = McpUi::currentCall();
    if(!call) return QFileDialog::getOpenFileNames(parent, caption, dir, filter, selectedFilter, options);

    const QString s = siteOr(site, caption, filter);

    if(std::optional<QJsonValue> a = call->answer(s))
    {
        QStringList paths;

        if(a->isArray())
        {
            for(const QJsonValue &v : a->toArray()) paths.append(v.toString());
        }
        else
        {
            paths.append(a->toString());
        }

        call->note(QStringLiteral("[%1] %2 -> %3 (answered)").arg(s, caption, paths.join(QStringLiteral(", "))));
        return paths;
    }

    record(call, 1, QStringLiteral("needs_input"), s, caption, QStringLiteral("One or more paths are required"), QStringLiteral("type: open-files"));
    return QStringList();
}

QString getSaveFileName(QWidget *parent, const QString &caption, const QString &dir, const QString &filter,
                        QString *selectedFilter, QFileDialog::Options options, const QString &site)
{
    Call *call = McpUi::currentCall();
    if(!call) return QFileDialog::getSaveFileName(parent, caption, dir, filter, selectedFilter, options);
    return answeredPath(call, siteOr(site, caption, filter), caption, QStringLiteral("save-file"), true);
}

QString getExistingDirectory(QWidget *parent, const QString &caption, const QString &dir,
                             QFileDialog::Options options, const QString &site)
{
    Call *call = McpUi::currentCall();
    if(!call) return QFileDialog::getExistingDirectory(parent, caption, dir, options);
    return answeredPath(call, siteOr(site, caption, QStringLiteral("dir")), caption, QStringLiteral("directory"), false);
}

// --- QMenu popups -----------------------------------------------------------

QAction *popupMenu(QMenu &menu, const QPoint &pos)
{
    if(McpUi::currentCall()) return Q_NULLPTR;
    return menu.exec(pos);
}

// --- Progress ---------------------------------------------------------------

Progress::Progress(QWidget *parent, Qt::WindowFlags flags) : QProgressDialog(parent, flags),
    m_site(McpUi::synthesizeSite(QStringLiteral("progress"), QString()))
{
}

Progress::Progress(const QString &labelText, const QString &cancelButtonText, int minimum, int maximum,
                   QWidget *parent, Qt::WindowFlags flags) : QProgressDialog(labelText, cancelButtonText, minimum, maximum, parent, flags),
    m_site(McpUi::synthesizeSite(QStringLiteral("progress"), labelText))
{
    if(Call *call = McpUi::currentCall()) call->note(QStringLiteral("[%1] %2").arg(m_site, labelText));
}

void Progress::setVisible(bool visible)
{
    // Never show a window while an agent drives the IDE; everything the window
    // would have said is in the call's log.
    if(McpUi::currentCall()) visible = false;
    QProgressDialog::setVisible(visible);
}

int Progress::exec()
{
    Call *call = McpUi::currentCall();
    if(!call) return QProgressDialog::exec();

    // Sites wire `finished -> accept` / `canceled -> reject` themselves; wait for
    // that without a window, bounded by the call's wall clock.
    QEventLoop loop;
    connect(this, &QDialog::finished, &loop, &QEventLoop::quit);

    QTimer deadline;
    deadline.setSingleShot(true);
    connect(&deadline, &QTimer::timeout, this, [this, call] {
        call->cancelled = true;
        McpUi::Error e;
        e.code = QStringLiteral("timeout");
        e.site = m_site;
        e.title = windowTitle();
        e.text = labelText();
        e.level = 2;
        call->fail(e);
        reject();
    });
    deadline.start(qMax(1, call->remainingMs()));

    loop.exec();
    return result();
}

bool Progress::wasCanceled() const
{
    Call *call = McpUi::currentCall();
    return QProgressDialog::wasCanceled() || (call && call->cancelled);
}

void Progress::setLabelText(const QString &text)
{
    QProgressDialog::setLabelText(text);
    if(Call *call = McpUi::currentCall()) call->note(QStringLiteral("[%1] %2").arg(m_site, text));
}

} // namespace OmvUi
} // namespace Internal
} // namespace OpenMV
