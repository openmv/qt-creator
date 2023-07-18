#include <QtCore>

#include <utils/qtcprocess.h>

#include "myqserialportinfo.h"

namespace OpenMV {
namespace Internal {

MyQSerialPortInfo::MyQSerialPortInfo()
{
    m_info = QSerialPortInfo();
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
    m_description = info.description();
    m_hasProductIdentifier = info.hasProductIdentifier();
    m_hasVendorIdentifier = info.hasVendorIdentifier();
    m_manufacturer = info.manufacturer();
    m_productIdentifier = info.productIdentifier();
    m_serialNumber = info.serialNumber();
    m_systemLocation = info.systemLocation();
    m_vendorIdentifier = info.vendorIdentifier();

    // The purpose of this class is to work around an issue in QSerialPortInfo
    // where it returns the PID/VID of the USB HUB the serial port is attached
    // to instead of the PID/VID of the serial port itself on linux...

#ifdef Q_OS_LINUX
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
        Utils::QtcProcess process;
        process.setTextChannelMode(Utils::Channel::Output, Utils::TextChannelMode::MultiLine);
        process.setTextChannelMode(Utils::Channel::Error, Utils::TextChannelMode::MultiLine);
        process.setCommand(Utils::CommandLine(Utils::FilePath::fromString(QStringLiteral("lsusb")), QStringList()
                                              << QStringLiteral("-v")
                                              << QStringLiteral("-d")
                                              << QString(QStringLiteral("%1:%2")).arg(m_vendorIdentifier, 4, 16, QLatin1Char('0')).arg(m_productIdentifier, 4, 16, QLatin1Char('0'))));
        process.runBlocking(Utils::EventLoopMode::On);

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
#endif
}

MyQSerialPortInfo &MyQSerialPortInfo::operator=(const MyQSerialPortInfo &other)
{
    m_info = other.m_info;
    m_description = other.m_description;
    m_hasProductIdentifier = other.m_hasProductIdentifier;
    m_hasVendorIdentifier = other.m_hasVendorIdentifier;
    m_manufacturer = other.m_manufacturer;
    m_productIdentifier = other.m_productIdentifier;
    m_serialNumber = other.m_serialNumber;
    m_systemLocation = other.m_systemLocation;
    m_vendorIdentifier = other.m_vendorIdentifier;

    return *this;
}

} // namespace Internal
} // namespace OpenMV
