#ifndef OPENMVPLUGINSERIALPORT_H
#define OPENMVPLUGINSERIALPORT_H

#include <QtCore>
#include <QtNetwork>

#include <QtSerialPort/QSerialPort>
#include <QtSerialPort/QSerialPortInfo>

#include <utils/hostosinfo.h>

#define OPENMVCAM_VID 0x1209
#define OPENMVCAM_PID 0xABD1

#define ARDUINOCAM_VID          0x2341
#define ARDUINOCAM_PH7_PID      0x005B
#define ARDUINOCAM_NRF_PID      0x005A
#define ARDUINOCAM_RPI_PID      0x005E
#define ARDUINOCAM_NCL_PID      0x005F
#define ARDUINOCAM_PID_MASK     0x00FF
#define PORTENTA_APP_O_PID      0x005B // old
#define PORTENTA_APP_N_PID      0x045B // new
#define PORTENTA_TTR_1_PID      0x025B
#define PORTENTA_TTR_2_PID      0x805B
#define PORTENTA_LDR_PID        0x035B
#define NRF_OLD_PID             0x805A
#define NRF_APP_PID             0x015A
#define NRF_LDR_PID             0x005A
#define RPI_OLD_PID             0x805E
#define RPI_APP_PID             0x015E
#define RPI_LDR_PID             0x005E
#define RPI2040_VID             0x28EA
#define RPI2040_PID             0x0003
#define NICLA_APP_PID           0x045F
#define NICLA_TTR_1_PID         0x025F
#define NICLA_TTR_2_PID         0x805F
#define NICLA_LDR_PID           0x035F

#define OPENMVCAM_BROADCAST_PORT 0xABD1

// OSX will for sure send a zero length USB packet if you send a packet that's
// a multiple of the end-point size. By not ever doing that you ensure that
// the OpenMV Cam will not see a zero-length packet. Other operating systems
// like windows/linux do not do this.
#define TABOO_PACKET_SIZE 64

#define FS_EP_SIZE                      64
#define HS_EP_SIZE                      512
#define FS_CHUNK_SIZE                   ((FS_EP_SIZE) - 4) // space for header
#define HS_CHUNK_SIZE                   ((HS_EP_SIZE) - 4) // space for header
#define SAFE_FS_CHUNK_SIZE              (FS_CHUNK_SIZE - 4) // space for header
#define SAFE_HS_CHUNK_SIZE              (HS_CHUNK_SIZE - 4) // space for header

///////////////////////////////////////////////////////////////////////////////

#define __USBDBG_CMD                        0x30
#define __USBDBG_FW_VERSION                 0x80
#define __USBDBG_FRAME_SIZE                 0x81
#define __USBDBG_FRAME_DUMP                 0x82
#define __USBDBG_ARCH_STR                   0x83
#define __USBDBG_LEARN_MTU                  0x84
#define __USBDBG_SCRIPT_EXEC                0x05
#define __USBDBG_SCRIPT_STOP                0x06
#define __USBDBG_SCRIPT_SAVE                0x07
#define __USBDBG_SCRIPT_RUNNING             0x87
#define __USBDBG_TEMPLATE_SAVE              0x08
#define __USBDBG_DESCRIPTOR_SAVE            0x09
#define __USBDBG_ATTR_READ                  0x8A // old
#define __USBDBG_ATTR_READ_2                0xCA // new
#define __USBDBG_ATTR_WRITE                 0x0B
#define __USBDBG_SYS_RESET                  0x0C
#define __USBDBG_SYS_RESET_TO_BL            0x0E
#define __USBDBG_FB_ENABLE                  0x0D
#define __USBDBG_JPEG_ENABLE                0x0E // not used anymore...
#define __USBDBG_TX_BUF_LEN                 0x8E
#define __USBDBG_TX_BUF                     0x8F
#define __USBDBG_SENSOR_ID                  0x90
#define __USBDBG_TX_INPUT                   0x11
#define __USBDBG_TIME_INPUT                 0x12

