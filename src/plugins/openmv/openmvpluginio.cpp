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

#include "openmvpluginio.h"
#include "protocol/omv_constants.h"

#include "protocol/omv_crc.h"

#define MTU_DEFAULT_SIZE (1024 * 1024 * 1024)

namespace OpenMV {
namespace Internal {

enum
{
    CHECK_PROTOCOL_VERSION_CPL,
    CHECK_PROTOCOL_VERSION_CPL_SPLIT,
    USBDBG_FW_VERSION_CPL,
    USBDBG_FRAME_SIZE_CPL,
    USBDBG_FRAME_DUMP_CPL,
    USBDBG_FRAME_DUMP_UNLOCK_CPL,
    USBDBG_ARCH_STR_CPL,
    USBDBG_LEARN_MTU_CPL,
    USBDBG_SCRIPT_EXEC_CPL_0,
    USBDBG_SCRIPT_EXEC_CPL_1,
    USBDBG_SCRIPT_STOP_CPL,
    USBDBG_SCRIPT_RUNNING_CPL,
    USBDBG_SCRIPT_SAVE_CPL,
    USBDBG_TEMPLATE_SAVE_CPL_0,
    USBDBG_TEMPLATE_SAVE_CPL_1,
    USBDBG_DESCRIPTOR_SAVE_CPL_0,
    USBDBG_DESCRIPTOR_SAVE_CPL_1,
    USBDBG_ATTR_READ_CPL,
    USBDBG_ATTR_READ_CPL_0,
    USBDBG_ATTR_READ_CPL_1,
    USBDBG_ATTR_WRITE_CPL,
    USBDBG_ATTR_WRITE_CPL_0,
    USBDBG_ATTR_WRITE_CPL_1,
    USBDBG_SYS_RESET_CPL,
    USBDBG_FB_ENABLE_CPL,
    USBDBG_FB_ENABLE_CPL_0,
    USBDBG_FB_ENABLE_CPL_1,
    USBDBG_JPEG_ENABLE_CPL,
    USBDBG_JPEG_ENABLE_CPL_0,
    USBDBG_JPEG_ENABLE_CPL_1,
    USBDBG_TX_BUF_LEN_CPL,
    USBDBG_TX_BUF_CPL,
    USBDBG_SENSOR_ID_CPL,
    USBDBG_TX_INPUT_CPL_0,
    USBDBG_TX_INPUT_CPL_1,
    USBDBG_TIME_INPUT_CPL_0,
    USBDBG_TIME_INPUT_CPL_1,
    USBDBG_GET_STATE_CPL,
    USBDBG_PROFILE_SIZE_CPL,
    USBDBG_PROFILE_DUMP_CPL,
    USBDBG_SET_PROFILE_MODE_0_CPL,
    USBDBG_SET_PROFILE_MODE_1_CPL,
    USBDBG_SET_EVT_CNTR_0_CPL,
    USBDBG_SET_EVT_CNTR_1_CPL,
    USBDBG_PROFILE_RESET_CPL,
    BOOTLDR_START_CPL,
    BOOTLDR_RESET_CPL,
    BOOTLDR_ERASE_CPL,
    BOOTLDR_WRITE_CPL,
    BOOTLDR_QUERY_CPL,
    BOOTLDR_QSPIF_ERASE_CPL,
    BOOTLDR_QSPIF_WRITE_CPL,
    BOOTLDR_QSPIF_LAYOUT_CPL,
    BOOTLDR_QSPIF_MEMTEST_CPL,
    CLOSE_CPL,
    V2_SYSTEM_INFO_STRING_CPL,
    V2_HOST_STATS_STRING_CPL,
    V2_DEVICE_STATS_STRING_CPL,
    V2_FIRMWARE_VERSION_CPL,
    V2_FRAME_BUFFER_DATA_CPL,
    V2_ARCH_STRING_CPL,
    V2_SCRIPT_EXEC_CPL,
    V2_SCRIPT_STOP_CPL,
    V2_SCRIPT_RUNNING_CPL,
    V2_SYSTEM_RESET_CPL,
    V2_FRAME_BUFFER_ENABLE_CPL,
    V2_JPEG_ENABLE_CPL,
    V2_PRINT_DATA_CPL,
    V2_SENSOR_ID_CPL,
    V2_GET_STATE_CPL,
    V2_PROFILE_DATA_CPL,
    V2_SET_PROFILE_MODE_CPL,
    V2_SET_EVENT_COUNTER_CPL,
    V2_PROFILE_RESET_CPL,
    V2_CLOSE_CPL,
};

static QByteArray byteSwap(QByteArray buffer, bool ok)
{
    if(ok)
    {
        for(int i = 0, j = (buffer.size() / 2) * 2; i < j; i += 2)
        {
            char temp = buffer.data()[i];
            buffer.data()[i] = buffer.data()[i+1];
            buffer.data()[i+1] = temp;
        }
    }

    return buffer;
}

int getImageSize(int w, int h, int bpp, bool newPixformat, int pixformat)
{
    if(newPixformat)
    {
        switch(pixformat)
        {
            case PIXFORMAT_BINARY:
            {
                return ((w + 31) / 32) * 4 * h;
            }
            case PIXFORMAT_GRAYSCALE:
            case PIXFORMAT_BAYER_ANY: // re-use
            {
                return w * h * 1;
            }
            case PIXFORMAT_RGB565:
            case PIXFORMAT_YUV_ANY: // re-use
            {
                return w * h * 2;
            }
            case PIXFORMAT_COMPRESSED_ANY:
            {
                switch(bpp)
                {
                    case PIXFORMAT_BPP_BINARY:
                    {
                        return ((w + 31) / 32) * 4 * h;
                    }
                    case PIXFORMAT_BPP_GRAY8:
                    {
                        return w * h * 1;
                    }
                    case PIXFORMAT_BPP_RGB565:
                    {
                        return w * h * 2;
                    }
                    default:
                    {
                        return bpp;
                    }
                }
            }
            default:
            {
                return int();
            }
        }
    }
    else
    {
        return OLD_IS_JPG(bpp) ? bpp :
            (OLD_IS_BAYER(bpp) ? (w * h) :
                ((OLD_IS_RGB(bpp) || OLD_IS_GS(bpp)) ? (w * h * bpp) :
                    (OLD_IS_BINARY(bpp) ? (((w + 31) / 32) * 4 * h) : int())));
    }
}

QPixmap getImageFromData(QByteArray data, int w, int h, int bpp, bool rgb565ByteReversed, bool newPixformat, int pixformat)
{
    QPixmap pixmap;

    if(newPixformat)
    {
        switch(pixformat)
        {
            case PIXFORMAT_BINARY:
            {
                pixmap = QPixmap::fromImage(QImage(reinterpret_cast<const uchar *>(data.constData()), w, h, ((w + 31) / 32) * 4, QImage::Format_MonoLSB));
                break;
            }
            case PIXFORMAT_GRAYSCALE:
            {
                pixmap = QPixmap::fromImage(QImage(reinterpret_cast<const uchar *>(data.constData()), w, h, w * 1, QImage::Format_Grayscale8));
                break;
            }
            case PIXFORMAT_RGB565:
            {
                pixmap = QPixmap::fromImage(QImage(reinterpret_cast<const uchar *>(byteSwap(data, rgb565ByteReversed).constData()), w, h, w * 2, QImage::Format_RGB16));
                break;
            }
            case PIXFORMAT_COMPRESSED_ANY:
            {
                switch(bpp)
                {
                    case PIXFORMAT_BPP_BINARY:
                    {
                        pixmap = QPixmap::fromImage(QImage(reinterpret_cast<const uchar *>(data.constData()), w, h, ((w + 31) / 32) * 4, QImage::Format_MonoLSB));
                        break;
                    }
                    case PIXFORMAT_BPP_GRAY8:
                    {
                        pixmap = QPixmap::fromImage(QImage(reinterpret_cast<const uchar *>(data.constData()), w, h, w * 1, QImage::Format_Grayscale8));
                        break;
                    }
                    case PIXFORMAT_BPP_RGB565:
                    {
                        pixmap = QPixmap::fromImage(QImage(reinterpret_cast<const uchar *>(byteSwap(data, rgb565ByteReversed).constData()), w, h, w * 2, QImage::Format_RGB16));
                        break;
                    }
                    default:
                    {
                        pixmap = QPixmap::fromImage(QImage::fromData(data));
                        break;
                    }
                }
            }
            default:
            {
                break;
            }
        }
    }
    else if (!OLD_IS_BAYER(bpp))
    {
        pixmap = getImageSize(w, h, bpp, false, 0) ? (QPixmap::fromImage(OLD_IS_JPG(bpp)
            ? QImage::fromData(data)
            : QImage(reinterpret_cast<const uchar *>(byteSwap(data,
                rgb565ByteReversed && OLD_IS_RGB(bpp)).constData()), w, h, OLD_IS_BINARY(bpp) ? (((w + 31) / 32) * 4) : (w * bpp),
                OLD_IS_RGB(bpp) ? QImage::Format_RGB16 : (OLD_IS_GS(bpp) ? QImage::Format_Grayscale8 : (OLD_IS_BINARY(bpp) ? QImage::Format_MonoLSB : QImage::Format_Invalid)))))
        : QPixmap();
    }

    if(pixmap.isNull() && (newPixformat ? (pixformat & PIXFORMAT_FLAGS_J) : OLD_IS_JPG(bpp)))
    {
        data = data.mid(1, data.size() - 2);

        int size = data.size();
        QByteArray temp;

        for(int i = 0, j = (size / 4) * 4; i < j; i += 4)
        {
            int x = 0;
            x |= (data.at(i + 0) & 0x3F) << 0;
            x |= (data.at(i + 1) & 0x3F) << 6;
            x |= (data.at(i + 2) & 0x3F) << 12;
            x |= (data.at(i + 3) & 0x3F) << 18;
            temp.append((x >> 0) & 0xFF);
            temp.append((x >> 8) & 0xFF);
            temp.append((x >> 16) & 0xFF);
        }

        if((size % 4) == 3) // 2 bytes -> 16-bits -> 24-bits sent
        {
            int x = 0;
            x |= (data.at(size - 3) & 0x3F) << 0;
            x |= (data.at(size - 2) & 0x3F) << 6;
            x |= (data.at(size - 1) & 0x0F) << 12;
            temp.append((x >> 0) & 0xFF);
            temp.append((x >> 8) & 0xFF);
        }

        if((size % 4) == 2) // 1 byte -> 8-bits -> 16-bits sent
        {
            int x = 0;
            x |= (data.at(size - 2) & 0x3F) << 0;
            x |= (data.at(size - 1) & 0x03) << 6;
            temp.append((x >> 0) & 0xFF);
        }

        pixmap = QPixmap::fromImage(QImage::fromData(temp));
    }

    return pixmap;
}

OpenMVPluginIO::OpenMVPluginIO(OpenMVPluginSerialPort *port, QObject *parent) : QObject(parent)
{
    m_port = port;

    connect(m_port, &OpenMVPluginSerialPort::commandResult,
            this, &OpenMVPluginIO::commandResult);

    m_postedQueue = QQueue<OpenMVPluginSerialPortCommand>();
    m_completionQueue = QQueue<int>();
    m_frameSizeW = int();
    m_frameSizeH = int();
    m_frameSizeBPP = int();
    m_record_count = int();
    m_record_size = int();
    m_event_count = int();
    m_mtu = MTU_DEFAULT_SIZE;
    m_pixelBuffer = QByteArray();
    m_lineBuffer = QByteArray();
    m_timeout = bool();
    m_breakUpGetAttributeCommand = bool();
    m_breakUpSetAttributeCommand = bool();
    m_breakUpFBEnable = bool();
    m_breakUpJPEGEnable = bool();
    m_rgb565ByteReversed = bool();
    m_newPixformat = bool();
    m_mainTerminalInput = bool();
    m_bootloaderHS = bool();
    m_bootloaderFastMode = bool();
    m_hsOn = bool();
    m_getStateVariableSize = bool();
    m_profileEnabled = bool();
    m_hasPMU = bool();
    m_archString = QString();
    m_firmwareMajor = int();
    m_firmwareMinor = int();
    m_firmwarePatch = int();
    m_sensorID = uint();
    m_sentPackets = uint();
    m_receivedPackets = uint();
    m_receivedImages = uint();

    //////////////
    // V2 protocol
    //////////////

    m_v2ProtocolEnabled = bool();

    connect(m_port, &OpenMVPluginSerialPort::enableV2ProtocolResponse,
            this, &OpenMVPluginIO::protocolVersionDone);

    connect(m_port, &OpenMVPluginSerialPort::systemInfoString,
            this, [this] (bool timeout, const QString &info) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_SYSTEM_INFO_STRING_CPL);
                emit systemInfoString(info);
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::hostStatsString,
            this, [this] (bool timeout, const QString &info) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_HOST_STATS_STRING_CPL);
                emit hostStatsString(info);
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::deviceStatsString,
            this, [this] (bool timeout, const QString &info) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_DEVICE_STATS_STRING_CPL);
                emit deviceStatsString(info);
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::firmwareVersion,
            this, [this] (bool timeout, int major, int minor, int patch) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_FIRMWARE_VERSION_CPL);
                emit firmwareVersion(major, minor, patch);
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::frameBufferData,
            this, [this] (bool timeout, const QPixmap &data) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_FRAME_BUFFER_DATA_CPL);
                bool null = data.isNull();
                if (!null) emit frameBufferData(data);
                emit frameBufferEmpty(null);
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::archString,
            this, [this] (bool timeout, const QString &arch) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_ARCH_STRING_CPL);
                emit archString(arch);
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::scriptExecDone,
            this, [this] (bool timeout) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_SCRIPT_EXEC_CPL);
                emit scriptExecDone();
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::scriptStopDone,
            this, [this] (bool timeout) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_SCRIPT_STOP_CPL);
                emit scriptStopDone();
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::scriptRunning,
            this, [this] (bool timeout, bool running) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_SCRIPT_RUNNING_CPL);
                emit scriptRunning(running);
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::sysResetDone,
            this, [this] (bool timeout) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_SYSTEM_RESET_CPL);
                emit sysResetDone();
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::fbEnableDone,
            this, [this] (bool timeout) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_FRAME_BUFFER_ENABLE_CPL);
                emit fbEnableDone();
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::jpegEnableDone,
            this, [this] (bool timeout) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_JPEG_ENABLE_CPL);
                emit jpegEnableDone();
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::printData,
            this, [this] (bool timeout, const QByteArray &data) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_PRINT_DATA_CPL);

                if (data.size()) {
                    m_lineBuffer.append(QByteArray(data).append('\0').split(0).takeFirst());
                    doTxBufCpl();
                } else if (m_lineBuffer.size()) {
                    emit printData(pasrsePrintData(m_lineBuffer));
                    m_lineBuffer.clear();
                    emit printEmpty(true);
                } else {
                    emit printEmpty(true);
                }

                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::sensorIdDone,
            this, [this] (bool timeout, QList<int> ids) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_SENSOR_ID_CPL);
                emit sensorIdDone(ids);
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::getStateDone,
            this, [this] (bool timeout, bool running, bool profileEnabled, bool hasPMU,
                          const QByteArray &data, const QPixmap &img) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_GET_STATE_CPL);
                emit scriptRunning(running);

                if (data.size()) {
                    m_lineBuffer.append(QByteArray(data).append('\0').split(0).takeFirst());
                    doTxBufCpl();
                } else if (m_lineBuffer.size()) {
                    emit printData(pasrsePrintData(m_lineBuffer));
                    m_lineBuffer.clear();
                    emit printEmpty(true);
                } else {
                    emit printEmpty(true);
                }

                bool null = img.isNull();
                if (!null) emit frameBufferData(img);
                emit frameBufferEmpty(null);

                m_profileEnabled = profileEnabled;
                m_hasPMU = hasPMU;

                emit getStateDone();
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::readProfileDone,
            this, [this] (bool timeout, const QList<profile_record_t> &records) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_PROFILE_DATA_CPL);
                emit readProfileDone(records);
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::setProfileModeDone,
            this, [this] (bool timeout) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_SET_PROFILE_MODE_CPL);
                emit setProfileModeDone();
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::setEventCounterDone,
            this, [this] (bool timeout) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_SET_EVENT_COUNTER_CPL);
                emit setEventCounterDone();
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::profileResetDone,
            this, [this] (bool timeout) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_PROFILE_RESET_CPL);
                emit profileResetDone();
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });

    connect(m_port, &OpenMVPluginSerialPort::closeResponse,
            this, [this] (bool timeout) {
                if (timeout) m_timeout = true;
                m_completionQueue.removeOne(V2_CLOSE_CPL);

                if (m_lineBuffer.size()) {
                    emit printData(pasrsePrintData(m_lineBuffer));
                    m_lineBuffer.clear();
                }

                m_v2ProtocolEnabled = false;

                emit closeResponse();
                if (m_completionQueue.isEmpty()) emit queueEmpty();
            });
}

