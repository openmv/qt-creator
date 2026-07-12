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

#ifndef OPENMVPLUGINSERIALPORT_H
#define OPENMVPLUGINSERIALPORT_H

#include <QtCore>
#include <QtNetwork>

#include <utils/hostosinfo.h>

#include "tools/myqserialportinfo.h"

#include "protocol/omv_camera.h"
#include "protocol/omv_port.h"

#define STM32_DFU_VID           0x0483
#define STM32_DFU_PID           0xDF11
#define OPENMVCAM_VID           0x1209
#define OPENMVCAM_PID           0xABD1

#define OPENMVCAM_VID_NEW       0x37C5
#define OPENMVCAM_RT1062_PID    0x1060
#define OPENMVCAM_AE3_PID       0x16E3

#define ARDUINOCAM_VID          0x2341
#define ARDUINOCAM_PH7_PID      0x005B
#define ARDUINOCAM_NRF_PID      0x005A
#define ARDUINOCAM_RPI_PID      0x005E
#define ARDUINOCAM_NCL_PID      0x005F
#define ARDUINOCAM_GH7_PID      0x0066
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
#define NICLA_APP_O_PID         0x045F // old
#define NICLA_APP_N_PID         0x055F // new
#define NICLA_TTR_1_PID         0x025F
#define NICLA_TTR_2_PID         0x805F
#define NICLA_LDR_PID           0x035F
#define GIGA_LDR_PID            0x0366
#define GIGA_APP_PID            0x0466
#define GIGA_TTR_1_PID          0x0266
#define GIGA_TTR_2_PID          0x8066

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
#define __USBDBG_GET_STATE                  0x93
#define __USBDBG_PROFILE_SIZE               0x94
#define __USBDBG_PROFILE_DUMP               0x95
#define __USBDBG_SET_PROFILE_MODE           0x16
#define __USBDBG_SET_EVT_CNTR               0x17
#define __USBDBG_PROFILE_RESET              0x18

#define __USBDBG_GET_STATE_FLAGS_SCRIPT     (1 << 0)
#define __USBDBG_GET_STATE_FLAGS_TEXT       (1 << 1)
#define __USBDBG_GET_STATE_FLAGS_FRAME      (1 << 2)
#define __USBDBG_GET_STATE_FLAGS_PROFILE    (1 << 3)
#define __USBDBG_GET_STATE_FLAGS_HAS_PMU    (1 << 5)

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
#define FRAME_DUMP_UNLOCK_RESPONSE_LEN      4
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
#define GET_STATE_PAYLOAD_LEN               64
#define GET_STATE_PAYLOAD_LEN_FS            63
#define GET_STATE_PAYLOAD_LEN_HS            511
#define PROFILE_SIZE_RESPONSE_LEN           12
#define SET_PROFILE_MODE_PAYLOAD_LEN        4
#define SET_EVENT_COUNTER_PAYLOAD_LEN       8
#define PROFILE_RESET_PAYLOAD_LEN           0

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
#define FRAME_DUMP_UNLOCK_START_DELAY       0
#define FRAME_DUMP_UNLOCK_END_DELAY         0
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
#define GET_STATE_START_DELAY               0
#define GET_STATE_END_DELAY                 0
#define PROFILE_SIZE_START_DELAY            0
#define PROFILE_SIZE_END_DELAY              0
#define PROFILE_DUMP_START_DELAY            0
#define PROFILE_DUMP_END_DELAY              0
#define SET_PROFILE_MODE_0_START_DELAY      50
#define SET_PROFILE_MODE_0_END_DELAY        25
#define SET_PROFILE_MODE_1_START_DELAY      25
#define SET_PROFILE_MODE_1_END_DELAY        50
#define SET_EVT_CNTR_0_START_DELAY          50
#define SET_EVT_CNTR_0_END_DELAY            25
#define SET_EVT_CNTR_1_START_DELAY          25
#define SET_EVT_CNTR_1_END_DELAY            50
#define PROFILE_RESET_START_DELAY           50
#define PROFILE_RESET_END_DELAY             50

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

