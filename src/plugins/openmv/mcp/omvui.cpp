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

#include <utils/filepath.h>
#include <utils/hostosinfo.h>
#include <utils/pathchooser.h>

#include <algorithm>

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

// Ask the agent's client to put a question to the user (MCP elicitation, form
// mode, one single-select enum of `names`). Blocks on a nested event loop
// until the client answers or the call's deadline passes. On Accepted,
// `*chosen` is one of `names`. The callback may fire after a timeout returned,
// so its state is shared and marked abandoned rather than stack-owned.
enum class Elicited { Accepted, Cancelled, Declined, Failed, Timeout };

static Elicited elicitName(Call *call, const QString &site, const QString &title, const QString &text,
                           const QStringList &names, QString *chosen)
{
    using namespace Mcp::Schema;

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
        Elicited result = Elicited::Failed;
        QString chosen;
        QString outcome;
        QEventLoop loop;
    };

    auto state = std::make_shared<State>();

    call->tool->elicit(ElicitRequestParams(form), [state, names](const Utils::Result<ElicitResult> &r) {
        if(state->abandoned) return;
        state->done = true;

        if(!r.has_value())
        {
            state->result = Elicited::Failed;
            state->outcome = QStringLiteral("elicitation failed: %1").arg(r.error());
        }
        else if(r->action() == ElicitResult::Action::accept)
        {
            if(r->content())
            {
                auto it = r->content()->constFind(QStringLiteral("choice"));

                if(it != r->content()->constEnd())
                {
                    if(const QString *s = std::get_if<QString>(&it.value()))
                    {
                        if(names.contains(s->trimmed())) state->chosen = s->trimmed();
                    }
                }
            }

            state->result = state->chosen.isEmpty() ? Elicited::Failed : Elicited::Accepted;
            state->outcome = state->chosen.isEmpty() ? QStringLiteral("accepted without a valid choice") : QStringLiteral("accepted");
        }
        else if(r->action() == ElicitResult::Action::cancel)
        {
            state->result = Elicited::Cancelled;
            state->outcome = QStringLiteral("cancelled by user");
        }
        else
        {
            state->result = Elicited::Declined;
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
        return Elicited::Timeout;
    }

    call->note(QStringLiteral("[%1] %2 -> %3 (elicited: %4)").arg(site, title, state->chosen, state->outcome));
    *chosen = state->chosen;
    return state->result;
}

// elicitName() over a box's standard buttons; cancel/decline map onto the
// button a human would have used for them when it exists.
static QMessageBox::StandardButton elicitChoice(Call *call, const QString &site, const QString &title, const QString &text,
                                                QMessageBox::StandardButtons buttons)
{
    QString chosen;

    switch(elicitName(call, site, title, text, buttonNames(buttons), &chosen))
    {
        case Elicited::Accepted: return buttonFromName(chosen);
        case Elicited::Cancelled: return (buttons & QMessageBox::Cancel) ? QMessageBox::Cancel : QMessageBox::NoButton;
        case Elicited::Declined: return (buttons & QMessageBox::No) ? QMessageBox::No
                                      : ((buttons & QMessageBox::Cancel) ? QMessageBox::Cancel : QMessageBox::NoButton);
        default: return QMessageBox::NoButton;
    }
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
        QMessageBox box(icon, title, text, buttons, parent, boxFlags());
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

int choiceBox(QMessageBox::Icon icon, QWidget *parent, const QString &title, const QString &text,
              const QStringList &choices, int defaultIndex, bool richText, const QString &site)
{
    Call *call = McpUi::currentCall();

    if(!call)
    {
        QMessageBox box(icon, title, text, QMessageBox::Cancel, parent, boxFlags());
        if(richText) box.setTextFormat(Qt::RichText);

        QList<QPushButton *> buttons;
        for(const QString &choice : choices) buttons.append(box.addButton(choice, QMessageBox::AcceptRole));

        if((defaultIndex >= 0) && (defaultIndex < buttons.size())) box.setDefaultButton(buttons.at(defaultIndex));
        box.setEscapeButton(QMessageBox::Cancel);
        box.exec();

        return buttons.indexOf(qobject_cast<QPushButton *>(box.clickedButton()));
    }

    const QString s = siteOr(site, title, text);
    const int level = (icon == QMessageBox::Critical) ? 2 : ((icon == QMessageBox::Warning) ? 1 : 0);
    if(level >= 1) record(call, level, QStringLiteral("choice"), s, title, text);

    // Labels are shown trimmed: the sites pad them with spaces for width.
    QStringList names;
    for(const QString &choice : choices) names.append(choice.trimmed());

    if(std::optional<QJsonValue> a = call->answer(s))
    {
        int i = a->isDouble() ? a->toInt(-1) : names.indexOf(a->toString().trimmed());

        if((i >= 0) && (i < names.size()))
        {
            call->note(QStringLiteral("[%1] %2 -> %3 (answered)").arg(s, title, names.at(i)));
            return i;
        }

        if(a->toString().trimmed().compare(QStringLiteral("Cancel"), Qt::CaseInsensitive) == 0)
        {
            call->note(QStringLiteral("[%1] %2 -> Cancel (answered)").arg(s, title));
            return -1;
        }

        record(call, 1, QStringLiteral("bad_answer"), s, title,
               QStringLiteral("Answer \"%1\" is not one of: %2, Cancel").arg(a->toString(), names.join(QStringLiteral(", "))));
    }

    if((call->policy == Policy::Elicit) && call->supportsElicitation() && !call->expired())
    {
        QString chosen;

        switch(elicitName(call, s, title, text, QStringList(names) << QStringLiteral("Cancel"), &chosen))
        {
            case Elicited::Accepted: return names.indexOf(chosen); // -1 for "Cancel"
            case Elicited::Cancelled:
            case Elicited::Declined: return -1;
            default: break;
        }
    }

    if(call->policy == Policy::AutoAnswer)
    {
        call->note(QStringLiteral("[%1] %2 -> Cancel (auto)").arg(s, title));
    }
    else
    {
        record(call, 1, QStringLiteral("needs_input"), s, title, text,
               QStringLiteral("choices: %1, Cancel; chose: Cancel").arg(names.join(QStringLiteral(", "))));
    }

    return -1;
}

void asyncBox(QMessageBox::Icon icon, QWidget *parent, const QString &title, const QString &text, const QString &site)
{
    Call *call = McpUi::currentCall();

    if(!call)
    {
        QMessageBox *box = new QMessageBox(icon, title, text, QMessageBox::Ok, parent);
        QObject::connect(box, &QMessageBox::finished, box, &QMessageBox::deleteLater);
        QTimer::singleShot(0, box, &QMessageBox::exec);
        return;
    }

    const int level = (icon == QMessageBox::Critical) ? 2 : ((icon == QMessageBox::Warning) ? 1 : 0);
    record(call, level, QStringLiteral("async_box"), siteOr(site, title, text), title, text);
}

Qt::WindowFlags boxFlags()
{
    return Qt::MSWindowsFixedSizeDialogHint | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
           (Utils::HostOsInfo::isMacHost() ? Qt::WindowType(0) : Qt::WindowCloseButtonHint);
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

// --- Hand-built modal dialogs -----------------------------------------------
// A dialog's inputs are discovered by walking its widgets, so an agent can be
// told what a form asks for and fill it in without the dialog being rewritten.
// A field is named by its objectName, else the QFormLayout label beside it,
// else (check/radio/group boxes) its own text.

namespace {

struct Field {
    QString name;
    QString type;
    QJsonValue value;
    QStringList choices;
    QWidget *widget = Q_NULLPTR;
};

QString cleanLabel(QString text)
{
    text.remove(QLatin1Char('&'));
    text = text.trimmed();
    if(text.endsWith(QLatin1Char(':'))) text.chop(1);
    return text.trimmed();
}

QString labelFor(QWidget *widget, const QList<QFormLayout *> &forms)
{
    // The label may sit beside the widget itself or beside a container row
    // the widget lives in; failing that, the plugin's forms put a spanning
    // QLabel on the row above ("What do you want to do?").
    for(QWidget *w = widget; w; w = w->parentWidget())
    {
        for(QFormLayout *form : forms)
        {
            if(QLabel *label = qobject_cast<QLabel *>(form->labelForField(w)))
            {
                return cleanLabel(label->text());
            }

            int row = -1;
            QFormLayout::ItemRole role;
            form->getWidgetPosition(w, &row, &role);

            for(int r = row - 1; r >= 0; r--)
            {
                QLayoutItem *item = form->itemAt(r, QFormLayout::SpanningRole);
                QLabel *label = item ? qobject_cast<QLabel *>(item->widget()) : Q_NULLPTR;
                if(label && !label->text().isEmpty()) return cleanLabel(label->text().remove(QRegularExpression(QStringLiteral("<[^>]*>"))));
                if(item && item->widget()) break; // another input's row: stop
            }
        }

        if(qobject_cast<QDialog *>(w)) break;
    }

    return QString();
}

bool insidePathChooser(QWidget *widget)
{
    for(QWidget *w = widget->parentWidget(); w; w = w->parentWidget())
    {
        if(qobject_cast<Utils::PathChooser *>(w)) return true;
    }

    return false;
}

QList<Field> formFields(QDialog &dialog)
{
    const QList<QFormLayout *> forms = dialog.findChildren<QFormLayout *>();
    QList<Field> fields;

    const QList<QWidget *> widgets = dialog.findChildren<QWidget *>();

    for(QWidget *w : widgets)
    {
        if(insidePathChooser(w)) continue;

        Field f;
        f.widget = w;

        if(Utils::PathChooser *p = qobject_cast<Utils::PathChooser *>(w))
        {
            f.type = QStringLiteral("path");
            f.value = p->filePath().toUserOutput();
        }
        else if(QCheckBox *c = qobject_cast<QCheckBox *>(w))
        {
            f.type = QStringLiteral("boolean");
            f.value = c->isChecked();
            f.name = cleanLabel(c->text());
        }
        else if(QRadioButton *r = qobject_cast<QRadioButton *>(w))
        {
            f.type = QStringLiteral("boolean");
            f.value = r->isChecked();
            f.name = cleanLabel(r->text());
        }
        else if(QGroupBox *g = qobject_cast<QGroupBox *>(w))
        {
            if(!g->isCheckable()) continue;
            f.type = QStringLiteral("boolean");
            f.value = g->isChecked();
            f.name = cleanLabel(g->title());
        }
        else if(QComboBox *c = qobject_cast<QComboBox *>(w))
        {
            f.type = QStringLiteral("choice");
            f.value = c->currentText();
            for(int i = 0; i < c->count(); i++) f.choices.append(c->itemText(i));
        }
        else if(QSpinBox *s = qobject_cast<QSpinBox *>(w))
        {
            f.type = QStringLiteral("integer");
            f.value = s->value();
            f.choices = QStringList{QString::number(s->minimum()), QString::number(s->maximum())};
        }
        else if(QSlider *s = qobject_cast<QSlider *>(w))
        {
            f.type = QStringLiteral("integer");
            f.value = s->value();
            f.choices = QStringList{QString::number(s->minimum()), QString::number(s->maximum())};
        }
        else if(QDoubleSpinBox *s = qobject_cast<QDoubleSpinBox *>(w))
        {
            f.type = QStringLiteral("number");
            f.value = s->value();
            f.choices = QStringList{QString::number(s->minimum()), QString::number(s->maximum())};
        }
        else if(QLineEdit *e = qobject_cast<QLineEdit *>(w))
        {
            if(e->isReadOnly() || (e->parentWidget() && (qobject_cast<QComboBox *>(e->parentWidget()) || qobject_cast<QAbstractSpinBox *>(e->parentWidget())))) continue;
            f.type = (e->echoMode() == QLineEdit::Normal) ? QStringLiteral("string") : QStringLiteral("secret");
            f.value = e->text();
        }
        else if(QPlainTextEdit *e = qobject_cast<QPlainTextEdit *>(w))
        {
            if(e->isReadOnly()) continue;
            f.type = QStringLiteral("text");
            f.value = e->toPlainText();
        }
        else if(QTextEdit *e = qobject_cast<QTextEdit *>(w))
        {
            if(e->isReadOnly()) continue;
            f.type = QStringLiteral("text");
            f.value = e->toPlainText();
        }
        else
        {
            continue;
        }

        if(!w->objectName().isEmpty() && !w->objectName().startsWith(QStringLiteral("qt_"))) f.name = w->objectName();
        if(f.name.isEmpty()) f.name = labelFor(w, forms);
        if(f.name.isEmpty()) f.name = cleanLabel(w->toolTip());
        if(f.name.isEmpty()) f.name = QStringLiteral("%1 %2").arg(f.type).arg(fields.size() + 1);

        // Two fields may share a label (e.g. twin check boxes): number the later ones.
        const QString base = f.name;
        for(int n = 2; std::any_of(fields.cbegin(), fields.cend(), [&f](const Field &o) { return o.name.compare(f.name, Qt::CaseInsensitive) == 0; }); n++)
        {
            f.name = QStringLiteral("%1 #%2").arg(base).arg(n);
        }

        fields.append(f);
    }

    return fields;
}

QJsonArray describeFields(const QList<Field> &fields)
{
    QJsonArray a;

    for(const Field &f : fields)
    {
        QJsonObject o{{QStringLiteral("name"), f.name}, {QStringLiteral("type"), f.type}, {QStringLiteral("value"), f.value}};
        if(f.type == QStringLiteral("choice")) o.insert(QStringLiteral("choices"), QJsonArray::fromStringList(f.choices));
        else if(f.choices.size() == 2) { o.insert(QStringLiteral("min"), f.choices.at(0).toDouble()); o.insert(QStringLiteral("max"), f.choices.at(1).toDouble()); }
        if(!f.widget->isEnabled()) o.insert(QStringLiteral("enabled"), false);
        a.append(o);
    }

    return a;
}

bool applyField(const Field &f, const QJsonValue &v)
{
    QWidget *w = f.widget;

    if(Utils::PathChooser *p = qobject_cast<Utils::PathChooser *>(w)) { p->setFilePath(Utils::FilePath::fromUserInput(v.toString())); return true; }
    if(QCheckBox *c = qobject_cast<QCheckBox *>(w)) { c->setChecked(v.toBool(c->isChecked())); return true; }
    if(QRadioButton *r = qobject_cast<QRadioButton *>(w)) { r->setChecked(v.toBool(r->isChecked())); return true; }
    if(QGroupBox *g = qobject_cast<QGroupBox *>(w)) { g->setChecked(v.toBool(g->isChecked())); return true; }

    if(QComboBox *c = qobject_cast<QComboBox *>(w))
    {
        int i = v.isDouble() ? v.toInt(-1) : c->findText(v.toString(), Qt::MatchFixedString);
        if((i < 0) || (i >= c->count())) return false;
        c->setCurrentIndex(i);
        return true;
    }

    if(QSpinBox *s = qobject_cast<QSpinBox *>(w)) { if(!v.isDouble()) return false; s->setValue(v.toInt()); return true; }
    if(QSlider *s = qobject_cast<QSlider *>(w)) { if(!v.isDouble()) return false; s->setValue(v.toInt()); return true; }
    if(QDoubleSpinBox *s = qobject_cast<QDoubleSpinBox *>(w)) { if(!v.isDouble()) return false; s->setValue(v.toDouble()); return true; }
    if(QLineEdit *e = qobject_cast<QLineEdit *>(w)) { e->setText(v.toString()); return true; }
    if(QPlainTextEdit *e = qobject_cast<QPlainTextEdit *>(w)) { e->setPlainText(v.toString()); return true; }
    if(QTextEdit *e = qobject_cast<QTextEdit *>(w)) { e->setPlainText(v.toString()); return true; }
    return false;
}

} // namespace

int exec(QDialog &dialog, const QString &site)
{
    Call *call = McpUi::currentCall();
    if(!call) return dialog.exec();

    const QString title = dialog.windowTitle();
    const QString s = siteOr(site, title, QStringLiteral("dialog"));
    const QList<Field> fields = formFields(dialog);
    const QString described = QString::fromUtf8(QJsonDocument(describeFields(fields)).toJson(QJsonDocument::Compact));

    if(std::optional<QJsonValue> a = call->answer(s))
    {
        // `false` rejects, `true` accepts the form as it is, an object fills
        // fields in by name and accepts.
        if(a->isBool())
        {
            call->note(QStringLiteral("[%1] %2 -> %3 (answered)").arg(s, title, a->toBool() ? QStringLiteral("accept") : QStringLiteral("reject")));
            if(a->toBool()) dialog.accept(); else dialog.reject();
            return a->toBool() ? QDialog::Accepted : QDialog::Rejected;
        }

        if(a->isObject())
        {
            const QJsonObject answers = a->toObject();
            QStringList unknown, invalid;

            for(auto it = answers.constBegin(); it != answers.constEnd(); ++it)
            {
                auto field = std::find_if(fields.cbegin(), fields.cend(), [&it](const Field &f) { return f.name.compare(it.key().trimmed(), Qt::CaseInsensitive) == 0; });
                if(field == fields.cend()) { unknown.append(it.key()); continue; }
                if(!applyField(*field, it.value())) invalid.append(it.key());
            }

            if(unknown.isEmpty() && invalid.isEmpty())
            {
                call->note(QStringLiteral("[%1] %2 -> accept (answered: %3)").arg(s, title, answers.keys().join(QStringLiteral(", "))));
                dialog.accept();
                return QDialog::Accepted;
            }

            record(call, 1, QStringLiteral("bad_answer"), s, title,
                   QStringLiteral("%1%2").arg(unknown.isEmpty() ? QString() : QStringLiteral("Unknown fields: %1. ").arg(unknown.join(QStringLiteral(", "))),
                                             invalid.isEmpty() ? QString() : QStringLiteral("Invalid values for: %1. ").arg(invalid.join(QStringLiteral(", ")))).trimmed(),
                   QStringLiteral("fields: ") + described);
            dialog.reject();
            return QDialog::Rejected;
        }

        record(call, 1, QStringLiteral("bad_answer"), s, title, QStringLiteral("A dialog answer must be true, false or an object of field values."), QStringLiteral("fields: ") + described);
        dialog.reject();
        return QDialog::Rejected;
    }

    record(call, 1, QStringLiteral("needs_input"), s, title,
           QStringLiteral("This step shows a form; answer the site with true (accept as is), false (cancel) or an object of field values."),
           QStringLiteral("fields: ") + described);
    dialog.reject();
    return QDialog::Rejected;
}

int exec(Progress &dialog, const QString &site)
{
    Q_UNUSED(site)
    return dialog.exec();
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