void OpenMVPluginIO::command()
{
    if((!m_postedQueue.isEmpty())
    && (!m_completionQueue.isEmpty())
    && (m_postedQueue.size() == m_completionQueue.size()))
    {
        m_port->command(m_postedQueue.dequeue());
        m_sentPackets++;
    }
}

void OpenMVPluginIO::doFrameSizeCpl(int w, int h, int bpp)
{
    if((0 < w) && (w < 32768) && (0 < h) && (h < 32768) && (0 <= bpp) && (bpp <= (1024 * 1024 * 1024)))
    {
        int size = getImageSize(w, h, bpp, m_newPixformat, PIXFORMAT_JPEG); // Works for PNG too.

        // If we dump a multiple of the bulk packet size (64/512 bytes) this results in a ZLP
        // packet being sent from TinyUSB... Which messes up our synchronization with the camera
        // as we will issue the next command before the ZLP has been received.
        if (!(size % (m_hsOn ? HS_EP_SIZE : FS_EP_SIZE)))
        {
            size += 1; // Add 1 extra byte to ensure we don't have ZLP packet issues.
        }

        if(size)
        {
            for(int i = 0, j = (size + m_mtu - 1) / m_mtu; i < j; i++)
            {
                int new_size = qMin(size - ((j - 1 - i) * m_mtu), m_mtu);
                QByteArray buffer;
                serializeByte(buffer, __USBDBG_CMD);
                serializeByte(buffer, __USBDBG_FRAME_DUMP);
                serializeLong(buffer, new_size);
                m_postedQueue.push_front(OpenMVPluginSerialPortCommand(buffer, new_size, FRAME_DUMP_START_DELAY, FRAME_DUMP_END_DELAY, true, true));
                m_completionQueue.insert(1, USBDBG_FRAME_DUMP_CPL);
            }

            m_frameSizeW = w;
            m_frameSizeH = h;
            m_frameSizeBPP = bpp;
        }
        else
        {
            emit frameBufferEmpty(true);
        }
    }
    else
    {
        emit frameBufferEmpty(true);
    }
}

void OpenMVPluginIO::doTxBufCpl()
{
    QByteArrayList list = m_lineBuffer.split('\n');
    m_lineBuffer = list.takeLast();

    QByteArray out;

    for(int i = 0, j = list.size(); i < j; i++)
    {
        out.append(pasrsePrintData(list.at(i) + '\n'));
    }

    if(!out.isEmpty())
    {
        emit printData(out);
    }

    emit printEmpty(m_lineBuffer.isEmpty());
}