#define __BOOTLDR_START                     static_cast<int>(0xABCD0001)
#define __BOOTLDR_RESET                     static_cast<int>(0xABCD0002)
#define __BOOTLDR_ERASE                     static_cast<int>(0xABCD0004)
#define __BOOTLDR_WRITE                     static_cast<int>(0xABCD0008)
#define __BOOTLDR_QUERY                     static_cast<int>(0xABCD0010)
#define __BOOTLDR_QSPIF_ERASE               static_cast<int>(0xABCD1004)
#define __BOOTLDR_QSPIF_WRITE               static_cast<int>(0xABCD1008)
#define __BOOTLDR_QSPIF_LAYOUT              static_cast<int>(0xABCD1010)
#define __BOOTLDR_QSPIF_MEMTEST             static_cast<int>(0xABCD1020)

#define FW_VERSION_RESPONSE_LEN             12
#define ARCH_STR_RESPONSE_LEN               64
#define FRAME_SIZE_RESPONSE_LEN             12
#define SCRIPT_RUNNING_RESPONSE_LEN         4
#define ATTR_READ_RESPONSE_LEN              1
#define ATTR_READ_2_PAYLOAD_LEN             4
#define ATTR_READ_2_REPONSE_LEN             4
#define ATTR_WRITE_PAYLOAD_LEN              8
#define FB_ENABLE_PAYLOAD_LEN               4
#define JPEG_ENABLE_PAYLOAD_LEN             4
#define TX_BUF_LEN_RESPONSE_LEN             4
#define SENSOR_ID_RESPONSE_LEN              4
#define TX_INPUT_PAYLOAD_LEN                4
#define TIME_INPUT_PAYLOAD_LEN              4

#define BOOTLDR_START_RESPONSE_LEN          4
#define BOOTLDR_QUERY_RESPONSE_LEN          12
#define BOOTLDR_QSPIF_LAYOUT_RESPONSE_LEN   12
#define BOOTLDR_QSPIF_MEMTEST_RESPONSE_LEN  4
#define V1_BOOTLDR                          static_cast<int>(0xABCD0001)
#define V2_BOOTLDR                          static_cast<int>(0xABCD0002)
#define V3_BOOTLDR                          static_cast<int>(0xABCD0003)

