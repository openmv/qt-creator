#ifndef MYQSERIALPORTINFO_H
#define MYQSERIALPORTINFO_H

#include <QSerialPortInfo>

namespace OpenMV {
namespace Internal {

class MyQSerialPortInfo
{
public:
    explicit MyQSerialPortInfo();
    explicit MyQSerialPortInfo(const QSerialPortInfo &info);
    QString description() const { return m_description; }
    bool hasProductIdentifier() const { return m_hasProductIdentifier; }
    bool hasVendorIdentifier() const { return m_hasVendorIdentifier; }
    QString manufacturer() const { return m_manufacturer; }
    QString portName() const { return m_info.portName(); }
    quint16 productIdentifier() const { return m_productIdentifier; }
    QString serialNumber() const { return m_serialNumber; }
    QString systemLocation() const { return m_systemLocation; }
    quint16 vendorIdentifier() const { return m_vendorIdentifier; }
    MyQSerialPortInfo &operator=(const MyQSerialPortInfo &other);
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