void OpenMVPluginIO::commandResult(const OpenMVPluginSerialPortCommandResult &commandResult)
{
    if(Q_LIKELY(!m_completionQueue.isEmpty()))
    {
        if(commandResult.m_ok)
        {
            QByteArray data = commandResult.m_data;
            m_receivedPackets++;

            switch(m_completionQueue.head())
            {
                case CHECK_PROTOCOL_VERSION_CPL:
                {
                    // V1 Protocol response will be 0/1.
                    // V2 Protocol response will be the proto sync.
                    m_v2ProtocolEnabled = deserializeWord(data) == OMVProto::SYNC_WORD;
                    m_port->enableV2Protocol(m_v2ProtocolEnabled);
                    break;
                }
                case CHECK_PROTOCOL_VERSION_CPL_SPLIT:
                {
                    break;
                }
                case USBDBG_FW_VERSION_CPL:
                {
                    // The optimizer will mess up the order if executed in emit.
                    m_firmwareMajor = deserializeLong(data);
                    m_firmwareMinor = deserializeLong(data);
                    m_firmwarePatch = deserializeLong(data);
                    emit firmwareVersion(m_firmwareMajor, m_firmwareMinor, m_firmwarePatch);
                    break;
                }
                case USBDBG_FRAME_SIZE_CPL:
                {
                    // The optimizer will mess up the order if executed in doFrameSizeCpl.
                    int w = deserializeLong(data);
                    int h = deserializeLong(data);
                    int bpp = deserializeLong(data);
                    doFrameSizeCpl(w, h, bpp);
                    break;
                }
                case USBDBG_FRAME_DUMP_CPL:
                {
                    m_pixelBuffer.append(data);

                    int size = getImageSize(m_frameSizeW, m_frameSizeH, m_frameSizeBPP, m_newPixformat, PIXFORMAT_JPEG);

                    if(m_pixelBuffer.size() >= size) // Works for PNG too.
                    {
                        m_pixelBuffer.chop(m_pixelBuffer.size() - size);

                        QPixmap pixmap = getImageFromData(m_pixelBuffer, m_frameSizeW, m_frameSizeH, m_frameSizeBPP, m_rgb565ByteReversed, m_newPixformat, PIXFORMAT_JPEG); // Works for PNG too.
                        bool null = pixmap.isNull();

                        if(!null)
                        {
                            emit frameBufferData(pixmap);
                            m_receivedImages++;
                        }

                        m_frameSizeW = int();
                        m_frameSizeH = int();
                        m_frameSizeBPP = int();
                        m_pixelBuffer.clear();
                        emit frameBufferEmpty(null);
                    }
                    // DISABLED - NOT REQUIRED - FIXING ZLP OVERLAP WAS WHY THINGS STALL - REMOVE AFTER TRIAL PERIOD
                    //
                    // else
                    // {
                    //     QByteArray buffer;
                    //     serializeByte(buffer, __USBDBG_CMD);
                    //     serializeByte(buffer, __USBDBG_FRAME_DUMP);
                    //     serializeLong(buffer, FRAME_DUMP_UNLOCK_RESPONSE_LEN);
                    //     m_postedQueue.push_front(OpenMVPluginSerialPortCommand(buffer, FRAME_DUMP_UNLOCK_RESPONSE_LEN, FRAME_DUMP_UNLOCK_START_DELAY, FRAME_DUMP_UNLOCK_END_DELAY));
                    //     m_completionQueue.insert(1, USBDBG_FRAME_DUMP_UNLOCK_CPL);
                    // }
                    //
                    // DISABLED - NOT REQUIRED - FIXING ZLP OVERLAP WAS WHY THINGS STALL - REMOVE AFTER TRIAL PERIOD
                    else
                    {
                        m_frameSizeW = int();
                        m_frameSizeH = int();
                        m_frameSizeBPP = int();
                        m_pixelBuffer.clear();
                        emit frameBufferEmpty(true);
                    }

                    break;
                }
                case USBDBG_FRAME_DUMP_UNLOCK_CPL:
                {
                    m_frameSizeW = int();
                    m_frameSizeH = int();
                    m_frameSizeBPP = int();
                    m_pixelBuffer.clear();
                    emit frameBufferEmpty(true);
                    break;
                }
                case USBDBG_ARCH_STR_CPL:
                {
                    m_archString = QString::fromUtf8(data.append('\0').split(0).takeFirst());
                    emit archString(m_archString);
                    break;
                }
                case USBDBG_LEARN_MTU_CPL:
                {
                    m_mtu = deserializeLong(data);
                    emit learnedMTU(true);
                    break;
                }
                case USBDBG_SCRIPT_EXEC_CPL_0:
                {
                    break;
                }
                case USBDBG_SCRIPT_EXEC_CPL_1:
                {
                    emit scriptExecDone();
                    break;
                }
                case USBDBG_SCRIPT_STOP_CPL:
                {
                    emit scriptStopDone();
                    break;
                }
                case USBDBG_SCRIPT_RUNNING_CPL:
                {
                    emit scriptRunning(deserializeLong(data));
                    break;
                }
                case USBDBG_TEMPLATE_SAVE_CPL_0:
                {
                    break;
                }
                case USBDBG_TEMPLATE_SAVE_CPL_1:
                {
                    emit templateSaveDone();
                    break;
                }
                case USBDBG_DESCRIPTOR_SAVE_CPL_0:
                {
                    break;
                }
                case USBDBG_DESCRIPTOR_SAVE_CPL_1:
                {
                    emit descriptorSaveDone();
                    break;
                }
                case USBDBG_ATTR_READ_CPL:
                {
                    emit attribute(deserializeByte(data));
                    break;
                }
                case USBDBG_ATTR_READ_CPL_0:
                {
                    break;
                }
                case USBDBG_ATTR_READ_CPL_1:
                {
                    emit attribute(deserializeByte(data));
                    break;
                }
                case USBDBG_ATTR_WRITE_CPL_0:
                {
                    break;
                }
                case USBDBG_ATTR_WRITE_CPL_1:
                {
                    emit setAttrributeDone();
                    break;
                }
                case USBDBG_ATTR_WRITE_CPL:
                {
                    emit setAttrributeDone();
                    break;
                }
                case USBDBG_SYS_RESET_CPL:
                {
                    emit sysResetDone();
                    break;
                }
                case USBDBG_FB_ENABLE_CPL:
                {
                    emit fbEnableDone();
                    break;
                }
                case USBDBG_FB_ENABLE_CPL_0:
                {
                    break;
                }
                case USBDBG_FB_ENABLE_CPL_1:
                {
                    emit fbEnableDone();
                    break;
                }
                case USBDBG_JPEG_ENABLE_CPL:
                {
                    emit jpegEnableDone();
                    break;
                }
                case USBDBG_JPEG_ENABLE_CPL_0:
                {
                    break;
                }
                case USBDBG_JPEG_ENABLE_CPL_1:
                {
                    emit jpegEnableDone();
                    break;
                }
                case USBDBG_TX_BUF_LEN_CPL:
                {
                    int len = deserializeLong(data);

                    if(len)
                    {
                        // If we dump a multiple of the bulk packet size (64/512 bytes) this results in a ZLP
                        // packet being sent from TinyUSB... Which messes up our synchronization with the camera
                        // as we will issue the next command before the ZLP has been received.
                        if (!(len % (m_hsOn ? HS_EP_SIZE : FS_EP_SIZE)))
                        {
                            len -= 1; // Remove 1 byte to ensure we don't have ZLP packet issues.
                        }

                        for(int i = 0, j = (len + m_mtu - 1) / m_mtu; i < j; i++)
                        {
                            int new_len = qMin(len - ((j - 1 - i) * m_mtu), m_mtu);
                            QByteArray buffer;
                            serializeByte(buffer, __USBDBG_CMD);
                            serializeByte(buffer, __USBDBG_TX_BUF);
                            serializeLong(buffer, new_len);
                            m_postedQueue.push_front(OpenMVPluginSerialPortCommand(buffer, new_len, TX_BUF_START_DELAY, TX_BUF_END_DELAY, true, true));
                            m_completionQueue.insert(1, USBDBG_TX_BUF_CPL);
                        }
                    }
                    else if(m_lineBuffer.size())
                    {
                        emit printData(pasrsePrintData(m_lineBuffer));
                        m_lineBuffer.clear();
                        emit printEmpty(true);
                    }
                    else
                    {
                        emit printEmpty(true);
                    }

                    break;
                }
                case USBDBG_TX_BUF_CPL:
                {
                    m_lineBuffer.append(data);
                    doTxBufCpl();
                    break;
                }
                case USBDBG_SENSOR_ID_CPL:
                {
                    m_sensorID = deserializeLong(data);
                    emit sensorIdDone(QList<int>() << m_sensorID);
                    break;
                }
                case USBDBG_TX_INPUT_CPL_0:
                {
                    break;
                }
                case USBDBG_TX_INPUT_CPL_1:
                {
                    break;
                }
                case USBDBG_TIME_INPUT_CPL_0:
                {
                    break;
                }
                case USBDBG_TIME_INPUT_CPL_1:
                {
                    break;
                }
                case USBDBG_GET_STATE_CPL:
                {
                    int payload_len = m_getStateVariableSize ? (m_hsOn ? GET_STATE_PAYLOAD_LEN_HS : GET_STATE_PAYLOAD_LEN_FS) : GET_STATE_PAYLOAD_LEN;

                    if (data.size() == payload_len)
                    {
                        int flags = deserializeLong(data);
                        int w = deserializeLong(data);
                        int h = deserializeLong(data);
                        int bpp = deserializeLong(data);

                        emit scriptRunning(flags & __USBDBG_GET_STATE_FLAGS_SCRIPT);

                        if(flags & __USBDBG_GET_STATE_FLAGS_TEXT)
                        {
                            m_lineBuffer.append(data.append('\0').split(0).takeFirst());
                            doTxBufCpl();
                        }
                        else if(m_lineBuffer.size())
                        {
                            emit printData(pasrsePrintData(m_lineBuffer));
                            m_lineBuffer.clear();
                            emit printEmpty(true);
                        }
                        else
                        {
                            emit printEmpty(true);
                        }

                        if(flags & __USBDBG_GET_STATE_FLAGS_FRAME)
                        {
                            doFrameSizeCpl(w, h, bpp);
                        }
                        else
                        {
                            emit frameBufferEmpty(true);
                        }

                        m_profileEnabled = !!(flags & __USBDBG_GET_STATE_FLAGS_PROFILE);
                        m_hasPMU = m_profileEnabled && (flags & __USBDBG_GET_STATE_FLAGS_HAS_PMU);
                    }
                    else if(m_lineBuffer.size())
                    {
                        emit printData(pasrsePrintData(m_lineBuffer));
                        m_lineBuffer.clear();
                    }

                    emit getStateDone();

                    break;
                }
                case USBDBG_PROFILE_SIZE_CPL:
                {
                    m_record_count = deserializeLong(data);
                    m_record_size = deserializeLong(data);
                    m_event_count = deserializeLong(data);

                    if(m_record_count)
                    {
                        // If we dump a multiple of the bulk packet size (64/512 bytes) this results in a ZLP
                        // packet being sent from TinyUSB... Which messes up our synchronization with the camera
                        // as we will issue the next command before the ZLP has been received.
                        if (!((m_record_count * m_record_size) % (m_hsOn ? HS_EP_SIZE : FS_EP_SIZE)))
                        {
                            m_record_count--; // Drop the last record to ensure we don't have ZLP packet issues.
                        }
                    }

                    if(m_record_count)
                    {
                        QByteArray buffer;
                        serializeByte(buffer, __USBDBG_CMD);
                        serializeByte(buffer, __USBDBG_PROFILE_DUMP);
                        serializeLong(buffer, m_record_count * m_record_size);
                        m_postedQueue.push_front(OpenMVPluginSerialPortCommand(buffer, m_record_count * m_record_size, PROFILE_DUMP_START_DELAY, PROFILE_DUMP_END_DELAY, true, true));
                        m_completionQueue.insert(1, USBDBG_PROFILE_DUMP_CPL);
                    }
                    else
                    {
                        m_record_count = int();
                        m_record_size = int();
                        m_event_count = int();
                        QList<profile_record_t> records;
                        emit readProfileDone(records);
                    }

                    break;
                }
                case USBDBG_PROFILE_DUMP_CPL:
                {
                    typedef struct __attribute__((packed)) profile_record_raw {
                        uint32_t address;
                        uint32_t caller;
                        uint32_t call_count;
                        uint32_t min_ticks;
                        uint32_t max_ticks;
                        uint64_t total_ticks;
                        uint64_t total_cycles;
                        uint64_t events[];
                        // uint32_t spacing
                    } profile_record_raw_t;

                    QList<profile_record_t> records;

                    int raw_record_size = sizeof(profile_record_raw_t) + (m_event_count * sizeof(uint64_t)) + sizeof(uint32_t);
                    int max_valid_record_count = data.size() / raw_record_size;

                    for (int i = 0; i < max_valid_record_count; i++)
                    {
                        profile_record_raw_t *raw = (profile_record_raw_t *) (data.data() + (i * raw_record_size));
                        profile_record_t record;

                        record.address = raw->address;
                        record.caller = raw->caller;
                        record.call_count = raw->call_count;
                        record.min_ticks = raw->min_ticks;
                        record.max_ticks = raw->max_ticks;
                        record.total_ticks = raw->total_ticks;
                        record.total_cycles = raw->total_cycles;

                        for (int j = 0; j < m_event_count; j++)
                        {
                            record.events.append(raw->events[j]);
                        }

                        records.append(record);
                    }

                    m_record_count = int();
                    m_record_size = int();
                    m_event_count = int();
                    emit readProfileDone(records);
                    break;
                }
                case USBDBG_SET_PROFILE_MODE_0_CPL:
                {
                    break;
                }
                case USBDBG_SET_PROFILE_MODE_1_CPL:
                {
                    emit setProfileModeDone();
                    break;
                }
                case USBDBG_SET_EVT_CNTR_0_CPL:
                {
                    break;
                }
                case USBDBG_SET_EVT_CNTR_1_CPL:
                {
                    emit setEventCounterDone();
                    break;
                }
                case USBDBG_PROFILE_RESET_CPL:
                {
                    emit profileResetDone();
                    break;
                }
                case BOOTLDR_START_CPL:
                {
                    int result = deserializeLong(data);
                    emit gotBootloaderStart((result == V1_BOOTLDR) || (result == V2_BOOTLDR) || (result == V3_BOOTLDR), result);
                    break;
                }
                case BOOTLDR_RESET_CPL:
                {
                    emit bootloaderResetDone(true);
                    break;
                }
                case BOOTLDR_ERASE_CPL:
                {
                    emit flashEraseDone(true);
                    break;
                }
                case BOOTLDR_WRITE_CPL:
                {
                    emit flashWriteDone(true);
                    break;
                }
                case BOOTLDR_QUERY_CPL:
                {
                    // The optimizer will mess up the order if executed in emit.
                    int all_start = deserializeLong(data);
                    int start = deserializeLong(data);
                    int last = deserializeLong(data);
                    emit bootloaderQueryDone(all_start, start, last);
                    break;
                }
                case BOOTLDR_QSPIF_ERASE_CPL:
                {
                    emit bootloaderQSPIFEraseDone(true);
                    break;
                }
                case BOOTLDR_QSPIF_WRITE_CPL:
                {
                    emit bootloaderQSPIFWriteDone(true);
                    break;
                }
                case BOOTLDR_QSPIF_LAYOUT_CPL:
                {
                    // The optimizer will mess up the order if executed in emit.
                    int start_block = deserializeLong(data);
                    int max_block = deserializeLong(data);
                    int block_size_in_bytes = deserializeLong(data);
                    emit bootloaderQSPIFLayoutDone(start_block, max_block, block_size_in_bytes);
                    break;
                }
                case BOOTLDR_QSPIF_MEMTEST_CPL:
                {
                    bool result = deserializeLong(data);
                    emit bootloaderQSPIFMemtestDone(result);
                    break;
                }
                case CLOSE_CPL:
                {
                    if(m_lineBuffer.size())
                    {
                        emit printData(pasrsePrintData(m_lineBuffer));
                        m_lineBuffer.clear();
                    }

                    m_v2ProtocolEnabled = false;
                    m_profileEnabled = false;
                    m_hasPMU = false;
                    m_archString = QString();
                    m_firmwareMajor = int();
                    m_firmwareMinor = int();
                    m_firmwarePatch = int();
                    m_sensorID = uint();
                    m_sentPackets = uint();
                    m_receivedPackets = uint();
                    m_receivedImages = uint();
                    emit closeResponse();
                    break;
                }
            }

            m_completionQueue.dequeue();

            if (m_postedQueue.isEmpty() && m_completionQueue.isEmpty())
            {
                emit queueEmpty();
            }

            command();
        }
        else
        {
            forever
            {
                switch(m_completionQueue.head())
                {
                    case CHECK_PROTOCOL_VERSION_CPL:
                    {
                        m_v2ProtocolEnabled = false;
                        m_port->enableV2Protocol(m_v2ProtocolEnabled);
                        break;
                    }
                    case CHECK_PROTOCOL_VERSION_CPL_SPLIT:
                    {
                        break;
                    }
                    case USBDBG_FW_VERSION_CPL:
                    {
                        emit firmwareVersion(int(), int(), int());
                        break;
                    }
                    case USBDBG_FRAME_SIZE_CPL:
                    {
                        emit frameBufferEmpty(true);
                        break;
                    }
                    case USBDBG_FRAME_DUMP_CPL:
                    {
                        m_frameSizeW = int();
                        m_frameSizeH = int();
                        m_frameSizeBPP = int();
                        m_pixelBuffer.clear();
                        emit frameBufferEmpty(true);
                        break;
                    }
                    case USBDBG_FRAME_DUMP_UNLOCK_CPL:
                    {
                        m_frameSizeW = int();
                        m_frameSizeH = int();
                        m_frameSizeBPP = int();
                        m_pixelBuffer.clear();
                        emit frameBufferEmpty(true);
                        break;
                    }
                    case USBDBG_ARCH_STR_CPL:
                    {
                        emit archString(QString());
                        break;
                    }
                    case USBDBG_LEARN_MTU_CPL:
                    {
                        m_mtu = MTU_DEFAULT_SIZE;
                        emit learnedMTU(false);
                        break;
                    }
                    case USBDBG_SCRIPT_EXEC_CPL_0:
                    {
                        break;
                    }
                    case USBDBG_SCRIPT_EXEC_CPL_1:
                    {
                        emit scriptExecDone();
                        break;
                    }
                    case USBDBG_SCRIPT_STOP_CPL:
                    {
                        emit scriptStopDone();
                        break;
                    }
                    case USBDBG_SCRIPT_RUNNING_CPL:
                    {
                        emit scriptRunning(bool());
                        break;
                    }
                    case USBDBG_TEMPLATE_SAVE_CPL_0:
                    {
                        break;
                    }
                    case USBDBG_TEMPLATE_SAVE_CPL_1:
                    {
                        emit templateSaveDone();
                        break;
                    }
                    case USBDBG_DESCRIPTOR_SAVE_CPL_0:
                    {
                        break;
                    }
                    case USBDBG_DESCRIPTOR_SAVE_CPL_1:
                    {
                        emit descriptorSaveDone();
                        break;
                    }
                    case USBDBG_ATTR_READ_CPL:
                    {
                        emit attribute(int());
                        break;
                    }
                    case USBDBG_ATTR_READ_CPL_0:
                    {
                        break;
                    }
                    case USBDBG_ATTR_READ_CPL_1:
                    {
                        emit attribute(int());
                        break;
                    }
                    case USBDBG_ATTR_WRITE_CPL:
                    {
                        emit setAttrributeDone();
                        break;
                    }
                    case USBDBG_ATTR_WRITE_CPL_0:
                    {
                        break;
                    }
                    case USBDBG_ATTR_WRITE_CPL_1:
                    {
                        emit setAttrributeDone();
                        break;
                    }
                    case USBDBG_SYS_RESET_CPL:
                    {
                        emit sysResetDone();
                        break;
                    }
                    case USBDBG_FB_ENABLE_CPL:
                    {
                        emit fbEnableDone();
                        break;
                    }
                    case USBDBG_FB_ENABLE_CPL_0:
                    {
                        break;
                    }
                    case USBDBG_FB_ENABLE_CPL_1:
                    {
                        emit fbEnableDone();
                        break;
                    }
                    case USBDBG_JPEG_ENABLE_CPL:
                    {
                        emit jpegEnableDone();
                        break;
                    }
                    case USBDBG_JPEG_ENABLE_CPL_0:
                    {
                        break;
                    }
                    case USBDBG_JPEG_ENABLE_CPL_1:
                    {
                        emit jpegEnableDone();
                        break;
                    }
                    case USBDBG_TX_BUF_LEN_CPL:
                    {
                        if(m_lineBuffer.size())
                        {
                            emit printData(pasrsePrintData(m_lineBuffer));
                            m_lineBuffer.clear();
                        }

                        emit printEmpty(true);

                        break;
                    }
                    case USBDBG_TX_BUF_CPL:
                    {
                        if(m_lineBuffer.size())
                        {
                            emit printData(pasrsePrintData(m_lineBuffer));
                            m_lineBuffer.clear();
                        }

                        emit printEmpty(true);

                        break;
                    }
                    case USBDBG_SENSOR_ID_CPL:
                    {
                        emit sensorIdDone(QList<int>() << int());
                        break;
                    }
                    case USBDBG_TX_INPUT_CPL_0:
                    {
                        break;
                    }
                    case USBDBG_TX_INPUT_CPL_1:
                    {
                        break;
                    }
                    case USBDBG_TIME_INPUT_CPL_0:
                    {
                        break;
                    }
                    case USBDBG_TIME_INPUT_CPL_1:
                    {
                        break;
                    }
                    case USBDBG_GET_STATE_CPL:
                    {
                        emit scriptRunning(bool());

                        emit frameBufferEmpty(true);

                        if(m_lineBuffer.size())
                        {
                            emit printData(pasrsePrintData(m_lineBuffer));
                            m_lineBuffer.clear();
                        }

                        emit printEmpty(true);

                        emit getStateDone();

                        break;
                    }
                    case USBDBG_PROFILE_SIZE_CPL:
                    {
                        QList<profile_record_t> records;
                        emit readProfileDone(records);
                        break;
                    }
                    case USBDBG_PROFILE_DUMP_CPL:
                    {
                        m_record_count = int();
                        m_record_size = int();
                        m_event_count = int();
                        QList<profile_record_t> records;
                        emit readProfileDone(records);
                        break;
                    }
                    case USBDBG_SET_PROFILE_MODE_0_CPL:
                    {
                        break;
                    }
                    case USBDBG_SET_PROFILE_MODE_1_CPL:
                    {
                        emit setProfileModeDone();
                        break;
                    }
                    case USBDBG_SET_EVT_CNTR_0_CPL:
                    {
                        break;
                    }
                    case USBDBG_SET_EVT_CNTR_1_CPL:
                    {
                        emit setEventCounterDone();
                        break;
                    }
                    case USBDBG_PROFILE_RESET_CPL:
                    {
                        emit profileResetDone();
                        break;
                    }
                    case BOOTLDR_START_CPL:
                    {
                        emit gotBootloaderStart(false, int());
                        break;
                    }
                    case BOOTLDR_RESET_CPL:
                    {
                        emit bootloaderResetDone(false);
                        break;
                    }
                    case BOOTLDR_ERASE_CPL:
                    {
                        emit flashEraseDone(false);
                        break;
                    }
                    case BOOTLDR_WRITE_CPL:
                    {
                        emit flashWriteDone(false);
                        break;
                    }
                    case BOOTLDR_QUERY_CPL:
                    {
                        emit bootloaderQueryDone(int(), int(), int());
                        break;
                    }
                    case BOOTLDR_QSPIF_ERASE_CPL:
                    {
                        emit bootloaderQSPIFEraseDone(false);
                        break;
                    }
                    case BOOTLDR_QSPIF_WRITE_CPL:
                    {
                        emit bootloaderQSPIFWriteDone(false);
                        break;
                    }
                    case BOOTLDR_QSPIF_LAYOUT_CPL:
                    {
                        emit bootloaderQSPIFLayoutDone(int(), int(), int());
                        break;
                    }
                    case BOOTLDR_QSPIF_MEMTEST_CPL:
                    {
                        emit bootloaderQSPIFMemtestDone(false);
                        break;
                    }
                    case CLOSE_CPL:
                    {
                        if(m_lineBuffer.size())
                        {
                            emit printData(pasrsePrintData(m_lineBuffer));
                            m_lineBuffer.clear();
                        }

                        m_v2ProtocolEnabled = false;
                        m_profileEnabled = false;
                        m_hasPMU = false;
                        m_archString = QString();
                        m_firmwareMajor = int();
                        m_firmwareMinor = int();
                        m_firmwarePatch = int();
                        m_sensorID = uint();
                        m_sentPackets = uint();
                        m_receivedPackets = uint();
                        m_receivedImages = uint();
                        emit closeResponse();
                        break;
                    }
                }

                m_completionQueue.dequeue();

                if (m_postedQueue.isEmpty() && m_completionQueue.isEmpty())
                {
                    emit queueEmpty();
                }

                if((!m_postedQueue.isEmpty())
                && (!m_completionQueue.isEmpty())
                && (m_postedQueue.size() == m_completionQueue.size()))
                {
                    m_postedQueue.dequeue();
                }
                else
                {
                    break;
                }
            }

            m_timeout = true;
        }
    }
}

