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

// Board Info view for the histogram pane's view selector: a fixed table of
// board/version/capability rows populated from the camera's cached SYS_INFO
// map (OpenMVPluginIO::getSystemInfo()) plus the plugin's board/sensor/port
// strings. Row set and order mirror OpenMV Studio's Board Info tab.

#ifndef OPENMVBOARDINFOVIEW_H
#define OPENMVBOARDINFOVIEW_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

namespace OpenMV {
namespace Internal {

// A two-page stack: a big centered status message (styled like the frame
// buffer's "No Image" text) or the info table.
class OpenMVBoardInfoView : public QStackedWidget
{
    Q_OBJECT

public:

    explicit OpenMVBoardInfoView(QWidget *parent = Q_NULLPTR);

    // System info is static per connection, so the poll loop stops asking
    // once the table is populated.
    bool hasData() const { return currentIndex() == 1; }

public slots:

    // An empty map shows the "not available" message; reset() shows the
    // "connect" message.
    void systemInfo(const QVariantMap &info, const QString &board,
                    const QString &boardId, const QString &sensor, const QString &port,
                    bool profilerAvailable);
    void reset();

private:

    void showMessage(const QString &message);

    QLabel *m_message;
    QList<QWidget *> m_rows;  // one per row, in row order
    QList<QLabel *> m_values; // one per row, in row order

    // Shows/hides a whole row: absent keys (e.g. on the V1 protocol) hide
    // their rows instead of reading "--".
    void setRow(int row, bool present, const QString &text = QString());
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVBOARDINFOVIEW_H
