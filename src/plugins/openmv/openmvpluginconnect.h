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

#ifndef OPENMVPLUGINCONNECT_H
#define OPENMVPLUGINCONNECT_H

#include <QtCore>

#include "tools/myqserialportinfo.h"
#include "utils/qtcassert.h"

#define CONNECT_END() \
do { \
    m_working = false; \
    QTimer::singleShot(0, this, &OpenMVPlugin::workingDone); \
    return; \
} while(0)

#define RECONNECT_END() \
do { \
    m_working = false; \
    QTimer::singleShot(0, this, [this] {connectClicked();}); \
    return; \
} while(0)

#define RECONNECT_WAIT_END() \
do { \
    m_working = false; \
    QTimer::singleShot(0, this, [this] {connectClicked(false, QString(), false, false, false, true);}); \
    return; \
} while(0)

#define RECONNECT_AND_FORCEBOOTLOADER_END(forceFlashFSErase, previousMapping, romfsAccess) \
do { \
    m_working = false; \
    if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QTimer::singleShot(0, this, [this, forceFlashFSErase, previousMapping, romfsAccess] {connectClicked(true, QString(), forceFlashFSErase, false, false, false, previousMapping, romfsAccess);}); \
    else if(m_autoUpdate == QStringLiteral("release")) QTimer::singleShot(0, this, [this, forceFlashFSErase, previousMapping, romfsAccess] {connectClicked(true, QString(), m_autoErase || forceFlashFSErase, false, false, false, previousMapping, romfsAccess);}); \
    else if(m_autoUpdate == QStringLiteral("developement")) QTimer::singleShot(0, this, [this, forceFlashFSErase, previousMapping, romfsAccess] {connectClicked(true, QString(), m_autoErase || forceFlashFSErase, false, true, false, previousMapping, romfsAccess);}); \
    else if(QFileInfo(m_autoUpdate).isFile()) QTimer::singleShot(0, this, [this, forceFlashFSErase, previousMapping, romfsAccess] {connectClicked(true, m_autoUpdate, m_autoErase || forceFlashFSErase, false, false, false, previousMapping, romfsAccess);}); \
    else if(m_autoErase) QTimer::singleShot(0, this, [this, previousMapping, romfsAccess] {connectClicked(true, QString(), true, true, false, false, previousMapping, romfsAccess);}); \
    return; \
} while(0)

#define CLOSE_CONNECT_END() \
do { \
    QEventLoop m_loop; \
    connect(m_iodevice, &OpenMVPluginIO::closeResponse, &m_loop, &QEventLoop::quit); \
    m_iodevice->close(); \
    m_loop.exec(); \
    m_working = false; \
    QTimer::singleShot(0, this, &OpenMVPlugin::workingDone); \
    return; \
} while(0)

#define CLOSE_RECONNECT_END() \
do { \
    QEventLoop m_loop; \
    connect(m_iodevice, &OpenMVPluginIO::closeResponse, &m_loop, &QEventLoop::quit); \
    m_iodevice->close(); \
    m_loop.exec(); \
    m_working = false; \
    QTimer::singleShot(0, this, [this] {connectClicked();}); \
    return; \
} while(0)

#define CLOSE_RECONNECT_WAIT_END() \
do { \
    QEventLoop m_loop; \
    connect(m_iodevice, &OpenMVPluginIO::closeResponse, &m_loop, &QEventLoop::quit); \
    m_iodevice->close(); \
    m_loop.exec(); \
    m_working = false; \
    QTimer::singleShot(0, this, [this] {connectClicked(false, QString(), false, false, false, true);}); \
    return; \
} while(0)

#define CLOSE_RECONNECT_AND_FORCEBOOTLOADER_END(forceFlashFSErase, previousMapping, romfsAccess) \
do { \
    QEventLoop m_loop; \
    connect(m_iodevice, &OpenMVPluginIO::closeResponse, &m_loop, &QEventLoop::quit); \
    m_iodevice->close(); \
    m_loop.exec(); \
    m_working = false; \
    if((m_autoUpdate.isEmpty()) && (!m_autoErase)) QTimer::singleShot(0, this, [this, forceFlashFSErase, previousMapping, romfsAccess] {connectClicked(true, QString(), forceFlashFSErase, false, false, false, previousMapping, romfsAccess);}); \
    else if(m_autoUpdate == QStringLiteral("release")) QTimer::singleShot(0, this, [this, forceFlashFSErase, previousMapping, romfsAccess] {connectClicked(true, QString(), m_autoErase || forceFlashFSErase, false, false, false, previousMapping, romfsAccess);}); \
    else if(m_autoUpdate == QStringLiteral("developement")) QTimer::singleShot(0, this, [this, forceFlashFSErase, previousMapping, romfsAccess] {connectClicked(true, QString(), m_autoErase || forceFlashFSErase, false, true, false, previousMapping, romfsAccess);}); \
    else if(QFileInfo(m_autoUpdate).isFile()) QTimer::singleShot(0, this, [this, forceFlashFSErase, previousMapping, romfsAccess] {connectClicked(true, m_autoUpdate, m_autoErase || forceFlashFSErase, false, false, false, previousMapping, romfsAccess);}); \
    else if(m_autoErase) QTimer::singleShot(0, this, [this, previousMapping, romfsAccess] {connectClicked(true, QString(), true, true, false, false, previousMapping, romfsAccess);}); \
    return; \
} while(0)

namespace OpenMV {
namespace Internal {

bool validPort(const QJsonDocument &settings, const QString &serialNumberFilter, const MyQSerialPortInfo &port);

} // namespace Internal
} // namespace OpenMV

#endif // OPENMVPLUGINCONNECT_H