bool OpenMVPluginIO::getTimeout()
{
    bool timeout = m_timeout;
    m_timeout = false;
    return timeout;
}

bool OpenMVPluginIO::queueisEmpty() const
{
    return m_postedQueue.isEmpty() && m_completionQueue.isEmpty();
}

bool OpenMVPluginIO::frameSizeDumpQueued() const
{
    return m_completionQueue.contains(USBDBG_FRAME_SIZE_CPL) ||
           m_completionQueue.contains(USBDBG_FRAME_DUMP_CPL) ||
           m_completionQueue.contains(USBDBG_FRAME_DUMP_UNLOCK_CPL) ||
           m_completionQueue.contains(V2_FRAME_BUFFER_DATA_CPL);
}

bool OpenMVPluginIO::getScriptRunningQueued() const
{
    return m_completionQueue.contains(USBDBG_SCRIPT_RUNNING_CPL) ||
           m_completionQueue.contains(V2_SCRIPT_RUNNING_CPL);
}

bool OpenMVPluginIO::getAttributeQueued() const
{
    return m_completionQueue.contains(USBDBG_ATTR_READ_CPL);
}

bool OpenMVPluginIO::getTxBufferQueued() const
{
    return m_completionQueue.contains(USBDBG_TX_BUF_LEN_CPL) ||
           m_completionQueue.contains(USBDBG_TX_BUF_CPL) ||
           m_completionQueue.contains(V2_PRINT_DATA_CPL);
}

