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

// Shared look for the histogram pane's table views (Board Info, Statistics),
// mirroring OpenMV Studio's right-panel tables: muted name labels against
// brighter monospace values, a hairline under each row, and small bold
// section labels. Everything is mouse-selectable.

#ifndef OPENMVVIEWSTYLE_H
#define OPENMVVIEWSTYLE_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

namespace OpenMV {
namespace Internal {

// Paint a pane view on Base (white in light themes) like the histogram's
// plots, with the theme's normal text color for every label.
void viewApplyBackground(QWidget *view);

// Bold section label with a hairline underneath.
QWidget *viewSectionLabel(const QString &text);

// Muted (tertiary) selectable label for row names and secondary values.
QLabel *viewNameLabel(const QString &text);

// Bright monospace right-aligned selectable label for row values ("--" until
// data arrives).
QLabel *viewValueLabel();

// A row: muted name, stretch, then the value widget(s), with a hairline
// underneath. Hiding the returned row hides its hairline too.
QWidget *viewRow(const QString &name, const QList<QWidget *> &values);
QWidget *viewRow(const QString &name, QWidget *value);
QWidget *viewRow(QWidget *name, const QList<QWidget *> &values);

// Toggles a row's hairline (e.g. off for the last visible row of a table).
void viewRowSetLineVisible(QWidget *row, bool visible);

// The frame buffer's "No Image" markup for the big centered status message.
QString viewMessageHtml(const QString &message);

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVVIEWSTYLE_H
