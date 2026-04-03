/* Copyright (C) 2023-2024 OpenMV, LLC.
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

#ifndef OPENMVTERMINAL_H
#define OPENMVTERMINAL_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>
#include <QtNetwork>

#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>

#include <aggregation/aggregate.h>
#include <extensionsystem/pluginmanager.h>
#include <utils/ansiescapecodehandler.h>
#include <utils/elidinglabel.h>
#include <utils/fadingindicator.h>
#include <utils/styledbar.h>
#include <utils/utilsicons.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/find/basetextfind.h>
#include <coreplugin/find/textfindconstants.h>
#include <coreplugin/findplaceholder.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/minisplitter.h>
#include <coreplugin/openmvpluginescapecodeparser.h>
#include <texteditor/icodestylepreferences.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <texteditor/tabsettings.h>
#include <texteditor/texteditor.h>

#include "openmvpluginfb.h"
#include "openmv/histogram/openmvpluginhistogram.h"

namespace OpenMV {
namespace Internal {

class MyPlainTextEdit : public QPlainTextEdit
{
    Q_OBJECT

public:

    explicit MyPlainTextEdit(qreal fontPointSizeF, QWidget *parent = Q_NULLPTR);

public slots:

    void readBytes(const QByteArray &data);
    void clear();
    void save();
    void execute(bool standAlone = false);
    void interrupt();
    void reload();

signals:

    void writeBytes(const QByteArray &data);
    void execScript(const QByteArray &data);
    void interruptScript();
    void reloadScript();
    void paste(const QByteArray &data);
    void frameBufferData(const QPixmap &data);

protected:

    void keyPressEvent(QKeyEvent *event);
    void wheelEvent(QWheelEvent *event);
    void resizeEvent(QResizeEvent *event);
    void contextMenuEvent(QContextMenuEvent *event);
    bool focusNextPrevChild(bool next);
    void focusInEvent(QFocusEvent *event);
    void focusOutEvent(QFocusEvent *event);
    void paintEvent(QPaintEvent *event);

private:

    int m_tabWidth;
    QTimer *m_cursorBlinkTimer;
    QTextCursor m_textCursor;

    enum
    {
        ASCII,
        UTF_8,
        EXIT_0,
        EXIT_1
    }
    m_stateMachine;
    bool m_strip_newline;

    QByteArray m_shiftReg;
    QByteArray m_frameBufferData;
    Utils::AnsiEscapeCodeHandler m_handler;
    QChar m_lastChar;
    bool m_isCursorVisible;
    Core::OpenMVPluginEscapeCodeParser *m_parser;
};

class OpenMVTerminal : public QWidget
{
    Q_OBJECT

public:

    explicit OpenMVTerminal(const QString &displayName, Utils::QtcSettings *settings, const Core::Context &context, bool stand_alone = false, QWidget *parent = Q_NULLPTR);
    ~OpenMVTerminal();

signals:

    void readBytes(const QByteArray &data);
    void writeBytes(const QByteArray &data);
    void execScript(const QByteArray &data);
    void interruptScript();
    void reloadScript();
    void paste(const QByteArray &data);

protected:

    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    void paintEvent(QPaintEvent *event) override;

private:

    Utils::QtcSettings *m_settings;
    Core::MiniSplitter *m_hsplitter;
    Core::MiniSplitter *m_vsplitter;
    QToolButton *m_topDrawer;
    QToolButton *m_bottomDrawer;
    QToolButton *m_leftDrawer;
    QToolButton *m_rightDrawer;
    QToolButton *m_zoomButton;
    QComboBox *m_colorSpace;
    QToolButton *m_wrapButton;
    MyPlainTextEdit *m_edit;
    Core::IContext *m_context;
    QString m_styleSheet, m_highDPIStyleSheet;
    qreal m_devicePixelRatio;
};

// Base ///////////////////////////////////////////////////////////////////////

class OpenMVTerminalPort : public QObject
{
    Q_OBJECT

public:

    explicit OpenMVTerminalPort(QObject *parent = Q_NULLPTR) : QObject(parent) { }

signals:

    void open(const QString &commandStr, int commandVal);
    void openResult(const QString &errorMessage);

    void writeBytes(const QByteArray &data);
    void execScript(const QByteArray &data);
    void interruptScript();
    void reloadScript();
    void paste(const QByteArray &data);
    void readBytes(const QByteArray &data);
};

// Serial Port Thread /////////////////////////////////////////////////////////

class OpenMVTerminalSerialPort_private : public QObject
{
    Q_OBJECT

public:

    explicit OpenMVTerminalSerialPort_private(QObject *parent = Q_NULLPTR);

public slots:

    void open(const QString &portName, int buadRate);
    void writeBytes(const QByteArray &data);
    void execScript(const QByteArray &data);
    void interruptScript();
    void reloadScript();
    void paste(const QByteArray &data);

signals:

    void openResult(const QString &errorMessage);
    void readBytes(const QByteArray &data);

private:

    QSerialPort *m_port;
    bool m_readEnabled;
};

class OpenMVTerminalSerialPort : public OpenMVTerminalPort
{
    Q_OBJECT

public:

    explicit OpenMVTerminalSerialPort(QObject *parent = Q_NULLPTR);
};

// UDP Port Thread ////////////////////////////////////////////////////////////

class OpenMVTerminalUDPPort_private : public QObject
{
    Q_OBJECT

public:

    explicit OpenMVTerminalUDPPort_private(QObject *parent = Q_NULLPTR);

public slots:

    void open(const QString &hostName, int port);
    void writeBytes(const QByteArray &data);
    void execScript(const QByteArray &data);
    void interruptScript();
    void reloadScript();
    void paste(const QByteArray &data);

signals:

    void openResult(const QString &errorMessage);
    void readBytes(const QByteArray &data);

private:

    QUdpSocket *m_port;
    bool m_readEnabled;
};

class OpenMVTerminalUDPPort : public OpenMVTerminalPort
{
    Q_OBJECT

public:

    explicit OpenMVTerminalUDPPort(QObject *parent = Q_NULLPTR);
};

// TCP Port Thread ////////////////////////////////////////////////////////////

class OpenMVTerminalTCPPort_private : public QObject
{
    Q_OBJECT

public:

    explicit OpenMVTerminalTCPPort_private(QObject *parent = Q_NULLPTR);

public slots:

    void open(const QString &hostName, int port);
    void writeBytes(const QByteArray &data);
    void execScript(const QByteArray &data);
    void interruptScript();
    void reloadScript();
    void paste(const QByteArray &data);

signals:

    void openResult(const QString &errorMessage);
    void readBytes(const QByteArray &data);

private:

    QTcpSocket *m_port;
    bool m_readEnabled;
};

class OpenMVTerminalTCPPort : public OpenMVTerminalPort
{
    Q_OBJECT

public:

    explicit OpenMVTerminalTCPPort(QObject *parent = Q_NULLPTR);
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVTERMINAL_H