bool OpenMVPluginIO::getStateQueued() const
{
    return m_completionQueue.contains(USBDBG_GET_STATE_CPL) ||
           m_completionQueue.contains(USBDBG_FRAME_DUMP_CPL) ||
           m_completionQueue.contains(USBDBG_FRAME_DUMP_UNLOCK_CPL) ||
           m_completionQueue.contains(V2_GET_STATE_CPL);
}

bool OpenMVPluginIO::readProfileQueued() const
{
    return m_completionQueue.contains(USBDBG_PROFILE_SIZE_CPL) ||
           m_completionQueue.contains(USBDBG_PROFILE_DUMP_CPL) ||
           m_completionQueue.contains(V2_PROFILE_DATA_CPL);
}

void OpenMVPluginIO::checkProtocolVerison(bool splitCommand)
{
    // STM32 USBDBG Behavior:
    // * Entire USB transfer (up to 64 bytes) is treated as one command.
    // * Command at the start of the packet is read (first byte checked), remaining bytes ignored.
    // -> Will respond with firmware version (extra bytes ignored).

    // TinyUSB USBDBG Behavior:
    // * 6-bytes read at a time, first byte checked if valid, and remaining bytes used as part of command.
    // -> Will respond with firmware version (extra bytes read, but ignored as invalid commands).

    // V2 Behavior:
    // * Sync pattern checked for 1 byte at a time.
    // * Valid packet after sync is processed.
    // -> Ignores leading USBDBG command and processes valid V2 command.

    // Solution (in one USB packet):
    // * USBDBG command with read response (6-bytes).
    // * V2 Packet with sync header (18-bytes) (multiple of 6-bytes for TinyUSB USBDBG).

    const int USBDBG_LEN = 6;
    const int V2_LEN = 4;

    QByteArray buffer;

    // First part - 6 byte packet with a read response.
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_SCRIPT_RUNNING);
    serializeLong(buffer, SCRIPT_RUNNING_RESPONSE_LEN);

    // Second part - 18 byte V2 packet.
    serializeWord(buffer, OMVProto::SYNC_WORD);
    serializeByte(buffer, 0);
    serializeByte(buffer, 0);
    serializeByte(buffer, 0);
    serializeByte(buffer, OMVPOpcode::PROTO_SYNC);
    serializeWord(buffer, V2_LEN);
    serializeWord(buffer, crc16(buffer.mid(USBDBG_LEN, OMVProto::HEADER_SIZE - 2)));
    buffer.append(QByteArray(V2_LEN, 0));
    serializeLong(buffer, crc32(buffer.mid(USBDBG_LEN + OMVProto::HEADER_SIZE, V2_LEN)));

    // Sanity check for USBDBG.
    Q_ASSERT(!(buffer.size() % USBDBG_LEN));

    for (int i = USBDBG_LEN; i < buffer.size(); i += USBDBG_LEN) {
        Q_ASSERT(buffer.at(i) != __USBDBG_CMD);
    }

    // On Mac for the RT1062 and AE3, they cannot handle receiving all 4 commands at once.
    // Splitting the command up into UDSBG_LEN sized commands seems to work around this...
    int len = SCRIPT_RUNNING_RESPONSE_LEN;

    if (splitCommand) {
        while (buffer.size() > USBDBG_LEN) {
            QByteArray part = buffer.left(USBDBG_LEN);
            buffer = buffer.mid(USBDBG_LEN);
            m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(part,
                                                                len,
                                                                SCRIPT_RUNNING_START_DELAY,
                                                                SCRIPT_RUNNING_END_DELAY,
                                                                true, false, len > 0));
            m_completionQueue.enqueue(CHECK_PROTOCOL_VERSION_CPL_SPLIT);
            len = 0;
        }
    }

    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        len,
                                                        SCRIPT_RUNNING_START_DELAY,
                                                        SCRIPT_RUNNING_END_DELAY,
                                                        true, false, len > 0));
    m_completionQueue.enqueue(CHECK_PROTOCOL_VERSION_CPL);
    command();
}

void OpenMVPluginIO::getFirmwareVersion()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_FIRMWARE_VERSION_CPL);
        m_port->getFirmwareVersion();
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_FW_VERSION);
    serializeLong(buffer, FW_VERSION_RESPONSE_LEN);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        FW_VERSION_RESPONSE_LEN,
                                                        FW_VERSION_START_DELAY,
                                                        FW_VERSION_END_DELAY,
                                                        true, true, true));
    m_completionQueue.enqueue(USBDBG_FW_VERSION_CPL);
    command();
}

void OpenMVPluginIO::getSystemInfoString()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_SYSTEM_INFO_STRING_CPL);
        m_port->getSystemInfoString();
        return;
    }

    QTimer::singleShot(0, this, [this] {
        QString info;
        QTextStream stream(&info);

        QRegularExpressionMatch match = QRegularExpression(QStringLiteral("(.+?)\\[(.+?):(.+?)\\]")).match(m_archString);

        if(match.hasMatch())
        {
            stream << "CPU ID: " << match.captured(2) << " - ";
            stream << "Device ID: " << match.captured(3) << '\n';
        }

        stream << QStringLiteral("CSI0: 0x%1").arg(m_sensorID) << '\n';

        if(match.hasMatch())
        {
            stream << "USB ID: " << match.captured(1).trimmed() << '\n';
        }

        stream << "Hardware capabilities:" << '\n';
        stream << "  USB High-Speed: " << (m_hsOn ? "Yes" : "No");
        stream << "\t\tPMU: " << (m_hasPMU ? "Yes" : "No") << '\n';
        stream << "Profiler: " << (m_profileEnabled ? "Available" : "Not available") << '\n';
        stream << "Firmware version: " << m_firmwareMajor << "." << m_firmwareMinor << "." << m_firmwarePatch;
        systemInfoString(info);
    });
}

void OpenMVPluginIO::getHostStatsString()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_HOST_STATS_STRING_CPL);
        m_port->getHostStatsString();
        return;
    }

    QTimer::singleShot(0, this, [this] {
        QString info;
        QTextStream stream(&info);
        stream << "Packets Sent: " << m_sentPackets << "\n";
        stream << "Packets Received: " << m_receivedPackets;
        hostStatsString(info);
    });
}

void OpenMVPluginIO::getDeviceStatsString()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_DEVICE_STATS_STRING_CPL);
        m_port->getDeviceStatsString();
        return;
    }

    QTimer::singleShot(0, this, [this] {
        QString info;
        QTextStream stream(&info);
        stream << "Sent Images: " << m_receivedImages;
        deviceStatsString(info);
    });
}