namespace OpenMV {
namespace Internal {

using namespace omv;

typedef struct profile_record {
    uint32_t address;
    uint32_t caller;
    uint32_t call_count;
    uint32_t min_ticks;
    uint32_t max_ticks;
    uint64_t total_ticks;
    uint64_t total_cycles;
    QList<uint64_t> events;
} profile_record_t;

void serializeByte(QByteArray &buffer, int value); // LittleEndian
void serializeWord(QByteArray &buffer, int value); // LittleEndian
void serializeLong(QByteArray &buffer, int value); // LittleEndian

int deserializeByte(QByteArray &buffer); // LittleEndian
int deserializeWord(QByteArray &buffer); // LittleEndian
int deserializeLong(QByteArray &buffer); // LittleEndian

bool isTouchToReset(const QJsonDocument &settings, const MyQSerialPortInfo &port);

class OpenMVPluginSerialPortCommand
{
public:
    explicit OpenMVPluginSerialPortCommand(const QByteArray &data = QByteArray(),
                                           int responseLen = int(),
                                           int startWait = int(),
                                           int endWait = int(),
                                           bool perCommandWait = true,
                                           bool commandAbortOkay = false,
                                           bool readFlushBeforeCommnad = false) :
        m_data(data),
        m_responseLen(responseLen),
        m_startWait(startWait),
        m_endWait(endWait),
        m_perCommandWait(perCommandWait),
        m_commandAbortOkay(commandAbortOkay),
        m_readFlushBeforeCommnad(readFlushBeforeCommnad)
    {
    }
    QByteArray m_data;
    int m_responseLen;
    int m_startWait; // in ms
    int m_endWait; // in ms
    bool m_perCommandWait;
    bool m_commandAbortOkay;
    bool m_readFlushBeforeCommnad;
};

class OpenMVPluginSerialPortCommandResult
{
public:
    explicit OpenMVPluginSerialPortCommandResult(bool ok = bool(),
                                                 const QByteArray &data = QByteArray()) :
        m_ok(ok),
        m_data(data)
    {
    }
    bool m_ok;
    QByteArray m_data;
};

class OpenMVPluginSerialPort_private : public QObject
{
    Q_OBJECT

public:

    explicit OpenMVPluginSerialPort_private(const QJsonDocument &settings = QJsonDocument(),
                                            QObject *parent = Q_NULLPTR);
    ~OpenMVPluginSerialPort_private();

public slots:

    // Shared

    void enableV2Protocol(bool enable);
    void open(const QString &portName);

    // V1 protocol
    //
    // Serial thread only implements the transport layer of the protocol.
    // The GUI thread implements the transaction layer.

    void command(const OpenMVPluginSerialPortCommand &command);

    void bootloaderStart(const QString &selectedPort);
    void bootloaderStop();
    void bootloaderReset();

    // V2 protocol
    //
    // Serial thread implements the transport and transaction layer of the protocol.

    void getMemoryStats();
    void getSystemInfo();
    void getProtocolStats();
    void getFirmwareVersion();
    void getJPEGPreferred();
    void getFrameReady(); // poll event
    void frameDump();
    void getArchString();
    void scriptExec(const QByteArray &data);
    void scriptStop();
    void getScriptRunning();
    void sysReset(bool enterBootloader = false);
    void fbEnable(bool enable);
    void jpegEnable(bool enable);
    void getTxBuffer();
    void sensorId();
    void getState();
    void readProfile();
    void setProfileMode(int mode);
    void setEventCounter(int event_num, int event_type);
    void profileReset();
    void close();

signals:

    // Shared

    void enableV2ProtocolResponse();
    void openResult(const QString &errorMessage);

    // V1 protocol
    //
    // Serial thread only implements the transport layer of the protocol.
    // The GUI thread implements the transaction layer.

    void commandResult(const OpenMVPluginSerialPortCommandResult &commandResult);

    void bootloaderStartResponse(bool ok, int version, int highspeed);
    void bootloaderStopResponse();
    void bootloaderResetResponse();

    // V2 protocol
    //
    // Serial thread implements the transport and transaction layer of the protocol.

    void memoryStats(bool timeout, bool error, const QVariantList &entries);
    void systemInfo(bool timeout, bool error, const QVariantMap &info);
    void protocolStats(bool timeout, bool error, const QVariantMap &host,
                       const QVariantMap &device, const QVariantList &channels);
    void firmwareVersion(bool timeout, int major, int minor, int patch);
    void jpegPreferred(bool timeout, bool preferred);
    void frameReady(bool ready); // poll event
    void frameBufferData(bool timeout, const QPixmap &data);
    void cameraFrameRate(double fps); // on-camera FPS from the v5.0.0 stream header
    void archString(bool timeout, const QString &arch);
    void scriptExecDone(bool timeout);
    void scriptStopDone(bool timeout);
    void scriptRunning(bool timeout, bool running);
    void sysResetDone(bool timeout);
    void fbEnableDone(bool timeout);
    void jpegEnableDone(bool timeout);
    void printData(bool timeout, const QByteArray &data);
    void sensorIdDone(bool timeout, QList<int> ids);
    void getStateDone(bool timeout, bool running, bool profileEnabled, bool hasPMU,
                      const QByteArray &data, const QPixmap &img);
    void readProfileDone(bool timeout, const QList<profile_record_t> &records);
    void setProfileModeDone(bool timeout);
    void setEventCounterDone(bool timeout);
    void profileResetDone(bool timeout);
    void closeResponse(bool timeout);

private:

