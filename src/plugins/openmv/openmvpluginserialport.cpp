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

#include "openmvpluginserialport.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#define OPENMVCAM_BAUD_RATE 921600
#define OPENMVCAM_BAUD_RATE_2 12000000

#define ARDUINO_TTR_BAUD_RATE 1200

#define WRITE_LOOPS 1 // disabled
#define WRITE_DELAY 0 // disabled
#define FLUSH_TIMEOUT 100
#define WRITE_TIMEOUT 3000
#define BOOTLOADER_WRITE_TIMEOUT 6
#define BOOTLOADER_READ_TIMEOUT 10
#define BOOTLOADER_READ_STALL_TIMEOUT 2
#define LEARN_MTU_WRITE_TIMEOUT 30
#define LEATN_MTU_READ_TIMEOUT 50

#define LEARN_MTU_MAX 4096
#define LEARN_MTU_MIN 64

#define READ_BUFFER_SIZE (64 * 1024 * 1024)

#define DYNAMIC_READ_STALL_ENABLE 0
#define DYNAMIC_READ_STALL_BUFFER_SIZE 20
#define DYNAMIC_READ_STALL_THRESHOLD 10

namespace OpenMV {
namespace Internal {

void serializeByte(QByteArray &buffer, int value) // LittleEndian
{
    buffer.append(reinterpret_cast<const char *>(&value), 1);
}

void serializeWord(QByteArray &buffer, int value) // LittleEndian
{
    buffer.append(reinterpret_cast<const char *>(&value), 2);
}

void serializeLong(QByteArray &buffer, int value) // LittleEndian
{
    buffer.append(reinterpret_cast<const char *>(&value), 4);
}

int deserializeByte(QByteArray &buffer) // LittleEndian
{
    int r = int();
    memcpy(&r, buffer.data(), 1);
    buffer = buffer.mid(1);
    return r;
}

int deserializeWord(QByteArray &buffer) // LittleEndian
{
    int r = int();
    memcpy(&r, buffer.data(), 2);
    buffer = buffer.mid(2);
    return r;
}

int deserializeLong(QByteArray &buffer) // LittleEndian
{
    int r = int();
    memcpy(&r, buffer.data(), 4);
    buffer = buffer.mid(4);
    return r;
}

bool isTouchToReset(const QJsonDocument &settings, const MyQSerialPortInfo &port)
{
    bool match = false;

    if (port.hasVendorIdentifier() && port.hasProductIdentifier())
    {
        for (const QJsonValue &value : settings.object().value(QStringLiteral("boards")).toArray())
        {
            QStringList list = value.toObject().value(QStringLiteral("boardVidPid")).toString().split(QStringLiteral(":"));

            if ((list.size() == 2)
            && (port.vendorIdentifier() == list.at(0).toInt(nullptr, 16))
            && (value.toObject().value(QStringLiteral("bootloaderType")).toString() == QStringLiteral("arduino_dfu")))
            {
                QJsonObject bootloaderSettings = value.toObject().value(QStringLiteral("bootloaderSettings")).toObject();

                for (const QJsonValue &value : bootloaderSettings.value(QStringLiteral("touchToResetPids")).toArray())
                {
                    if (port.productIdentifier() == value.toString().toInt(nullptr, 16))
                    {
                        match = true;
                        break;
                    }
                }
            }

            if (match)
            {
                break;
            }
        }
    }

    return match;
}

OpenMVPluginSerialPort_private::OpenMVPluginSerialPort_private(int override_read_timeout,
                                                               int override_read_stall_timeout,
                                                               int override_per_command_wait,
                                                               const QJsonDocument &settings,
                                                               QObject *parent) : QObject(parent)
{
    m_port = Q_NULLPTR;
    m_camera = Q_NULLPTR;
    m_v2ProtocolEnabled = false;
    m_bootloaderStop = false;
    m_override_read_timeout = override_read_timeout;
    m_override_read_stall_timeout = override_read_stall_timeout;
    m_override_per_command_wait = override_per_command_wait;
    m_firmwareSettings = settings;
    m_unstuckWithGetState = false;
    m_readstallQueue = QHash<char, QQueue<qint64> >();
    m_readstallAverage = QHash<char, qint64 >();

    m_idleTimer = new QTimer(this);

    connect(m_idleTimer, &QTimer::timeout, this, [this]() {
        if (m_v2ProtocolEnabled && m_camera) {
            try {
                if (m_camera->isConnected()) {
                    m_camera->pollEvents();
                }
            } catch (...) {
                // ignore
            }
        }
    });
}

OpenMVPluginSerialPort_private::~OpenMVPluginSerialPort_private() {
    if (m_idleTimer) {
        delete m_idleTimer;
        m_idleTimer = Q_NULLPTR;
    }

    if (m_camera) {
        delete m_camera;
        m_camera = Q_NULLPTR;
    }

    if (m_port) {
        delete m_port;
        m_port = Q_NULLPTR;
    }
}

void OpenMVPluginSerialPort_private::enableV2Protocol(bool enable) {
    if (m_v2ProtocolEnabled != enable) {
        if (enable) {
            m_idleTimer->start(1);
        } else {
            m_idleTimer->stop();
        }
    }

    m_v2ProtocolEnabled = enable;

    emit enableV2ProtocolResponse();
}

void OpenMVPluginSerialPort_private::open(const QString &portName) {
    m_readstallQueue = QHash<char, QQueue<qint64> >();
    m_readstallAverage = QHash<char, qint64 >();

    if (m_camera) {
        delete m_camera;
        m_camera = Q_NULLPTR;
    }

    if (m_port) {
        delete m_port;
        m_port = Q_NULLPTR;
    }

    m_port = OMVPortFactory::createPort(portName, this);
    // QSerialPort is buggy unless this is set.
    m_port->setReadBufferSize(READ_BUFFER_SIZE);

    MyQSerialPortInfo arduinoPort(QSerialPortInfo(m_port->portName()));

    int baudRate = OPENMVCAM_BAUD_RATE;
    int baudRate2 = OPENMVCAM_BAUD_RATE_2;

    if (isTouchToReset(m_firmwareSettings, arduinoPort)) {
        baudRate = ARDUINO_TTR_BAUD_RATE;
        baudRate2 = ARDUINO_TTR_BAUD_RATE;
    }

    if ((!m_port->setBaudRate(baudRate))
    || (!m_port->open(QIODevice::ReadWrite))
    || (!m_port->setDataTerminalReady(true))) {
        delete m_port;
        m_port = OMVPortFactory::createPort(portName, this);
        // QSerialPort is buggy unless this is set.
        m_port->setReadBufferSize(READ_BUFFER_SIZE);

        if ((!m_port->setBaudRate(baudRate2))
        || (!m_port->open(QIODevice::ReadWrite))
        || (!m_port->setDataTerminalReady(true))) {
            emit openResult(m_port->errorString());
            delete m_port;
            m_port = Q_NULLPTR;
        }
    }

    if (m_port) {
        if (m_port->hasVIDPID()) {
            QPair<int, int> vidpid = m_port->getVIDPID();

            for (const QJsonValue &v : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray()) {
                QJsonObject o = v.toObject();
                QStringList bVidPid = o.value(QStringLiteral("boardVidPid")).toString().split(QStringLiteral(":"));
                int vid = bVidPid.at(0).toInt(nullptr, 16);
                int pid = bVidPid.at(1).toInt(nullptr, 16);
                int bPidMask = o.value(QStringLiteral("boardPidMask")).toString().toInt(nullptr, 16);

                if ((vid == vidpid.first) && ((pid & bPidMask) == (vidpid.second & bPidMask))) {
                    // We are connected over USB to a valid camera so we do not need ACKs.
                    m_camera = new OMVCamera(m_port, true, true, false, true);
                    break;
                }
            }
        }

        if (!m_camera) {
            bool reliable = m_port->reliableTransport();
            bool fullDuplex = m_port->fullDuplexTransport();
            // Only enable ACKs on unreliable transports.
            m_camera = new OMVCamera(m_port, true, true, !reliable, fullDuplex);
        }

        emit openResult(QString());
    }
}

// V1 protocol
//
// Serial thread only implements the transport layer of the protocol.
// The GUI thread implements the transaction layer.

void OpenMVPluginSerialPort_private::write(const QByteArray &data, int startWait, int stopWait, int timeout)
{
    if(m_port)
    {
        QString portName = m_port->portName();

        for(int i = 0; i < WRITE_LOOPS; i++)
        {
            if(!m_port)
            {
                m_port = OMVPortFactory::createPort(portName, this);
                // QSerialPort is buggy unless this is set.
                m_port->setReadBufferSize(READ_BUFFER_SIZE);

                if((!m_port->setBaudRate(OPENMVCAM_BAUD_RATE))
                || (!m_port->open(QIODevice::ReadWrite))
                || (!m_port->setDataTerminalReady(true)))
                {
                    delete m_port;
                    m_port = OMVPortFactory::createPort(portName, this);
                    // QSerialPort is buggy unless this is set.
                    m_port->setReadBufferSize(READ_BUFFER_SIZE);

                    if((!m_port->setBaudRate(OPENMVCAM_BAUD_RATE_2))
                    || (!m_port->open(QIODevice::ReadWrite))
                    || (!m_port->setDataTerminalReady(true)))
                    {
                        delete m_port;
                        m_port = Q_NULLPTR;
                    }
                }
            }

            if(m_port)
            {
                if(startWait)
                {
                    QThread::msleep(startWait);
                }

                m_port->clearError();

                if(m_port->write(data) != data.size())
                {
                    delete m_port;
                    m_port = Q_NULLPTR;
                }
                else
                {
                    m_port->flush(); // ignore return

                    QElapsedTimer elaspedTimer;
                    elaspedTimer.start();

                    while(m_port->bytesToWrite())
                    {
                        m_port->waitForBytesWritten(1);

                        if(m_port->bytesToWrite() && elaspedTimer.hasExpired(timeout))
                        {
                            break;
                        }
                    }

                    if(m_port->bytesToWrite())
                    {
                        delete m_port;
                        m_port = Q_NULLPTR;
                    }
                    else if(stopWait)
                    {
                        QThread::msleep(stopWait);
                    }
                }
            }

            if(m_port)
            {
                break;
            }

            if (WRITE_DELAY)
            {
                QThread::msleep(WRITE_DELAY);
            }
        }
    }
}

void OpenMVPluginSerialPort_private::command(const OpenMVPluginSerialPortCommand &command)
{
    if(command.m_data.isEmpty())
    {
        if(!command.m_responseLen) // close
        {
            if(m_port)
            {
                delete m_port;
                m_port = Q_NULLPTR;
            }

            emit commandResult(OpenMVPluginSerialPortCommandResult(true, QByteArray()));
        }
        else if(m_port) // learn
        {
            bool ok = false;

            for(int i = LEARN_MTU_MAX; i >= LEARN_MTU_MIN; i /= 2)
            {
                QByteArray learnMTU;
                serializeByte(learnMTU, __USBDBG_CMD);
                serializeByte(learnMTU, __USBDBG_LEARN_MTU);
                serializeLong(learnMTU, i - 1);

                write(learnMTU, LEARN_MTU_START_DELAY, LEARN_MTU_END_DELAY, LEARN_MTU_WRITE_TIMEOUT);

                if(!m_port)
                {
                    break;
                }
                else
                {
                    QByteArray response;
                    QElapsedTimer elaspedTimer;
                    elaspedTimer.start();

                    do
                    {
                        m_port->waitForReadyRead(1);
                        response.append(m_port->readAll());
                    }
                    while((response.size() < (i - 1)) && (!elaspedTimer.hasExpired(LEATN_MTU_READ_TIMEOUT)));

                    if(response.size() >= (i - 1))
                    {
                        QByteArray temp;
                        serializeLong(temp, (i - 1));
                        emit commandResult(OpenMVPluginSerialPortCommandResult(true, temp));
                        ok = true;
                        break;
                    }
                }
            }

            if(!ok)
            {
                if(m_port)
                {
                    delete m_port;
                    m_port = Q_NULLPTR;
                }

                emit commandResult(OpenMVPluginSerialPortCommandResult(false, QByteArray()));
            }
        }
        else
        {
            emit commandResult(OpenMVPluginSerialPortCommandResult(false, QByteArray()));
        }
    }
    else if(m_port)
    {
        if (command.m_readFlushBeforeCommnad)
        {
            QElapsedTimer elaspedTimer;
            elaspedTimer.start();

            do
            {
                m_port->waitForReadyRead(0);

                if(!m_port->readAll().isEmpty())
                {
                    elaspedTimer.restart();
                }
            }
            while(!elaspedTimer.hasExpired(FLUSH_TIMEOUT));
        }

        write(command.m_data, command.m_startWait, command.m_endWait, WRITE_TIMEOUT);

        if((!m_port) || (!command.m_responseLen))
        {
            emit commandResult(OpenMVPluginSerialPortCommandResult(m_port, QByteArray()));
        }
        else
        {
            int read_timeout = m_port->readTimeoutMs();

            if(m_override_read_timeout > 0)
            {
                read_timeout = m_override_read_timeout;
            }

            int read_stall_timeout = m_port->readStallTimeoutMs();

            if(m_override_read_stall_timeout > 0)
            {
                read_stall_timeout = m_override_read_stall_timeout;
            }

            QByteArray response;
            int responseLen = command.m_responseLen;
            QElapsedTimer elaspedTimer;
            elaspedTimer.start();
            #if DYNAMIC_READ_STALL_ENABLE
            qint64 lastReadWait = 0;
            #endif

            bool readStallHappened = false;

            do
            {
                m_port->waitForReadyRead(0);
                #if DYNAMIC_READ_STALL_ENABLE
                lastReadWait = elaspedTimer.elapsed();
                #endif

                QByteArray data = m_port->readAll();
                response.append(data);

                if(!data.isEmpty())
                {
                    elaspedTimer.restart();
                }

                // This code helps clear out read stalls where the OS received the data but then doesn't return it to the application.
                //
                // This happens on windows machines generally.

                #if DYNAMIC_READ_STALL_ENABLE
                if ((m_override_read_stall_timeout <= 0) && command.m_commandAbortOkay)
                {
                    char cmd = command.m_data[1];

                    if ((m_readstallQueue[cmd].size() == DYNAMIC_READ_STALL_BUFFER_SIZE) &&
                        (lastReadWait > (m_readstallAverage[cmd] * DYNAMIC_READ_STALL_THRESHOLD)))
                    {
                        readStallHappened = true;
                        break;
                    }
                }
                #endif

                if((response.size() < responseLen) && elaspedTimer.hasExpired(read_stall_timeout) && command.m_commandAbortOkay)
                {
                    readStallHappened = true;
                    break;
                }

                // DISABLED - NOT REQUIRED - FIXING ZLP OVERLAP WAS WHY THINGS STALL - REMOVE AFTER TRIAL PERIOD
                //
                // if(readStallHappened && (response.size() >= readStallAbaddonSize))
                // {
                //     // The device responsed to the read stall. So, all the data that is going to come has come.
                //     // We may or maynot however actually have a complete response from the command...
                //     response.chop(readStallDiscardSize);
                //     break;
                // }
                //
                // if(m_port->isSerialPort() && (response.size() < responseLen) && elaspedTimer2.hasExpired(read_stall_timeout))
                // {
                //     if(command.m_perCommandWait) // normal mode
                //     {
                //         if (m_unstuckWithGetState)
                //         {
                //             QByteArray data;
                //             serializeByte(data, __USBDBG_CMD);
                //             serializeByte(data, __USBDBG_GET_STATE);
                //             serializeLong(data, GET_STATE_PAYLOAD_LEN);
                //             write(data, GET_STATE_START_DELAY, GET_STATE_END_DELAY, WRITE_TIMEOUT);
                //
                //             if(m_port)
                //             {
                //                 elaspedTimer2.restart();
                //                 if (!readStallHappened) readStallAbaddonSize = response.size();
                //                 readStallHappened = true;
                //                 readStallAbaddonSize += GET_STATE_PAYLOAD_LEN;
                //                 readStallDiscardSize += GET_STATE_PAYLOAD_LEN;
                //             }
                //             else
                //             {
                //                 break;
                //             }
                //         }
                //         else
                //         {
                //             QByteArray data;
                //             serializeByte(data, __USBDBG_CMD);
                //             serializeByte(data, __USBDBG_SCRIPT_RUNNING);
                //             serializeLong(data, SCRIPT_RUNNING_RESPONSE_LEN);
                //             write(data, SCRIPT_RUNNING_START_DELAY, SCRIPT_RUNNING_END_DELAY, WRITE_TIMEOUT);
                //
                //             if(m_port)
                //             {
                //                 elaspedTimer2.restart();
                //                 if (!readStallHappened) readStallAbaddonSize = response.size();
                //                 readStallHappened = true;
                //                 readStallAbaddonSize += SCRIPT_RUNNING_RESPONSE_LEN;
                //                 readStallDiscardSize += SCRIPT_RUNNING_RESPONSE_LEN;
                //             }
                //             else
                //             {
                //                 break;
                //             }
                //         }
                //     }
                //     else // bootloader mode
                //     {
                //         QByteArray data;
                //         serializeLong(data, __BOOTLDR_QUERY);
                //         write(data, BOOTLDR_QUERY_START_DELAY, BOOTLDR_QUERY_END_DELAY, WRITE_TIMEOUT);
                //
                //         if(m_port)
                //         {
                //             elaspedTimer2.restart();
                //             if (!readStallHappened) readStallAbaddonSize = response.size();
                //             readStallHappened = true;
                //             readStallAbaddonSize += BOOTLDR_QUERY_RESPONSE_LEN;
                //             readStallDiscardSize += BOOTLDR_QUERY_RESPONSE_LEN;
                //         }
                //         else
                //         {
                //             break;
                //         }
                //     }
                // }
                //
                // if(m_port->isTCPPort() && (response.size() < responseLen) && elaspedTimer2.hasExpired(read_stall_timeout))
                // {
                //     write(command.m_data, 0, 0, WRITE_TIMEOUT);
                //
                //     if(!m_port)
                //     {
                //         break;
                //     }
                // }
                //
                // DISABLED - NOT REQUIRED - FIXING ZLP OVERLAP WAS WHY THINGS STALL - REMOVE AFTER TRIAL PERIOD
            }
            while((response.size() < responseLen) && (!elaspedTimer.hasExpired(read_timeout)));

            #if DYNAMIC_READ_STALL_ENABLE
            if (command.m_commandAbortOkay && (!readStallHappened))
            {
                char cmd = command.m_data[1];

                m_readstallQueue[cmd].push_back(lastReadWait);

                if(m_readstallQueue[cmd].size() > DYNAMIC_READ_STALL_BUFFER_SIZE)
                {
                    m_readstallQueue[cmd].pop_front();
                }

                qint64 average = 0;

                for(int i = 0; i < m_readstallQueue[cmd].size(); i++)
                {
                    average += m_readstallQueue[cmd].at(i);
                }

                m_readstallAverage[cmd] = average / m_readstallQueue[cmd].size();
            }
            #endif

            if((response.size() >= responseLen) || readStallHappened)
            {
                emit commandResult(OpenMVPluginSerialPortCommandResult(true, response.left(command.m_responseLen)));
            }
            else
            {
                if(m_port)
                {
                    delete m_port;
                    m_port = Q_NULLPTR;
                }

                emit commandResult(OpenMVPluginSerialPortCommandResult(false, QByteArray()));
            }
        }
    }
    else
    {
        emit commandResult(OpenMVPluginSerialPortCommandResult(false, QByteArray()));
    }

    if (command.m_perCommandWait) {
        // Execute commands slowly so as to not overload the OpenMV Cam board.
        int per_command_wait = Utils::HostOsInfo::isMacHost() ? 2 : 1;

        if(m_override_per_command_wait >= 0)
        {
            per_command_wait = m_override_per_command_wait;
        }

        if(per_command_wait > 0)
        {
            QThread::msleep(per_command_wait);
        }
    }
}

void OpenMVPluginSerialPort_private::bootloaderStart(const QString &selectedPort)
{
    m_bootloaderStop = false;

    if(m_port) {
        if (m_v2ProtocolEnabled) {
            if (m_camera) {
                try {
                    if (!m_camera->isConnected()) {
                        m_camera->connect();
                    }

                    QThread::msleep(SYS_RESET_TO_BL_START_DELAY);
                    m_camera->boot();
                    QThread::msleep(SYS_RESET_TO_BL_END_DELAY);
                } catch (...) {
                }
            }
        } else {
            int command = __USBDBG_SYS_RESET;
            QByteArray buffer;
            serializeByte(buffer, __USBDBG_CMD);
            serializeByte(buffer, command);
            serializeLong(buffer, int());
            write(buffer, SYS_RESET_START_DELAY, SYS_RESET_END_DELAY, WRITE_TIMEOUT);
        }

        if (m_camera) {
            delete m_camera;
            m_camera = Q_NULLPTR;
        }

        if(m_port) {
            delete m_port;
            m_port = Q_NULLPTR;
        }
    }

    forever
    {
        QStringList stringList;

        for(const QSerialPortInfo &raw_port : QSerialPortInfo::availablePorts())
        {
            MyQSerialPortInfo port(raw_port);

            if(port.hasVendorIdentifier() && (port.vendorIdentifier() == OPENMVCAM_VID)
            && port.hasProductIdentifier() && (port.productIdentifier() == OPENMVCAM_PID)
            && ((port.serialNumber() == QStringLiteral("000000000010")) ||
                (port.serialNumber() == QStringLiteral("000000000011"))))
            {
                stringList.append(port.portName());
            }
        }

        if(Utils::HostOsInfo::isMacHost())
        {
            stringList = stringList.filter(QStringLiteral("cu"), Qt::CaseInsensitive);
        }

        if(!stringList.isEmpty())
        {
            const QString portName = ((!selectedPort.isEmpty()) && stringList.contains(selectedPort)) ? selectedPort : stringList.first();

            if(Q_UNLIKELY(m_port))
            {
                delete m_port;
            }

            m_port = OMVPortFactory::createPort(portName, this);
            // QSerialPort is buggy unless this is set.
            m_port->setReadBufferSize(READ_BUFFER_SIZE);

            if((!m_port->setBaudRate(OPENMVCAM_BAUD_RATE))
            || (!m_port->open(QIODevice::ReadWrite))
            || (!m_port->setDataTerminalReady(true)))
            {
                delete m_port;
                m_port = OMVPortFactory::createPort(portName, this);
                // QSerialPort is buggy unless this is set.
                m_port->setReadBufferSize(READ_BUFFER_SIZE);

                if((!m_port->setBaudRate(OPENMVCAM_BAUD_RATE_2))
                || (!m_port->open(QIODevice::ReadWrite))
                || (!m_port->setDataTerminalReady(true)))
                {
                    delete m_port;
                    m_port = Q_NULLPTR;
                }
            }

            if(m_port)
            {
                bool hs = MyQSerialPortInfo(QSerialPortInfo(m_port->portName())).serialNumber() == QStringLiteral("000000000010");

                QByteArray buffer;
                serializeLong(buffer, __BOOTLDR_START);
                write(buffer, BOOTLDR_START_START_DELAY, BOOTLDR_START_END_DELAY, BOOTLOADER_WRITE_TIMEOUT);

                if(m_port)
                {
                    QByteArray response;
                    int responseLen = BOOTLDR_START_RESPONSE_LEN;
                    QElapsedTimer elaspedTimer;
                    QElapsedTimer elaspedTimer2;
                    elaspedTimer.start();
                    elaspedTimer2.start();

                    do
                    {
                        m_port->waitForReadyRead(1);
                        response.append(m_port->readAll());

                        if((response.size() < responseLen) && elaspedTimer2.hasExpired(BOOTLOADER_READ_STALL_TIMEOUT))
                        {
                            QByteArray data;
                            serializeLong(data, __BOOTLDR_START);
                            write(data, BOOTLDR_START_START_DELAY, BOOTLDR_START_END_DELAY, BOOTLOADER_WRITE_TIMEOUT);

                            if(m_port)
                            {
                                responseLen += BOOTLDR_START_RESPONSE_LEN;
                                elaspedTimer2.restart();
                            }
                            else
                            {
                                break;
                            }
                        }
                    }
                    while((response.size() < responseLen) && (!elaspedTimer.hasExpired(BOOTLOADER_READ_TIMEOUT)));

                    if(response.size() >= responseLen)
                    {
                        int result = deserializeLong(response);

                        if((result == V1_BOOTLDR)
                        || (result == V2_BOOTLDR)
                        || (result == V3_BOOTLDR))
                        {
                            emit bootloaderStartResponse(true, result, hs);
                            return;
                        }
                    }

                    if(m_port)
                    {
                        delete m_port;
                        m_port = Q_NULLPTR;
                    }
                }
            }
        }

        QCoreApplication::processEvents();

        if(m_bootloaderStop)
        {
            emit bootloaderStartResponse(false, int(), false);
            return;
        }
    }
}

void OpenMVPluginSerialPort_private::bootloaderStop()
{
    m_bootloaderStop = true;
    emit bootloaderStopResponse();
}

void OpenMVPluginSerialPort_private::bootloaderReset()
{
    m_bootloaderStop = false;
    emit bootloaderResetResponse();
}

// V2 protocol
//
// Serial thread implements the transport and transaction layer of the protocol.

void OpenMVPluginSerialPort_private::getFirmwareVersion() {
    if (!m_camera) {
        emit firmwareVersion(true, 0, 0, 0);
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        QVariantList v = m_camera->cachedSystemInfo().
                         value(QStringLiteral("firmware_version")).toList();

        if (v.size() == 3) {
            emit firmwareVersion(false, v.at(0).toInt(), v.at(1).toInt(), v.at(2).toInt());
        } else {
            emit firmwareVersion(false, 0, 0, 0);
        }
    } catch (...) {
        emit firmwareVersion(true, 0, 0, 0);
    }
}

void OpenMVPluginSerialPort_private::frameDump() {
    if (!m_camera) {
        emit frameBufferData(true, QPixmap());
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        OMVFrame frame;

        if (m_camera->readFrame(frame)) {
            emit frameBufferData(false, frame.pixmap);
        } else {
            emit frameBufferData(false, QPixmap());
        }
    } catch (...) {
        emit frameBufferData(true, QPixmap());
    }
}

void OpenMVPluginSerialPort_private::getArchString() {
    if (!m_camera || !m_port) {
        emit archString(true, QString());
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        QString boardArchString;
        QString boardType;
        int usb_vid = m_camera->cachedSystemInfo().value(QStringLiteral("usb_vid")).toUInt();
        int usb_pid = m_camera->cachedSystemInfo().value(QStringLiteral("usb_pid")).toUInt();

        for (const QJsonValue &v : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray()) {
            QJsonObject o = v.toObject();
            QStringList bVidPid = o.value(QStringLiteral("boardVidPid")).toString().split(QStringLiteral(":"));
            int vid = bVidPid.at(0).toInt(nullptr, 16);
            int pid = bVidPid.at(1).toInt(nullptr, 16);
            int bPidMask = o.value(QStringLiteral("boardPidMask")).toString().toInt(nullptr, 16);

            if ((vid == usb_vid) && ((pid & bPidMask) == (usb_pid & bPidMask))) {
                boardArchString = o.value(QStringLiteral("boardArchString")).toString();
                boardType = o.value(QStringLiteral("boardType")).toString();
                break;
            }
        }

        if ((boardArchString.isEmpty() || boardType.isEmpty()) && m_port->hasVIDPID()) {
            QPair<int, int> vidpid = m_port->getVIDPID();

            for (const QJsonValue &v : m_firmwareSettings.object().value(QStringLiteral("boards")).toArray()) {
                QJsonObject o = v.toObject();
                QStringList bVidPid = o.value(QStringLiteral("boardVidPid")).toString().split(QStringLiteral(":"));
                int vid = bVidPid.at(0).toInt(nullptr, 16);
                int pid = bVidPid.at(1).toInt(nullptr, 16);
                int bPidMask = o.value(QStringLiteral("boardPidMask")).toString().toInt(nullptr, 16);

                if ((vid == vidpid.first) && ((pid & bPidMask) == (vidpid.second & bPidMask))) {
                    boardArchString = o.value(QStringLiteral("boardArchString")).toString();
                    boardType = o.value(QStringLiteral("boardType")).toString();
                    break;
                }
            }
        }

        QVariantList v = m_camera->cachedSystemInfo().
                         value(QStringLiteral("device_id")).toList();

        if ((!boardArchString.isEmpty()) && (!boardType.isEmpty()) && (v.size() == 3)) {
            emit archString(false, QString(QStringLiteral("%1 [%2:%3%4%5]")).
                arg(boardArchString, boardType,
                    QString::number(v.at(0).toUInt(), 16).rightJustified(8, QChar('0')).toUpper(),
                    QString::number(v.at(1).toUInt(), 16).rightJustified(8, QChar('0')).toUpper(),
                    QString::number(v.at(2).toUInt(), 16).rightJustified(8, QChar('0')).toUpper()));
        } else {
            emit archString(false, QString());
        }
    } catch (...) {
        emit archString(true, QString());
    }
}

void OpenMVPluginSerialPort_private::scriptExec(const QByteArray &data) {
    if (!m_camera) {
        emit scriptExecDone(true);
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        QThread::msleep(SCRIPT_EXEC_START_DELAY);
        m_camera->exec(QString::fromUtf8(data));
        QThread::msleep(SCRIPT_EXEC_2_END_DELAY);
        m_camera->pollEvents();

        emit scriptExecDone(false);
    } catch (...) {
        emit scriptExecDone(true);
    }
}

void OpenMVPluginSerialPort_private::scriptStop() {
    if (!m_camera) {
        emit scriptStopDone(true);
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        if (m_camera->scriptRunning(true)) {
            QThread::msleep(SCRIPT_STOP_START_DELAY);
            m_camera->stop();
            QThread::msleep(SCRIPT_STOP_END_DELAY);
            m_camera->pollEvents();
        }

        emit scriptStopDone(false);
    } catch (...) {
        emit scriptStopDone(true);
    }
}

void OpenMVPluginSerialPort_private::getScriptRunning() {
    if (!m_camera) {
        emit scriptRunning(true, false);
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        emit scriptRunning(false, m_camera->scriptRunning());
    } catch (...) {
        emit scriptRunning(true, false);
    }
}

void OpenMVPluginSerialPort_private::sysReset(bool enterBootloader) {
    if (!m_camera) {
        emit sysResetDone(true);
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        if (enterBootloader) {
            m_camera->boot();
        } else {
            m_camera->reset();
        }

        emit sysResetDone(false);
    } catch (...) {
        emit sysResetDone(true);
    }
}

void OpenMVPluginSerialPort_private::fbEnable(bool enable) {
    if (!m_camera) {
        emit fbEnableDone(true);
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        m_camera->streaming(enable, m_camera->rawStreamingState(), m_camera->streamingResolution());
        emit fbEnableDone(false);
    } catch (...) {
        emit fbEnableDone(true);
    }
}

void OpenMVPluginSerialPort_private::jpegEnable(bool enable) {
    if (!m_camera) {
        emit jpegEnableDone(true);
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        m_camera->streaming(m_camera->streamingEnabledState(), !enable, m_camera->streamingResolution());
        emit jpegEnableDone(false);
    } catch (...) {
        emit jpegEnableDone(true);
    }
}

void OpenMVPluginSerialPort_private::getTxBuffer() {
    if (!m_camera) {
        emit printData(true, QByteArray());
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        QString s = m_camera->readStdout();
        emit printData(false, s.toUtf8());
    } catch (...) {
        emit printData(true, QByteArray());
    }
}

void OpenMVPluginSerialPort_private::sensorId() {
    if (!m_camera) {
        emit sensorIdDone(true, QList<int>());
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        QList<int> sensorIds;
        for (const QVariant &item : m_camera->cachedSystemInfo().
                                    value(QStringLiteral("sensor_chip_id")).toList()) {
            if (item.toInt()) sensorIds.append(item.toInt());
        }

        emit sensorIdDone(false, sensorIds);
    } catch (...) {
        emit sensorIdDone(true, QList<int>());
    }
}

void OpenMVPluginSerialPort_private::getState() {
    if (!m_camera) {
        emit getStateDone(true, false, false, false, QByteArray(), QPixmap());
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        bool profileEnabled = m_camera->hasChannel(QStringLiteral("profile"));
        bool hasPMU = m_camera->cachedSystemInfo().value(QStringLiteral("pmu_present")).toBool();

        QString s = m_camera->readStdout();
        QPair<bool, bool> status = m_camera->frameReadyAndScriptRunning();

        OMVFrame frame;
        bool frameValid = status.first && m_camera->readFrame(frame);

        emit getStateDone(false,
                          status.second,
                          profileEnabled,
                          profileEnabled && hasPMU,
                          s.toUtf8(),
                          frameValid ? frame.pixmap : QPixmap());
    } catch (...) {
        emit getStateDone(true, false, false, false, QByteArray(), QPixmap());
    }
}

void OpenMVPluginSerialPort_private::readProfile() {
    if (!m_camera) {
        emit readProfileDone(true, QList<profile_record_t>());
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        QList<profile_record_t> records;

        for (const QVariant &v : m_camera->readProfile()) {
            profile_record_t r;
            r.address = v.toMap().value(QStringLiteral("address")).toUInt();
            r.caller = v.toMap().value(QStringLiteral("caller")).toUInt();
            r.call_count = v.toMap().value(QStringLiteral("call_count")).toUInt();
            r.min_ticks = v.toMap().value(QStringLiteral("min_ticks")).toUInt();
            r.max_ticks = v.toMap().value(QStringLiteral("max_ticks")).toUInt();
            r.total_ticks = v.toMap().value(QStringLiteral("total_ticks")).toULongLong();
            r.total_cycles = v.toMap().value(QStringLiteral("total_cycles")).toULongLong();

            for (const QVariant &v2 : v.toMap().value(QStringLiteral("events")).toList()) {
                r.events.append(v2.toULongLong());
            }

            records.append(r);
        }

        emit readProfileDone(false, records);
    } catch (...) {
        emit readProfileDone(true, QList<profile_record_t>());
    }
}

void OpenMVPluginSerialPort_private::setProfileMode(int mode) {
    if (!m_camera) {
        emit setProfileModeDone(true);
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        m_camera->profilerMode(mode);
        emit setProfileModeDone(false);
    } catch (...) {
        emit setProfileModeDone(true);
    }
}

void OpenMVPluginSerialPort_private::setEventCounter(int event_num, int event_type) {
    if (!m_camera) {
        emit setEventCounterDone(true);
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        m_camera->profilerEventType(event_num, event_type);
        emit setEventCounterDone(false);
    } catch (...) {
        emit setEventCounterDone(true);
    }
}

void OpenMVPluginSerialPort_private::profileReset() {
    if (!m_camera) {
        emit profileResetDone(true);
        return;
    }

    try {
        if (!m_camera->isConnected()) {
            m_camera->connect();
        }

        m_camera->profilerReset();
        emit profileResetDone(false);
    } catch (...) {
        emit profileResetDone(true);
    }
}

void OpenMVPluginSerialPort_private::close() {
    if (!m_camera) {
        emit closeResponse(true);
        return;
    }

    try {
        if (m_camera->isConnected()) {
            m_camera->disconnect();
        }

        emit closeResponse(false);
    } catch (...) {
        emit closeResponse(true);
    }
}

OpenMVPluginSerialPort::OpenMVPluginSerialPort(int override_read_timeout,
                                               int override_read_stall_timeout,
                                               int override_per_command_wait,
                                               const QJsonDocument &settings,
                                               QObject *parent) : QObject(parent)
{
    m_thread = new QThread;
    m_port = new OpenMVPluginSerialPort_private(override_read_timeout,
                                                override_read_stall_timeout,
                                                override_per_command_wait,
                                                settings);
    m_port->moveToThread(m_thread);

    // Shared

    connect(this, &OpenMVPluginSerialPort::enableV2Protocol,
            m_port, &OpenMVPluginSerialPort_private::enableV2Protocol);

    connect(m_port, &OpenMVPluginSerialPort_private::enableV2ProtocolResponse,
            this, &OpenMVPluginSerialPort::enableV2ProtocolResponse);

    connect(this, &OpenMVPluginSerialPort::open,
            m_port, &OpenMVPluginSerialPort_private::open);

    connect(m_port, &OpenMVPluginSerialPort_private::openResult,
            this, &OpenMVPluginSerialPort::openResult);

    // V1 protocol
    //
    // Serial thread only implements the transport layer of the protocol.
    // The GUI thread implements the transaction layer.

    connect(this, &OpenMVPluginSerialPort::command,
            m_port, &OpenMVPluginSerialPort_private::command);

    connect(m_port, &OpenMVPluginSerialPort_private::commandResult,
            this, &OpenMVPluginSerialPort::commandResult);

    connect(this, &OpenMVPluginSerialPort::bootloaderStart,
            m_port, &OpenMVPluginSerialPort_private::bootloaderStart);

    connect(this, &OpenMVPluginSerialPort::bootloaderStop,
            m_port, &OpenMVPluginSerialPort_private::bootloaderStop);

    connect(this, &OpenMVPluginSerialPort::bootloaderReset,
            m_port, &OpenMVPluginSerialPort_private::bootloaderReset);

    connect(m_port, &OpenMVPluginSerialPort_private::bootloaderStartResponse,
            this, &OpenMVPluginSerialPort::bootloaderStartResponse);

    connect(m_port, &OpenMVPluginSerialPort_private::bootloaderStopResponse,
            this, &OpenMVPluginSerialPort::bootloaderStopResponse);

    connect(m_port, &OpenMVPluginSerialPort_private::bootloaderResetResponse,
            this, &OpenMVPluginSerialPort::bootloaderResetResponse);

    connect(this, &OpenMVPluginSerialPort::updateSettings,
            m_port, &OpenMVPluginSerialPort_private::updateSettings);

    connect(m_port, &OpenMVPluginSerialPort_private::settingsUpdated,
            this, &OpenMVPluginSerialPort::settingsUpdated);

    // V2 protocol
    //
    // Serial thread implements the transport and transaction layer of the protocol.

    connect(this, &OpenMVPluginSerialPort::getFirmwareVersion,
            m_port, &OpenMVPluginSerialPort_private::getFirmwareVersion);

    connect(m_port, &OpenMVPluginSerialPort_private::firmwareVersion,
            this, &OpenMVPluginSerialPort::firmwareVersion);

    connect(this, &OpenMVPluginSerialPort::frameDump,
            m_port, &OpenMVPluginSerialPort_private::frameDump);

    connect(m_port, &OpenMVPluginSerialPort_private::frameBufferData,
            this, &OpenMVPluginSerialPort::frameBufferData);

    connect(this, &OpenMVPluginSerialPort::getArchString,
            m_port, &OpenMVPluginSerialPort_private::getArchString);

    connect(m_port, &OpenMVPluginSerialPort_private::archString,
            this, &OpenMVPluginSerialPort::archString);

    connect(this, &OpenMVPluginSerialPort::scriptExec,
            m_port, &OpenMVPluginSerialPort_private::scriptExec);

    connect(m_port, &OpenMVPluginSerialPort_private::scriptExecDone,
            this, &OpenMVPluginSerialPort::scriptExecDone);

    connect(this, &OpenMVPluginSerialPort::scriptStop,
            m_port, &OpenMVPluginSerialPort_private::scriptStop);

    connect(m_port, &OpenMVPluginSerialPort_private::scriptStopDone,
            this, &OpenMVPluginSerialPort::scriptStopDone);

    connect(this, &OpenMVPluginSerialPort::getScriptRunning,
            m_port, &OpenMVPluginSerialPort_private::getScriptRunning);

    connect(m_port, &OpenMVPluginSerialPort_private::scriptRunning,
            this, &OpenMVPluginSerialPort::scriptRunning);

    connect(this, &OpenMVPluginSerialPort::sysReset,
            m_port, &OpenMVPluginSerialPort_private::sysReset);

    connect(m_port, &OpenMVPluginSerialPort_private::sysResetDone,
            this, &OpenMVPluginSerialPort::sysResetDone);

    connect(this, &OpenMVPluginSerialPort::fbEnable,
            m_port, &OpenMVPluginSerialPort_private::fbEnable);

    connect(m_port, &OpenMVPluginSerialPort_private::fbEnableDone,
            this, &OpenMVPluginSerialPort::fbEnableDone);

    connect(this, &OpenMVPluginSerialPort::jpegEnable,
            m_port, &OpenMVPluginSerialPort_private::jpegEnable);

    connect(m_port, &OpenMVPluginSerialPort_private::jpegEnableDone,
            this, &OpenMVPluginSerialPort::jpegEnableDone);

    connect(this, &OpenMVPluginSerialPort::getTxBuffer,
            m_port, &OpenMVPluginSerialPort_private::getTxBuffer);

    connect(m_port, &OpenMVPluginSerialPort_private::printData,
            this, &OpenMVPluginSerialPort::printData);

    connect(this, &OpenMVPluginSerialPort::sensorId,
            m_port, &OpenMVPluginSerialPort_private::sensorId);

    connect(m_port, &OpenMVPluginSerialPort_private::sensorIdDone,
            this, &OpenMVPluginSerialPort::sensorIdDone);

    connect(this, &OpenMVPluginSerialPort::getState,
            m_port, &OpenMVPluginSerialPort_private::getState);

    connect(m_port, &OpenMVPluginSerialPort_private::getStateDone,
            this, &OpenMVPluginSerialPort::getStateDone);

    connect(this, &OpenMVPluginSerialPort::readProfile,
            m_port, &OpenMVPluginSerialPort_private::readProfile);

    connect(m_port, &OpenMVPluginSerialPort_private::readProfileDone,
            this, &OpenMVPluginSerialPort::readProfileDone);

    connect(this, &OpenMVPluginSerialPort::setProfileMode,
            m_port, &OpenMVPluginSerialPort_private::setProfileMode);

    connect(m_port, &OpenMVPluginSerialPort_private::setProfileModeDone,
            this, &OpenMVPluginSerialPort::setProfileModeDone);

    connect(this, &OpenMVPluginSerialPort::setEventCounter,
            m_port, &OpenMVPluginSerialPort_private::setEventCounter);

    connect(m_port, &OpenMVPluginSerialPort_private::setEventCounterDone,
            this, &OpenMVPluginSerialPort::setEventCounterDone);

    connect(this, &OpenMVPluginSerialPort::profileReset,
            m_port, &OpenMVPluginSerialPort_private::profileReset);

    connect(m_port, &OpenMVPluginSerialPort_private::profileResetDone,
            this, &OpenMVPluginSerialPort::profileResetDone);

    connect(this, &OpenMVPluginSerialPort::close,
            m_port, &OpenMVPluginSerialPort_private::close);

    connect(m_port, &OpenMVPluginSerialPort_private::closeResponse,
            this, &OpenMVPluginSerialPort::closeResponse);

    // Cleanup

    connect(this, &OpenMVPluginSerialPort::destroyed,
            m_port, &OpenMVPluginSerialPort_private::deleteLater);

    connect(m_port, &OpenMVPluginSerialPort_private::destroyed,
            m_thread, &QThread::quit);

    connect(m_thread, &QThread::finished,
            m_thread, &QThread::deleteLater);

    m_thread->start();
}

void OpenMVPluginSerialPort::terminate()
{
    m_thread->terminate();
}

} // namespace Internal
} // namespace OpenMV