void OpenMVPluginIO::frameSizeDump()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_FRAME_BUFFER_DATA_CPL);
        m_port->frameDump();
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_FRAME_SIZE);
    serializeLong(buffer, FRAME_SIZE_RESPONSE_LEN);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        FRAME_SIZE_RESPONSE_LEN,
                                                        FRAME_SIZE_START_DELAY,
                                                        FRAME_SIZE_END_DELAY));
    m_completionQueue.enqueue(USBDBG_FRAME_SIZE_CPL);
    command();
}

void OpenMVPluginIO::getArchString()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_ARCH_STRING_CPL);
        m_port->getArchString();
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_ARCH_STR);
    serializeLong(buffer, ARCH_STR_RESPONSE_LEN);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        ARCH_STR_RESPONSE_LEN,
                                                        ARCH_STR_START_DELAY,
                                                        ARCH_STR_END_DELAY));
    m_completionQueue.enqueue(USBDBG_ARCH_STR_CPL);
    command();
}

void OpenMVPluginIO::learnMTU()
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {learnedMTU(true);});
        return;
    }

    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(QByteArray(),
                                                        sizeof(int),
                                                        LEARN_MTU_START_DELAY,
                                                        LEARN_MTU_END_DELAY));
    m_completionQueue.enqueue(USBDBG_LEARN_MTU_CPL);
    command();
}

void OpenMVPluginIO::scriptExec(const QByteArray &data)
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_SCRIPT_EXEC_CPL);
        m_port->scriptExec(data);
        return;
    }

    QByteArray buffer, script = (data.size() % TABOO_PACKET_SIZE) ? data : (data + '\n');
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_SCRIPT_EXEC);
    serializeLong(buffer, script.size());
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        int(),
                                                        SCRIPT_EXEC_START_DELAY,
                                                        SCRIPT_EXEC_END_DELAY));
    m_completionQueue.enqueue(USBDBG_SCRIPT_EXEC_CPL_0);
    command();
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(script,
                                                        int(),
                                                        SCRIPT_EXEC_2_START_DELAY,
                                                        SCRIPT_EXEC_2_END_DELAY));
    m_completionQueue.enqueue(USBDBG_SCRIPT_EXEC_CPL_1);
    command();
}

void OpenMVPluginIO::scriptStop()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_SCRIPT_STOP_CPL);
        m_port->scriptStop();
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_SCRIPT_STOP);
    serializeLong(buffer, int());
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        int(),
                                                        SCRIPT_STOP_START_DELAY,
                                                        SCRIPT_STOP_END_DELAY));
    m_completionQueue.enqueue(USBDBG_SCRIPT_STOP_CPL);
    command();
}

void OpenMVPluginIO::getScriptRunning()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_SCRIPT_RUNNING_CPL);
        m_port->getScriptRunning();
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_SCRIPT_RUNNING);
    serializeLong(buffer, SCRIPT_RUNNING_RESPONSE_LEN);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        SCRIPT_RUNNING_RESPONSE_LEN,
                                                        SCRIPT_RUNNING_START_DELAY,
                                                        SCRIPT_RUNNING_END_DELAY));
    m_completionQueue.enqueue(USBDBG_SCRIPT_RUNNING_CPL);
    command();
}

void OpenMVPluginIO::templateSave(int x, int y, int w, int h, const QByteArray &path)
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {templateSaveDone();});
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_TEMPLATE_SAVE);
    serializeLong(buffer, 2 + 2 + 2 + 2 + path.size());
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        int(),
                                                        TEMPLATE_SAVE_START_DELAY,
                                                        TEMPLATE_SAVE_END_DELAY));
    m_completionQueue.enqueue(USBDBG_TEMPLATE_SAVE_CPL_0);
    command();
    buffer.clear();
    serializeWord(buffer, x);
    serializeWord(buffer, y);
    serializeWord(buffer, w);
    serializeWord(buffer, h);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer + path,
                                                        int(),
                                                        TEMPLATE_SAVE_2_START_DELAY,
                                                        TEMPLATE_SAVE_2_END_DELAY));
    m_completionQueue.enqueue(USBDBG_TEMPLATE_SAVE_CPL_1);
    command();
}

void OpenMVPluginIO::descriptorSave(int x, int y, int w, int h, const QByteArray &path)
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {descriptorSaveDone();});
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_DESCRIPTOR_SAVE);
    serializeLong(buffer, 2 + 2 + 2 + 2 + path.size());
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        int(),
                                                        DESCRIPTOR_SAVE_START_DELAY,
                                                        DESCRIPTOR_SAVE_END_DELAY));
    m_completionQueue.enqueue(USBDBG_DESCRIPTOR_SAVE_CPL_0);
    command();
    buffer.clear();
    serializeWord(buffer, x);
    serializeWord(buffer, y);
    serializeWord(buffer, w);
    serializeWord(buffer, h);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer + path,
                                                        int(),
                                                        DESCRIPTOR_SAVE_2_START_DELAY,
                                                        DESCRIPTOR_SAVE_2_END_DELAY));
    m_completionQueue.enqueue(USBDBG_DESCRIPTOR_SAVE_CPL_1);
    command();
}

void OpenMVPluginIO::getAttribute(int attr)
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {attribute(int());});
        return;
    }

    if(!m_breakUpGetAttributeCommand)
    {
        QByteArray buffer;
        serializeByte(buffer, __USBDBG_CMD);
        serializeByte(buffer, __USBDBG_ATTR_READ);
        serializeLong(buffer, ATTR_READ_RESPONSE_LEN);
        serializeWord(buffer, attr);
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            ATTR_READ_RESPONSE_LEN,
                                                            ATTR_READ_START_DELAY,
                                                            ATTR_READ_END_DELAY));
        m_completionQueue.enqueue(USBDBG_ATTR_READ_CPL);
        command();
    }
    else
    {
        QByteArray buffer;
        serializeByte(buffer, __USBDBG_CMD);
        serializeByte(buffer, __USBDBG_ATTR_READ_2);
        serializeLong(buffer, ATTR_READ_2_PAYLOAD_LEN);
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            int(),
                                                            ATTR_READ_0_START_DELAY,
                                                            ATTR_READ_0_END_DELAY));
        m_completionQueue.enqueue(USBDBG_ATTR_READ_CPL_0);
        command();
        buffer.clear();
        serializeLong(buffer, attr);
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            ATTR_READ_2_REPONSE_LEN,
                                                            ATTR_READ_1_START_DELAY,
                                                            ATTR_READ_1_END_DELAY));
        m_completionQueue.enqueue(USBDBG_ATTR_READ_CPL_1);
        command();
    }
}

void OpenMVPluginIO::setAttribute(int attr, int value)
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {setAttrributeDone();});
        return;
    }

    if(!m_breakUpSetAttributeCommand)
    {
        QByteArray buffer;
        serializeByte(buffer, __USBDBG_CMD);
        serializeByte(buffer, __USBDBG_ATTR_WRITE);
        serializeLong(buffer, int());
        serializeWord(buffer, attr);
        serializeWord(buffer, value);
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            int(),
                                                            ATTR_WRITE_START_DELAY,
                                                            ATTR_WRITE_END_DELAY));
        m_completionQueue.enqueue(USBDBG_ATTR_WRITE_CPL);
        command();
    }
    else
    {
        QByteArray buffer;
        serializeByte(buffer, __USBDBG_CMD);
        serializeByte(buffer, __USBDBG_ATTR_WRITE);
        serializeLong(buffer, ATTR_WRITE_PAYLOAD_LEN);
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            int(),
                                                            ATTR_WRITE_0_START_DELAY,
                                                            ATTR_WRITE_0_END_DELAY));
        m_completionQueue.enqueue(USBDBG_ATTR_WRITE_CPL_0);
        command();
        buffer.clear();
        serializeLong(buffer, attr);
        serializeLong(buffer, value);
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            int(),
                                                            ATTR_WRITE_1_START_DELAY,
                                                            ATTR_WRITE_1_END_DELAY));
        m_completionQueue.enqueue(USBDBG_ATTR_WRITE_CPL_1);
        command();
    }
}

void OpenMVPluginIO::sysReset(bool enterBootloader)
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_SYSTEM_RESET_CPL);
        m_port->sysReset(enterBootloader);
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, enterBootloader ? __USBDBG_SYS_RESET_TO_BL : __USBDBG_SYS_RESET);
    serializeLong(buffer, int());
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        int(),
                                                        enterBootloader ? SYS_RESET_TO_BL_START_DELAY : SYS_RESET_START_DELAY,
                                                        enterBootloader ? SYS_RESET_TO_BL_END_DELAY : SYS_RESET_END_DELAY));
    m_completionQueue.enqueue(USBDBG_SYS_RESET_CPL);
    command();
}

void OpenMVPluginIO::fbEnable(bool enabled)
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_FRAME_BUFFER_ENABLE_CPL);
        m_port->fbEnable(enabled);
        return;
    }

    if(!m_breakUpFBEnable)
    {
        QByteArray buffer;
        serializeByte(buffer, __USBDBG_CMD);
        serializeByte(buffer, __USBDBG_FB_ENABLE);
        serializeLong(buffer, int());
        serializeWord(buffer, enabled ? true : false);
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            int(),
                                                            FB_ENABLE_START_DELAY,
                                                            FB_ENABLE_END_DELAY));
        m_completionQueue.enqueue(USBDBG_FB_ENABLE_CPL);
        command();
    }
    else
    {
        QByteArray buffer;
        serializeByte(buffer, __USBDBG_CMD);
        serializeByte(buffer, __USBDBG_FB_ENABLE);
        serializeLong(buffer, FB_ENABLE_PAYLOAD_LEN);
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            int(),
                                                            FB_ENABLE_0_START_DELAY,
                                                            FB_ENABLE_0_END_DELAY));
        m_completionQueue.enqueue(USBDBG_FB_ENABLE_CPL_0);
        command();
        buffer.clear();
        serializeLong(buffer, enabled ? true : false);
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            int(),
                                                            FB_ENABLE_1_START_DELAY,
                                                            FB_ENABLE_1_END_DELAY));
        m_completionQueue.enqueue(USBDBG_FB_ENABLE_CPL_1);
        command();
    }
}

void OpenMVPluginIO::jpegEnable(bool enabled)
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_JPEG_ENABLE_CPL);
        m_port->jpegEnable(enabled);
        return;
    }

    QTimer::singleShot(0, this, [this] {jpegEnableDone();});