#define FW_VERSION_START_DELAY              0
#define FW_VERSION_END_DELAY                0
#define FRAME_SIZE_START_DELAY              0
#define FRAME_SIZE_END_DELAY                0
#define FRAME_DUMP_START_DELAY              0
#define FRAME_DUMP_END_DELAY                0
#define ARCH_STR_START_DELAY                0
#define ARCH_STR_END_DELAY                  0
#define LEARN_MTU_START_DELAY               0
#define LEARN_MTU_END_DELAY                 0
#define SCRIPT_EXEC_START_DELAY             50
#define SCRIPT_EXEC_END_DELAY               25
#define SCRIPT_EXEC_2_START_DELAY           25
#define SCRIPT_EXEC_2_END_DELAY             50
#define SCRIPT_STOP_START_DELAY             50
#define SCRIPT_STOP_END_DELAY               50
#define SCRIPT_SAVE_START_DELAY             50
#define SCRIPT_SAVE_END_DELAY               50
#define SCRIPT_RUNNING_START_DELAY          0
#define SCRIPT_RUNNING_END_DELAY            0
#define TEMPLATE_SAVE_START_DELAY           50
#define TEMPLATE_SAVE_END_DELAY             25
#define TEMPLATE_SAVE_2_START_DELAY         25
#define TEMPLATE_SAVE_2_END_DELAY           50
#define DESCRIPTOR_SAVE_START_DELAY         50
#define DESCRIPTOR_SAVE_END_DELAY           25
#define DESCRIPTOR_SAVE_2_START_DELAY       25
#define DESCRIPTOR_SAVE_2_END_DELAY         50
#define ATTR_READ_START_DELAY               0
#define ATTR_READ_END_DELAY                 0
#define ATTR_READ_0_START_DELAY             0
#define ATTR_READ_0_END_DELAY               50
#define ATTR_READ_1_START_DELAY             50
#define ATTR_READ_1_END_DELAY               0
#define ATTR_WRITE_START_DELAY              50
#define ATTR_WRITE_END_DELAY                50
#define ATTR_WRITE_0_START_DELAY            50
#define ATTR_WRITE_0_END_DELAY              25
#define ATTR_WRITE_1_START_DELAY            25
#define ATTR_WRITE_1_END_DELAY              50
#define SYS_RESET_START_DELAY               50
#define SYS_RESET_END_DELAY                 50
#define SYS_RESET_TO_BL_START_DELAY         50
#define SYS_RESET_TO_BL_END_DELAY           50
#define FB_ENABLE_START_DELAY               50
#define FB_ENABLE_END_DELAY                 50
#define FB_ENABLE_0_START_DELAY             50
#define FB_ENABLE_0_END_DELAY               25
#define FB_ENABLE_1_START_DELAY             25
#define FB_ENABLE_1_END_DELAY               50
#define JPEG_ENABLE_START_DELAY             50
#define JPEG_ENABLE_END_DELAY               50
#define JPEG_ENABLE_0_START_DELAY           50
#define JPEG_ENABLE_0_END_DELAY             25
#define JPEG_ENABLE_1_START_DELAY           25
#define JPEG_ENABLE_1_END_DELAY             50
#define TX_BUF_LEN_START_DELAY              0
#define TX_BUF_LEN_END_DELAY                0
#define TX_BUF_START_DELAY                  0
#define TX_BUF_END_DELAY                    0
#define SENSOR_ID_START_DELAY               0
#define SENSOR_ID_END_DELAY                 0
#define TX_INPUT_0_START_DELAY              2
#define TX_INPUT_0_END_DELAY                2
#define TX_INPUT_1_START_DELAY              2
#define TX_INPUT_1_END_DELAY                2
#define TIME_INPUT_0_START_DELAY            2
#define TIME_INPUT_0_END_DELAY              2
#define TIME_INPUT_1_START_DELAY            2
#define TIME_INPUT_1_END_DELAY              2

#define BOOTLDR_START_START_DELAY           0
#define BOOTLDR_START_END_DELAY             0
#define BOOTLDR_RESET_START_DELAY           5
#define BOOTLDR_RESET_END_DELAY             5
#define BOOTLDR_ERASE_START_DELAY           0
#define BOOTLDR_ERASE_END_DELAY             0
#define BOOTLDR_WRITE_START_DELAY           0
#define BOOTLDR_WRITE_END_DELAY             0
#define BOOTLDR_QUERY_START_DELAY           0
#define BOOTLDR_QUERY_END_DELAY             0
#define BOOTLDR_QSPIF_ERASE_START_DELAY     0
#define BOOTLDR_QSPIF_ERASE_END_DELAY       0
#define BOOTLDR_QSPIF_WRITE_START_DELAY     0
#define BOOTLDR_QSPIF_WRITE_END_DELAY       0
#define BOOTLDR_QSPIF_LAYOUT_START_DELAY    0
#define BOOTLDR_QSPIF_LAYOUT_END_DELAY      0
#define BOOTLDR_QSPIF_MEMTEST_START_DELAY   0
#define BOOTLDR_QSPIF_MEMTEST_END_DELAY     0

///////////////////////////////////////////////////////////////////////////////

void serializeByte(QByteArray &buffer, int value); // LittleEndian
void serializeWord(QByteArray &buffer, int value); // LittleEndian
void serializeLong(QByteArray &buffer, int value); // LittleEndian

