#ifndef OPENMVTERMINAL_H
#define OPENMVTERMINAL_H

#include <QtCore>
#include <QtGui>
#include <QtWidgets>
#include <QtNetwork>

#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>

#include <aggregation/aggregate.h>
#include <utils/ansiescapecodehandler.h>
#include <utils/elidinglabel.h>
#include <utils/fadingindicator.h>
#include <utils/styledbar.h>
#include <utils/utilsicons.h>
#include <coreplugin/actionmanager/actionmanager.h>
#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/find/basetextfind.h>
#include <coreplugin/findplaceholder.h>
#include <coreplugin/icontext.h>
#include <coreplugin/icore.h>
#include <coreplugin/idocument.h>
#include <coreplugin/minisplitter.h>
#include <texteditor/icodestylepreferences.h>
#include <texteditor/fontsettings.h>
#include <texteditor/texteditorsettings.h>
#include <texteditor/tabsettings.h>
#include <texteditor/texteditor.h>

#include "openmvpluginfb.h"
#include "openmv/histogram/openmvpluginhistogram.h"

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
    void frameBufferData(const QPixmap &data);

protected:

    void keyPressEvent(QKeyEvent *event);
    void wheelEvent(QWheelEvent *event);
    void resizeEvent(QResizeEvent *event);
    void contextMenuEvent(QContextMenuEvent *event);
    bool focusNextPrevChild(bool next);
    void paintEvent(QPaintEvent *event);

private:

    int m_tabWidth;
    QTextCursor m_textCursor;

    enum
    {
        ASCII,
        UTF_8,
        EXIT_0,
        EXIT_1
    }
    m_stateMachine;

    QByteArray m_shiftReg;
    QByteArray m_frameBufferData;
    Utils::AnsiEscapeCodeHandler m_handler;
    QChar m_lastChar;
    bool m_isCursorVisible;
};

class OpenMVTerminal : public QWidget
{
    Q_OBJECT

public:

    explicit OpenMVTerminal(const QString &displayName, QSettings *settings, const Core::Context &context, bool stand_alone = false, QWidget *parent = Q_NULLPTR);
    ~OpenMVTerminal();

signals:

    void readBytes(const QByteArray &data);
    void writeBytes(const QByteArray &data);

protected:

    void showEvent(QShowEvent *event);
    void closeEvent(QCloseEvent *event);

    void paintEvent(QPaintEvent *event) override;

private:

    QSettings *m_settings;
    Core::MiniSplitter *m_hsplitter;
    Core::MiniSplitter *m_vsplitter;
    QToolButton *m_topDrawer;
    QToolButton *m_bottomDrawer;
    QToolButton *m_leftDrawer;
    QToolButton *m_rightDrawer;
    QToolButton *m_zoomButton;
    QComboBox *m_colorSpace;
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

signals:

    void openResult(const QString &errorMessage);
    void readBytes(const QByteArray &data);

private:

    QSerialPort *m_port;
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

signals:

    void openResult(const QString &errorMessage);
    void readBytes(const QByteArray &data);

private:

    QUdpSocket *m_port;
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

signals:

    void openResult(const QString &errorMessage);
    void readBytes(const QByteArray &data);

private:

    QTcpSocket *m_port;
};

class OpenMVTerminalTCPPort : public OpenMVTerminalPort
{
    Q_OBJECT

public:

    explicit OpenMVTerminalTCPPort(QObject *parent = Q_NULLPTR);
};

#endif // OPENMVTERMINAL_H