//    if(!m_breakUpJPEGEnable)
//    {
//        QByteArray buffer;
//        serializeByte(buffer, __USBDBG_CMD);
//        serializeByte(buffer, __USBDBG_JPEG_ENABLE);
//        serializeLong(buffer, int());
//        serializeWord(buffer, enabled ? true : false);
//        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
//                                                            int(),
//                                                            JPEG_ENABLE_START_DELAY,
//                                                            JPEG_ENABLE_END_DELAY));
//        m_completionQueue.enqueue(USBDBG_JPEG_ENABLE_CPL);
//        command();
//    }
//    else
//    {
//        QByteArray buffer;
//        serializeByte(buffer, __USBDBG_CMD);
//        serializeByte(buffer, __USBDBG_JPEG_ENABLE);
//        serializeLong(buffer, JPEG_ENABLE_PAYLOAD_LEN);
//        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
//                                                            int(),
//                                                            JPEG_ENABLE_0_START_DELAY,
//                                                            JPEG_ENABLE_0_END_DELAY));
//        m_completionQueue.enqueue(USBDBG_JPEG_ENABLE_CPL_0);
//        command();
//        buffer.clear();
//        serializeLong(buffer, enabled ? true : false);
//        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
//                                                            int(),
//                                                            JPEG_ENABLE_1_START_DELAY,
//                                                            JPEG_ENABLE_1_END_DELAY));
//        m_completionQueue.enqueue(USBDBG_JPEG_ENABLE_CPL_1);
//        command();
//    }
}

void OpenMVPluginIO::getTxBuffer()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_PRINT_DATA_CPL);
        m_port->getTxBuffer();
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_TX_BUF_LEN);
    serializeLong(buffer, TX_BUF_LEN_RESPONSE_LEN);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        TX_BUF_LEN_RESPONSE_LEN,
                                                        TX_BUF_LEN_START_DELAY,
                                                        TX_BUF_LEN_END_DELAY));
    m_completionQueue.enqueue(USBDBG_TX_BUF_LEN_CPL);
    command();
}

void OpenMVPluginIO::sensorId()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_SENSOR_ID_CPL);
        m_port->sensorId();
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_SENSOR_ID);
    serializeLong(buffer, SENSOR_ID_RESPONSE_LEN);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        SENSOR_ID_RESPONSE_LEN,
                                                        SENSOR_ID_START_DELAY,
                                                        SENSOR_ID_END_DELAY));
    m_completionQueue.enqueue(USBDBG_SENSOR_ID_CPL);
    command();
}

void OpenMVPluginIO::mainTerminalInput(const QByteArray &data)
{
    if (m_v2ProtocolEnabled) {
        return;
    }

    if(m_mainTerminalInput)
    {
        QByteArray buffer, text = (data.size() % TABOO_PACKET_SIZE) ? data : (data + '\0');
        serializeByte(buffer, __USBDBG_CMD);
        serializeByte(buffer, __USBDBG_TX_INPUT);
        serializeLong(buffer, text.size());
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            int(),
                                                            TX_INPUT_0_START_DELAY,
                                                            TX_INPUT_0_END_DELAY));
        m_completionQueue.enqueue(USBDBG_TX_INPUT_CPL_0);
        command();
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(text,
                                                            int(),
                                                            TX_INPUT_1_START_DELAY,
                                                            TX_INPUT_1_END_DELAY));
        m_completionQueue.enqueue(USBDBG_TX_INPUT_CPL_1);
        command();
    }
}

void OpenMVPluginIO::timeInput()
{
    if (m_v2ProtocolEnabled) {
        return;
    }

    QDateTime dt = QDateTime::currentDateTime();
    QByteArray rtcTuple;
    serializeLong(rtcTuple, dt.date().year());
    serializeLong(rtcTuple, dt.date().month());
    serializeLong(rtcTuple, dt.date().day());
    serializeLong(rtcTuple, dt.date().dayOfWeek());
    serializeLong(rtcTuple, dt.time().hour());
    serializeLong(rtcTuple, dt.time().minute());
    serializeLong(rtcTuple, dt.time().second());
    serializeLong(rtcTuple, dt.time().msec());
    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_TIME_INPUT);
    serializeLong(buffer, rtcTuple.size());
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        int(),
                                                        TIME_INPUT_0_START_DELAY,
                                                        TIME_INPUT_0_END_DELAY));
    m_completionQueue.enqueue(USBDBG_TIME_INPUT_CPL_0);
    command();
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(rtcTuple,
                                                        int(),
                                                        TIME_INPUT_1_START_DELAY,
                                                        TIME_INPUT_1_END_DELAY));
    m_completionQueue.enqueue(USBDBG_TIME_INPUT_CPL_1);
    command();
}

void OpenMVPluginIO::getState()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_GET_STATE_CPL);
        m_port->getState();
        return;
    }

    int payload_len = m_getStateVariableSize
        ? (m_hsOn ? GET_STATE_PAYLOAD_LEN_HS : GET_STATE_PAYLOAD_LEN_FS)
        : GET_STATE_PAYLOAD_LEN;
    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_GET_STATE);
    serializeLong(buffer, payload_len);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        payload_len,
                                                        GET_STATE_START_DELAY,
                                                        GET_STATE_END_DELAY));
    m_completionQueue.enqueue(USBDBG_GET_STATE_CPL);
    command();
}

void OpenMVPluginIO::readProfile()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_PROFILE_DATA_CPL);
        m_port->readProfile();
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_PROFILE_SIZE);
    serializeLong(buffer, PROFILE_SIZE_RESPONSE_LEN);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        PROFILE_SIZE_RESPONSE_LEN,
                                                        PROFILE_SIZE_START_DELAY,
                                                        PROFILE_SIZE_END_DELAY));
    m_completionQueue.enqueue(USBDBG_PROFILE_SIZE_CPL);
    command();
}

void OpenMVPluginIO::setProfileMode(int mode)
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_SET_PROFILE_MODE_CPL);
        m_port->setProfileMode(mode);
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_SET_PROFILE_MODE);
    serializeLong(buffer, SET_PROFILE_MODE_PAYLOAD_LEN);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        int(),
                                                        SET_PROFILE_MODE_0_START_DELAY,
                                                        SET_PROFILE_MODE_0_END_DELAY));
    m_completionQueue.enqueue(USBDBG_SET_PROFILE_MODE_0_CPL);
    command();
    buffer.clear();
    serializeLong(buffer, mode);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        int(),
                                                        SET_PROFILE_MODE_1_START_DELAY,
                                                        SET_PROFILE_MODE_1_END_DELAY));
    m_completionQueue.enqueue(USBDBG_SET_PROFILE_MODE_1_CPL);
    command();
}

void OpenMVPluginIO::setEventCounter(int event_num, int event_type)
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_SET_EVENT_COUNTER_CPL);
        m_port->setEventCounter(event_num, event_type);
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_SET_EVT_CNTR);
    serializeLong(buffer, SET_EVENT_COUNTER_PAYLOAD_LEN);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        int(),
                                                        SET_EVT_CNTR_0_START_DELAY,
                                                        SET_EVT_CNTR_0_END_DELAY));
    m_completionQueue.enqueue(USBDBG_SET_EVT_CNTR_0_CPL);
    command();
    buffer.clear();
    serializeLong(buffer, event_num);
    serializeLong(buffer, event_type);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        int(),
                                                        SET_EVT_CNTR_1_START_DELAY,
                                                        SET_EVT_CNTR_1_END_DELAY));
    m_completionQueue.enqueue(USBDBG_SET_EVT_CNTR_1_CPL);
    command();
}

void OpenMVPluginIO::profileReset()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_PROFILE_RESET_CPL);
        m_port->profileReset();
        return;
    }

    QByteArray buffer;
    serializeByte(buffer, __USBDBG_CMD);
    serializeByte(buffer, __USBDBG_PROFILE_RESET);
    serializeLong(buffer, PROFILE_RESET_PAYLOAD_LEN);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        int(),
                                                        PROFILE_RESET_START_DELAY,
                                                        PROFILE_RESET_END_DELAY));
    m_completionQueue.enqueue(USBDBG_PROFILE_RESET_CPL);
    command();
}

void OpenMVPluginIO::bootloaderStart()
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {gotBootloaderStart(false, int());});
        return;
    }

    QByteArray buffer;
    serializeLong(buffer, __BOOTLDR_START);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        BOOTLDR_START_RESPONSE_LEN,
                                                        BOOTLDR_START_START_DELAY,
                                                        BOOTLDR_START_END_DELAY,
                                                        !m_bootloaderFastMode));
    m_completionQueue.enqueue(BOOTLDR_START_CPL);
    command();
}

void OpenMVPluginIO::bootloaderReset()
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {bootloaderResetDone(false);});
        return;
    }

    QByteArray buffer;
    serializeLong(buffer, __BOOTLDR_RESET);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        int(),
                                                        BOOTLDR_RESET_START_DELAY,
                                                        BOOTLDR_RESET_END_DELAY,
                                                        !m_bootloaderFastMode));
    m_completionQueue.enqueue(BOOTLDR_RESET_CPL);
    command();
}

void OpenMVPluginIO::flashErase(int sector)
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {flashEraseDone(false);});
        return;
    }

    QByteArray buffer;
    serializeLong(buffer, __BOOTLDR_ERASE);
    serializeLong(buffer, sector);

    if(m_bootloaderFastMode)
    {
        buffer.append(QByteArray((m_bootloaderHS ? HS_CHUNK_SIZE : FS_CHUNK_SIZE) - 4, 0)); // padding
        // Add non-posted command to ensure sync (also ensures that the packet is not a multiple of 64 bytes)
        serializeLong(buffer, __BOOTLDR_QUERY);
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            BOOTLDR_QUERY_RESPONSE_LEN,
                                                            BOOTLDR_ERASE_START_DELAY,
                                                            BOOTLDR_ERASE_END_DELAY,
                                                            !m_bootloaderFastMode));
    }
    else
    {
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            int(),
                                                            BOOTLDR_ERASE_START_DELAY,
                                                            BOOTLDR_ERASE_END_DELAY));
    }

    m_completionQueue.enqueue(BOOTLDR_ERASE_CPL);
    command();
}