int deserializeByte(QByteArray &buffer); // LittleEndian
int deserializeWord(QByteArray &buffer); // LittleEndian
int deserializeLong(QByteArray &buffer); // LittleEndian

class OpenMVPluginSerialPortCommand
{
public:
    explicit OpenMVPluginSerialPortCommand(const QByteArray &data = QByteArray(), int responseLen = int(), int startWait = int(), int endWait = int(), bool perCommandWait = true, bool commandAbortOkay = false) :
        m_data(data), m_responseLen(responseLen), m_startWait(startWait), m_endWait(endWait), m_perCommandWait(perCommandWait), m_commandAbortOkay(commandAbortOkay) { }
    QByteArray m_data;
    int m_responseLen;
    int m_startWait; // in ms
    int m_endWait; // in ms
    bool m_perCommandWait;
    bool m_commandAbortOkay;
};

class OpenMVPluginSerialPortCommandResult
{
public:
    explicit OpenMVPluginSerialPortCommandResult(bool ok = bool(), const QByteArray &data = QByteArray()) :
        m_ok(ok), m_data(data) { }
    bool m_ok;
    QByteArray m_data;
};

class OpenMVPluginSerialPort_thing : public QObject
{
    Q_OBJECT

public:

    explicit OpenMVPluginSerialPort_thing(const QString &name, QObject *parent = Q_NULLPTR);
    QString portName();
    bool isSerialPort() { return m_serialPort != Q_NULLPTR; }
    bool isTCPPort() { return m_tcpSocket != Q_NULLPTR; }

    void setReadBufferSize(qint64 size);
    bool setBaudRate(qint32 baudRate);

    bool open(QIODevice::OpenMode mode);
    bool flush();

    QString errorString();
    void clearError();

    QByteArray readAll();
    qint64 write(const QByteArray &data);

    qint64 bytesAvailable();
    qint64 bytesToWrite();

    bool waitForReadyRead(int msecs);
    bool waitForBytesWritten(int msecs);
    bool setDataTerminalReady(bool set);
    bool setRequestToSend(bool set);

private:

    QSerialPort *m_serialPort;
    QTcpSocket *m_tcpSocket;
};

class OpenMVPluginSerialPort_private : public QObject
{
    Q_OBJECT

public:

    explicit OpenMVPluginSerialPort_private(int override_read_timeout = -1, int override_read_stall_timeout = -1, QObject *parent = Q_NULLPTR);

public slots:

    void open(const QString &portName);
    void command(const OpenMVPluginSerialPortCommand &command);

    void bootloaderStart(const QString &selectedPort);
    void bootloaderStop();
    void bootloaderReset();

signals:

    void openResult(const QString &errorMessage);
    void commandResult(const OpenMVPluginSerialPortCommandResult &commandResult);

    void bootloaderStartResponse(bool ok, int version, int highspeed);
    void bootloaderStopResponse();
    void bootloaderResetResponse();

private:

    void write(const QByteArray &data, int startWait, int stopWait, int timeout);

    OpenMVPluginSerialPort_thing *m_port;
    bool m_bootloaderStop;
    int m_override_read_timeout;
    int m_override_read_stall_timeout;
};

class OpenMVPluginSerialPort : public QObject
{
    Q_OBJECT

public:

    explicit OpenMVPluginSerialPort(int override_read_timeout = -1, int override_read_stall_timeout = -1, QObject *parent = Q_NULLPTR);

signals:

    void open(const QString &portName);
    void openResult(const QString &errorMessage);

    void command(const OpenMVPluginSerialPortCommand &command);
    void commandResult(const OpenMVPluginSerialPortCommandResult &commandResult);

    void bootloaderStart(const QString &selectedPort);
    void bootloaderStop();
    void bootloaderReset();

    void bootloaderStartResponse(bool ok, int version, int highspeed);
    void bootloaderStopResponse();
    void bootloaderResetResponse();
};

#endif // OPENMVPLUGINSERIALPORT_H
