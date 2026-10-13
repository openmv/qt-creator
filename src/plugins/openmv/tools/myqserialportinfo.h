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
    MyQSerialPortInfo(const MyQSerialPortInfo &other) = default;
    bool isNull() const { return m_isNull; }
    QString description() const { return m_description; }
    bool hasProductIdentifier() const { return m_hasProductIdentifier; }
    bool hasVendorIdentifier() const { return m_hasVendorIdentifier; }
    QString manufacturer() const { return m_manufacturer; }
    QString portName() const { return m_info.portName(); }
    quint16 productIdentifier() const { return m_productIdentifier; }
    QString serialNumber() const { return m_serialNumber; }
    QString systemLocation() const { return m_systemLocation; }
    quint16 vendorIdentifier() const { return m_vendorIdentifier; }
    MyQSerialPortInfo &operator=(const MyQSerialPortInfo &other) = default;
private:
    QSerialPortInfo m_info;
    bool m_isNull;
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