    void write(const QByteArray &data, int startWait, int stopWait, int timeout);

    QTimer *m_idleTimer;
    QPointer<OMVPort> m_port;
    OMVCamera *m_camera;
    bool m_v2ProtocolEnabled;
    bool m_bootloaderStop;
    QJsonDocument m_firmwareSettings;
};

class OpenMVPluginSerialPort : public QObject
{
    Q_OBJECT

public:

    explicit OpenMVPluginSerialPort(const QJsonDocument &settings = QJsonDocument(),
                                    QObject *parent = Q_NULLPTR);

    // Serial-terminal debug-logging level for the v1 protocol (0 off .. 3 fragments).
    // Set on the GUI thread; read on the serial thread, so it's an atomic.
    static void setDebugLevel(int level);

    void terminate();

signals:

    // Shared

    void enableV2Protocol(bool enable);
    void enableV2ProtocolResponse();
    void open(const QString &portName);
    void openResult(const QString &errorMessage);

    // V1 protocol
    //
    // Serial thread only implements the transport layer of the protocol.
    // The GUI thread implements the transaction layer.

    void command(const OpenMVPluginSerialPortCommand &command);
    void commandResult(const OpenMVPluginSerialPortCommandResult &commandResult);

    void bootloaderStart(const QString &selectedPort);
    void bootloaderStop();
    void bootloaderReset();

    void bootloaderStartResponse(bool ok, int version, int highspeed);
    void bootloaderStopResponse();
    void bootloaderResetResponse();

    void updateSettings(bool unstuckWithGetState);
    void settingsUpdated();

    // V2 protocol
    //
    // Serial thread implements the transport and transaction layer of the protocol.

    void getMemoryStats();
    void getSystemInfo();
    void getProtocolStats();
    void getFirmwareVersion();
    void getJPEGPreferred();
    void getFrameReady(); // poll event
    void frameDump();
    void getArchString();
    void scriptExec(const QByteArray &data);
    void scriptStop();
    void getScriptRunning();
    void sysReset(bool enterBootloader = false);
    void fbEnable(bool enable);
    void jpegEnable(bool enable);
    void getTxBuffer();
    void sensorId();
    void getState();
    void readProfile();
    void setProfileMode(int mode);
    void setEventCounter(int event_num, int event_type);
    void profileReset();
    void close();

    void memoryStats(bool timeout, bool error, const QVariantList &entries);
    void systemInfo(bool timeout, bool error, const QVariantMap &info);
    void protocolStats(bool timeout, bool error, const QVariantMap &host,
                       const QVariantMap &device, const QVariantList &channels);
    void firmwareVersion(bool timeout, int major, int minor, int patch);
    void jpegPreferred(bool timeout, bool preferred);
    void frameReady(bool ready); // poll event
    void frameBufferData(bool timeout, const QPixmap &data);
    void cameraFrameRate(double fps); // on-camera FPS from the v5.0.0 stream header
    void archString(bool timeout, const QString &arch);
    void scriptExecDone(bool timeout);
    void scriptStopDone(bool timeout);
    void scriptRunning(bool timeout, bool running);
    void sysResetDone(bool timeout);
    void fbEnableDone(bool timeout);
    void jpegEnableDone(bool timeout);
    void printData(bool timeout, const QByteArray &data);
    void sensorIdDone(bool timeout, QList<int> ids);
    void getStateDone(bool timeout, bool running, bool profileEnabled, bool hasPMU,
                      const QByteArray &data, const QPixmap &img);
    void readProfileDone(bool timeout, const QList<profile_record_t> &records);
    void setProfileModeDone(bool timeout);
    void setEventCounterDone(bool timeout);
    void profileResetDone(bool timeout);
    void closeResponse(bool timeout);

private:

    QThread *m_thread;
    OpenMVPluginSerialPort_private *m_port;
};

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVPLUGINSERIALPORT_H
