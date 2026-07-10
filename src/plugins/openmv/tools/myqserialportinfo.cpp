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

#include <QtCore>

#include <utils/qtcprocess.h>

#include "myqserialportinfo.h"

namespace OpenMV {
namespace Internal {

extern QMutex dfu_util_working;

MyQSerialPortInfo::MyQSerialPortInfo()
{
    m_info = QSerialPortInfo();
    m_isNull = m_info.isNull();
    m_description = m_info.description();
    m_hasProductIdentifier = m_info.hasProductIdentifier();
    m_hasVendorIdentifier = m_info.hasVendorIdentifier();
    m_manufacturer = m_info.manufacturer();
    m_productIdentifier = m_info.productIdentifier();
    m_serialNumber = m_info.serialNumber();
    m_systemLocation = m_info.systemLocation();
    m_vendorIdentifier = m_info.vendorIdentifier();
}

MyQSerialPortInfo::MyQSerialPortInfo(const QSerialPortInfo &info)
{
    m_info = info;
    m_isNull = info.isNull();
    m_description = info.description();
    m_hasProductIdentifier = info.hasProductIdentifier();
    m_hasVendorIdentifier = info.hasVendorIdentifier();
    m_manufacturer = info.manufacturer();
    m_productIdentifier = info.productIdentifier();
    m_serialNumber = info.serialNumber();
    m_systemLocation = info.systemLocation();
    m_vendorIdentifier = info.vendorIdentifier();

    if (m_isNull) return;

    // The purpose of this class is to work around an issue in QSerialPortInfo
    // where it returns the PID/VID of the USB HUB the serial port is attached
    // to instead of the PID/VID of the serial port itself on linux...

#ifdef Q_OS_LINUX
    if(!dfu_util_working.try_lock()) return;

    QFile file(QStringLiteral("/sys/class/tty/") + info.portName() + "/device/uevent");

    if(file.open(QIODevice::ReadOnly))
    {
        QString bytes = QString::fromLatin1(file.readAll());
        QRegularExpressionMatch m = QRegularExpression(QStringLiteral("PRODUCT=([a-fA-F0-9]+)/([a-fA-F0-9]+)")).match(bytes);

        if(m.hasMatch())
        {
            bool vidOk;
            quint16 vid = m.captured(1).toInt(&vidOk, 16);
            bool pidOk;
            quint16 pid = m.captured(2).toInt(&pidOk, 16);

            if(vidOk) // trust vid we pulled more
            {
                m_hasVendorIdentifier = vidOk;
                m_vendorIdentifier = vid;
            }

            if(pidOk) // trust pid we pulled more
            {
                m_hasProductIdentifier = pidOk;
                m_productIdentifier = pid;
            }
        }
    }

    if(m_hasVendorIdentifier && m_hasProductIdentifier)
    {
        Utils::Process process;
        std::chrono::seconds timeout(10);
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(QStringLiteral("lsusb")), QStringList()
                                              << QStringLiteral("-v")
                                              << QStringLiteral("-d")
                                              << QString(QStringLiteral("%1:%2")).arg(m_vendorIdentifier, 4, 16, QLatin1Char('0')).arg(m_productIdentifier, 4, 16, QLatin1Char('0'))));
        process.runBlocking(timeout, Utils::EventLoopMode::On);

        if(process.result() == Utils::ProcessResult::FinishedWithSuccess)
        {
            QString bytes = process.stdOut();
            QRegularExpressionMatch m = QRegularExpression(QStringLiteral("iManufacturer\\s+\\d+\\s+(.+?)"
                                                                          "iProduct\\s+\\d+\\s+(.+?)"
                                                                          "iSerial\\s+\\d+\\s+(.+?)"
                                                                          "bNumConfigurations"),
                                                                          QRegularExpression::DotMatchesEverythingOption).match(bytes);

            if(m.hasMatch())
            {
                if(m_manufacturer.isEmpty()) m_manufacturer = m.captured(1).trimmed();
                if(m_description.isEmpty()) m_description = m.captured(2).trimmed();
                if(m_serialNumber.isEmpty()) m_serialNumber = m.captured(3).trimmed();
            }
        }
    }

    if(m_systemLocation.isEmpty()) m_systemLocation = QStringLiteral("/dev/") + info.portName();

    dfu_util_working.unlock();
#endif
}

} // namespace Internal
} // namespace OpenMV
