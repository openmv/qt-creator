#ifndef MYQSERIALPORTINFO_H
#define MYQSERIALPORTINFO_H

#include <QSerialPortInfo>

namespace OpenMV {
namespace Internal {

class MyQSerialPortInfo
{
public:
    explicit MyQSerialPortInfo(const QSerialPortInfo &info);
    QString description() { return m_description; }
    bool hasProductIdentifier() { return m_hasProductIdentifier; }
    bool hasVendorIdentifier() { return m_hasVendorIdentifier; }
    QString manufacturer() { return m_manufacturer; }
    QString portName() { return m_info.portName(); }
    quint16 productIdentifier() { return m_productIdentifier; }
    QString serialNumber() { return m_serialNumber; }
    QString systemLocation() { return m_systemLocation; }
    quint16 vendorIdentifier() { return m_vendorIdentifier; }
private:
    QSerialPortInfo m_info;
    QString m_description;
    bool m_hasProductIdentifier;
    bool m_hasVendorIdentifier;
    QString m_manufacturer;
    quint16 m_productIdentifier;
    QString m_serialNumber;
    QString m_systemLocation;
    quint16 m_vendorIdentifier;
};

} // namespace Internal
} // namespace OpenMV

#endif // MYQSERIALPORTINFO_H
