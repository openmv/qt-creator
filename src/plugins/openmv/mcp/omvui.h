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

#ifndef OMVUI_H
#define OMVUI_H

#include <QtCore>
#include <QtWidgets>

#include <utils/checkablemessagebox.h>

// Drop-in replacements for the dialog primitives the OpenMV plugin uses.
//
// Every function keeps the Qt signature (same parameters, same defaults, same
// return type) plus one trailing, defaulted `site` id, so a call site migrates by
// changing only the namespace:
//
//     QMessageBox::critical(parent, title, text);   ->   OmvUi::critical(parent, title, text);
//
// With no MCP call in progress (McpUi::currentCall() == nullptr) each function
// forwards to Qt unchanged. Inside an MCP tool call it never blocks: it records
// what a human would have seen into the call's log/errors and returns the
// answer the agent pre-supplied for `site`, or the policy's safe default.

namespace OpenMV {
namespace Internal {
namespace OmvUi {

// --- QMessageBox statics ---------------------------------------------------

QMessageBox::StandardButton critical(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                     QMessageBox::StandardButton defaultButton = QMessageBox::NoButton,
                                     const QString &site = QString());

QMessageBox::StandardButton warning(QWidget *parent, const QString &title, const QString &text,
                                    QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton,
                                    const QString &site = QString());

QMessageBox::StandardButton information(QWidget *parent, const QString &title, const QString &text,
                                        QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                        QMessageBox::StandardButton defaultButton = QMessageBox::NoButton,
                                        const QString &site = QString());

QMessageBox::StandardButton question(QWidget *parent, const QString &title, const QString &text,
                                     QMessageBox::StandardButtons buttons = QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No),
                                     QMessageBox::StandardButton defaultButton = QMessageBox::NoButton,
                                     const QString &site = QString());

// The `QMessageBox box; box.setDetailedText(...); box.exec();` shape (59 sites):
// the detailed text is where tool stdout/stderr surfaces, so in MCP mode it
// becomes the structured error's detailedText.
QMessageBox::StandardButton detailedBox(QMessageBox::Icon icon, QWidget *parent, const QString &title,
                                        const QString &text, const QString &detailedText,
                                        QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                        QMessageBox::StandardButton defaultButton = QMessageBox::NoButton,
                                        const QString &site = QString());

// The `box.addButton(Tr::tr("..."), QMessageBox::AcceptRole)` shape: a box whose
// buttons are custom labels plus Cancel. Returns the index into `choices` of
// the button clicked, or -1 for Cancel/escape. MCP mode accepts the answer as
// an index or as the label text.
int choiceBox(QMessageBox::Icon icon, QWidget *parent, const QString &title, const QString &text,
              const QStringList &choices, int defaultIndex = 0, bool richText = false,
              const QString &site = QString());

// Deferred heap box (`QTimer::singleShot(0, box, &QMessageBox::exec)` sites,
// e.g. the serial traceback box): interactive shows it on the next loop
// iteration; MCP mode records it and never schedules it.
void asyncBox(QMessageBox::Icon icon, QWidget *parent, const QString &title, const QString &text,
              const QString &site = QString());

// The window flags every hand-built box in the plugin passes.
Qt::WindowFlags boxFlags();

// --- Utils::CheckableMessageBox ("don't ask again") ------------------------

QMessageBox::StandardButton questionCheckable(const QString &title, const QString &question,
                                              const Utils::CheckableDecider &decider,
                                              QMessageBox::StandardButtons buttons = QMessageBox::StandardButtons(QMessageBox::Yes | QMessageBox::No),
                                              QMessageBox::StandardButton defaultButton = QMessageBox::No,
                                              QMessageBox::StandardButton acceptButton = QMessageBox::Yes,
                                              QMap<QMessageBox::StandardButton, QString> buttonTextOverrides = {},
                                              const QString &msg = QString(),
                                              QMessageBox::StandardButton acceptButton2 = QMessageBox::NoButton,
                                              const QString &site = QString());

QMessageBox::StandardButton informationCheckable(const QString &title, const QString &text,
                                                 const Utils::CheckableDecider &decider,
                                                 QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                                 QMessageBox::StandardButton defaultButton = QMessageBox::Ok,
                                                 const QString &site = QString());

// --- QInputDialog statics ---------------------------------------------------

QString getText(QWidget *parent, const QString &title, const QString &label,
                QLineEdit::EchoMode echo = QLineEdit::Normal, const QString &text = QString(),
                bool *ok = Q_NULLPTR, Qt::WindowFlags flags = Qt::WindowFlags(),
                Qt::InputMethodHints inputMethodHints = Qt::ImhNone,
                const QString &site = QString());

QString getItem(QWidget *parent, const QString &title, const QString &label, const QStringList &items,
                int current = 0, bool editable = true, bool *ok = Q_NULLPTR,
                Qt::WindowFlags flags = Qt::WindowFlags(), Qt::InputMethodHints inputMethodHints = Qt::ImhNone,
                const QString &site = QString());

int getInt(QWidget *parent, const QString &title, const QString &label, int value = 0,
           int minValue = -2147483647, int maxValue = 2147483647, int step = 1,
           bool *ok = Q_NULLPTR, Qt::WindowFlags flags = Qt::WindowFlags(),
           const QString &site = QString());

// --- QFileDialog statics ----------------------------------------------------
// MCP mode never asks a human for a path: the agent must pre-supply it for the
// site. A save path without a suffix returns empty with an invalid_path error,
// which is what the "Please add a file extension!" loops already treat as
// "give up".

QString getOpenFileName(QWidget *parent = Q_NULLPTR, const QString &caption = QString(),
                        const QString &dir = QString(), const QString &filter = QString(),
                        QString *selectedFilter = Q_NULLPTR, QFileDialog::Options options = QFileDialog::Options(),
                        const QString &site = QString());

QStringList getOpenFileNames(QWidget *parent = Q_NULLPTR, const QString &caption = QString(),
                             const QString &dir = QString(), const QString &filter = QString(),
                             QString *selectedFilter = Q_NULLPTR, QFileDialog::Options options = QFileDialog::Options(),
                             const QString &site = QString());

QString getSaveFileName(QWidget *parent = Q_NULLPTR, const QString &caption = QString(),
                        const QString &dir = QString(), const QString &filter = QString(),
                        QString *selectedFilter = Q_NULLPTR, QFileDialog::Options options = QFileDialog::Options(),
                        const QString &site = QString());

QString getExistingDirectory(QWidget *parent = Q_NULLPTR, const QString &caption = QString(),
                             const QString &dir = QString(), QFileDialog::Options options = QFileDialog::ShowDirsOnly,
                             const QString &site = QString());

// --- QMenu popups -----------------------------------------------------------
// Context menus are only reachable by right-click; MCP mode returns nullptr.

QAction *popupMenu(QMenu &menu, const QPoint &pos);

// --- Hand-built modal dialogs (`dialog.exec()`) -----------------------------
// Interactive: runs the dialog. MCP mode: nobody can fill the form in, so it
// is rejected at once and a needs_input error names it by window title. A
// Progress dialog keeps its own MCP-aware exec (declared below).

class Progress;

int exec(QDialog &dialog, const QString &site = QString());
inline int exec(QDialog *dialog, const QString &site = QString()) { return exec(*dialog, site); }
int exec(Progress &dialog, const QString &site = QString());
inline int exec(Progress *dialog, const QString &site = QString()) { return exec(*dialog, site); }

// --- QProgressDialog --------------------------------------------------------
// A QProgressDialog that never shows while an MCP call is in progress. Sites keep
// their `connect(&dialog, &QProgressDialog::canceled, ...)` wiring and their
// setWindowModality/setCancelButton/show/exec/close calls unchanged. exec() in
// MCP mode waits (without a window) for the accept/reject the site wires up, or
// the call's deadline; wasCanceled() also reports the agent's task cancel.

class Progress : public QProgressDialog
{
    Q_OBJECT

public:

    explicit Progress(QWidget *parent = Q_NULLPTR, Qt::WindowFlags flags = Qt::WindowFlags());
    Progress(const QString &labelText, const QString &cancelButtonText, int minimum, int maximum,
             QWidget *parent = Q_NULLPTR, Qt::WindowFlags flags = Qt::WindowFlags());

    void setVisible(bool visible) override;
    int exec() override;

    bool wasCanceled() const;
    void setLabelText(const QString &text); // shadows the slot to also log it

private:

    QString m_site;
};

} // namespace OmvUi
} // namespace Internal
} // namespace OpenMV

#endif // OMVUI_H