void OpenMVPluginIO::flashWrite(const QByteArray &data)
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {flashWriteDone(false);});
        return;
    }

    QByteArray buffer;
    serializeLong(buffer, __BOOTLDR_WRITE);
    buffer.append(data);

    if(m_bootloaderFastMode)
    {
        if(Utils::HostOsInfo::isMacHost() && (buffer.size() == (m_bootloaderHS ? HS_EP_SIZE : FS_EP_SIZE)))
        {
            // Add non-posted command to ensure sync (also ensures that the packet is not a multiple of 64 bytes)
            serializeLong(buffer, __BOOTLDR_QUERY);
            m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                                BOOTLDR_QUERY_RESPONSE_LEN,
                                                                BOOTLDR_WRITE_START_DELAY,
                                                                BOOTLDR_WRITE_END_DELAY,
                                                                !m_bootloaderFastMode));
        }
        else
        {
            m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                                int(),
                                                                BOOTLDR_WRITE_START_DELAY,
                                                                BOOTLDR_WRITE_END_DELAY,
                                                                !m_bootloaderFastMode));
        }
    }
    else
    {
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            int(),
                                                            BOOTLDR_WRITE_START_DELAY,
                                                            BOOTLDR_WRITE_END_DELAY));
    }

    m_completionQueue.enqueue(BOOTLDR_WRITE_CPL);
    command();
}

void OpenMVPluginIO::bootloaderQuery()
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {bootloaderQueryDone(int(), int(), int());});
        return;
    }

    QByteArray buffer;
    serializeLong(buffer, __BOOTLDR_QUERY);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        BOOTLDR_QUERY_RESPONSE_LEN,
                                                        BOOTLDR_QUERY_START_DELAY,
                                                        BOOTLDR_QUERY_END_DELAY,
                                                        !m_bootloaderFastMode));
    m_completionQueue.enqueue(BOOTLDR_QUERY_CPL);
    command();
}

void OpenMVPluginIO::bootloaderQSPIFErase(int sector)
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {bootloaderQSPIFEraseDone(false);});
        return;
    }

    QByteArray buffer;
    serializeLong(buffer, __BOOTLDR_QSPIF_ERASE);
    serializeLong(buffer, sector);

    if(m_bootloaderFastMode)
    {
        buffer.append(QByteArray((m_bootloaderHS ? HS_CHUNK_SIZE : FS_CHUNK_SIZE) - 4, 0)); // padding
        // Add non-posted command to ensure sync (also ensures that the packet is not a multiple of 64 bytes)
        serializeLong(buffer, __BOOTLDR_QUERY);
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            BOOTLDR_QUERY_RESPONSE_LEN,
                                                            BOOTLDR_QSPIF_ERASE_START_DELAY,
                                                            BOOTLDR_QSPIF_ERASE_END_DELAY,
                                                            !m_bootloaderFastMode));
    }
    else
    {
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            int(),
                                                            BOOTLDR_QSPIF_ERASE_START_DELAY,
                                                            BOOTLDR_QSPIF_ERASE_END_DELAY));
    }

    m_completionQueue.enqueue(BOOTLDR_QSPIF_ERASE_CPL);
    command();
}

void OpenMVPluginIO::bootloaderQSPIFWrite(const QByteArray &data)
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {bootloaderQSPIFWriteDone(false);});
        return;
    }

    QByteArray buffer;
    serializeLong(buffer, __BOOTLDR_QSPIF_WRITE);
    buffer.append(data);

    if(m_bootloaderFastMode)
    {
        if(Utils::HostOsInfo::isMacHost() && (buffer.size() == (m_bootloaderHS ? HS_EP_SIZE : FS_EP_SIZE)))
        {
            // Add non-posted command to ensure sync (also ensures that the packet is not a multiple of 64 bytes)
            serializeLong(buffer, __BOOTLDR_QUERY);
            m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                                BOOTLDR_QUERY_RESPONSE_LEN,
                                                                BOOTLDR_QSPIF_WRITE_START_DELAY,
                                                                BOOTLDR_QSPIF_WRITE_END_DELAY,
                                                                !m_bootloaderFastMode));
        }
        else
        {
            m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                                int(),
                                                                BOOTLDR_QSPIF_WRITE_START_DELAY,
                                                                BOOTLDR_QSPIF_WRITE_END_DELAY,
                                                                !m_bootloaderFastMode));
        }
    }
    else
    {
        m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                            int(),
                                                            BOOTLDR_QSPIF_WRITE_START_DELAY,
                                                            BOOTLDR_QSPIF_WRITE_END_DELAY));
    }

    m_completionQueue.enqueue(BOOTLDR_QSPIF_WRITE_CPL);
    command();
}

void OpenMVPluginIO::bootloaderQSPIFLayout()
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {bootloaderQSPIFLayoutDone(int(), int(), int());});
        return;
    }

    QByteArray buffer;
    serializeLong(buffer, __BOOTLDR_QSPIF_LAYOUT);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        BOOTLDR_QSPIF_LAYOUT_RESPONSE_LEN,
                                                        BOOTLDR_QSPIF_LAYOUT_START_DELAY,
                                                        BOOTLDR_QSPIF_LAYOUT_END_DELAY,
                                                        !m_bootloaderFastMode));
    m_completionQueue.enqueue(BOOTLDR_QSPIF_LAYOUT_CPL);
    command();
}

void OpenMVPluginIO::bootloaderQSPIFMemtest()
{
    if (m_v2ProtocolEnabled) {
        QTimer::singleShot(0, this, [this] {bootloaderQSPIFMemtestDone(false);});
        return;
    }

    QByteArray buffer;
    serializeLong(buffer, __BOOTLDR_QSPIF_MEMTEST);
    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(buffer,
                                                        BOOTLDR_QSPIF_MEMTEST_RESPONSE_LEN,
                                                        BOOTLDR_QSPIF_MEMTEST_START_DELAY,
                                                        BOOTLDR_QSPIF_MEMTEST_END_DELAY,
                                                        !m_bootloaderFastMode));
    m_completionQueue.enqueue(BOOTLDR_QSPIF_MEMTEST_CPL);
    command();
}

void OpenMVPluginIO::close()
{
    if (m_v2ProtocolEnabled) {
        m_completionQueue.enqueue(V2_CLOSE_CPL);
        m_port->close();
        return;
    }

    m_postedQueue.enqueue(OpenMVPluginSerialPortCommand(QByteArray(),
                                                        int(),
                                                        int(),
                                                        int()));
    m_completionQueue.enqueue(CLOSE_CPL);
    command();
}

QByteArray OpenMVPluginIO::pasrsePrintData(const QByteArray &data)
{
    enum
    {
        ASCII,
        UTF_8,
        EXIT_0,
        EXIT_1
    }
    int_stateMachine = ASCII;
    bool strip_newline = false;

    QByteArray int_shiftReg = QByteArray();
    QByteArray int_frameBufferData = QByteArray();

    QByteArray buffer;

    for(int i = 0, j = data.size(); i < j; i++)
    {
        if((int_stateMachine == UTF_8) && ((data.at(i) & 0xC0) != 0x80))
        {
            int_stateMachine = ASCII;
        }

        if((int_stateMachine == EXIT_0) && ((data.at(i) & 0xFF) != 0x00))
        {
            int_stateMachine = ASCII;
        }

        switch(int_stateMachine)
        {
            case ASCII:
            {
                if(((data.at(i) & 0xE0) == 0xC0)
                || ((data.at(i) & 0xF0) == 0xE0)
                || ((data.at(i) & 0xF8) == 0xF0)
                || ((data.at(i) & 0xFC) == 0xF8)
                || ((data.at(i) & 0xFE) == 0xFC)) // UTF_8
                {
                    int_shiftReg.clear();

                    int_stateMachine = UTF_8;
                }
                else if((data.at(i) & 0xFF) == 0xFF)
                {
                    int_stateMachine = EXIT_0;
                }
                else if((data.at(i) & 0xC0) == 0x80)
                {
                    int_frameBufferData.append(data.at(i));
                }
                else if((data.at(i) & 0xFF) == 0xFE)
                {
                    int size = int_frameBufferData.size();
                    QByteArray temp;

                    for(int k = 0, l = (size / 4) * 4; k < l; k += 4)
                    {
                        int x = 0;
                        x |= (int_frameBufferData.at(k + 0) & 0x3F) << 0;
                        x |= (int_frameBufferData.at(k + 1) & 0x3F) << 6;
                        x |= (int_frameBufferData.at(k + 2) & 0x3F) << 12;
                        x |= (int_frameBufferData.at(k + 3) & 0x3F) << 18;
                        temp.append((x >> 0) & 0xFF);
                        temp.append((x >> 8) & 0xFF);
                        temp.append((x >> 16) & 0xFF);
                    }

                    if((size % 4) == 3) // 2 bytes -> 16-bits -> 24-bits sent
                    {
                        int x = 0;
                        x |= (int_frameBufferData.at(size - 3) & 0x3F) << 0;
                        x |= (int_frameBufferData.at(size - 2) & 0x3F) << 6;
                        x |= (int_frameBufferData.at(size - 1) & 0x0F) << 12;
                        temp.append((x >> 0) & 0xFF);
                        temp.append((x >> 8) & 0xFF);
                    }

                    if((size % 4) == 2) // 1 byte -> 8-bits -> 16-bits sent
                    {
                        int x = 0;
                        x |= (int_frameBufferData.at(size - 2) & 0x3F) << 0;
                        x |= (int_frameBufferData.at(size - 1) & 0x03) << 6;
                        temp.append((x >> 0) & 0xFF);
                    }

                    QPixmap pixmap = QPixmap::fromImage(QImage::fromData(temp));

                    if(!pixmap.isNull())
                    {
                        emit frameBufferData(pixmap);
                    }

                    int_frameBufferData.clear();

                    strip_newline = true;
                }
                else if((data.at(i) & 0x80) == 0x00) // ASCII
                {
                    if(strip_newline)
                    {
                        if(data.at(i) == '\r') break;
                        strip_newline = false;
                        if(data.at(i) == '\n') break;
                    }

                    buffer.append(data.at(i));
                }

                break;
            }

            case UTF_8:
            {
                if((((int_shiftReg.at(0) & 0xE0) == 0xC0) && (int_shiftReg.size() == 1))
                || (((int_shiftReg.at(0) & 0xF0) == 0xE0) && (int_shiftReg.size() == 2))
                || (((int_shiftReg.at(0) & 0xF8) == 0xF0) && (int_shiftReg.size() == 3))
                || (((int_shiftReg.at(0) & 0xFC) == 0xF8) && (int_shiftReg.size() == 4))
                || (((int_shiftReg.at(0) & 0xFE) == 0xFC) && (int_shiftReg.size() == 5)))
                {
                    buffer.append(int_shiftReg + data.at(i));

                    int_stateMachine = ASCII;
                }

                break;
            }

            case EXIT_0:
            {
                int_stateMachine = EXIT_1;

                break;
            }

            case EXIT_1:
            {
                int_stateMachine = ASCII;

                break;
            }
        }

        int_shiftReg = int_shiftReg.append(data.at(i)).right(5);
    }

    return buffer;
}

} // namespace Internal
} // namespace OpenMV
