// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#pragma once

#include "utils_global.h"

#include "osspecificaspects.h"

#include <optional>

#ifdef Q_OS_WIN
#define QTC_HOST_EXE_SUFFIX QTC_WIN_EXE_SUFFIX
#else
#define QTC_HOST_EXE_SUFFIX ""
#endif // Q_OS_WIN

namespace Utils { class FilePath; }

// The "Host" is the machine QtCreator is running on.

namespace Utils::HostOsInfo {

// OPENMV-DIFF //
// QTCREATOR_UTILS_EXPORT constexpr OsType hostOs()
// OPENMV-DIFF //
constexpr OsType hostOs()
// OPENMV-DIFF //
{
#if defined(Q_OS_WIN)
    return OsTypeWindows;
#elif defined(Q_OS_LINUX)
    return OsTypeLinux;
#elif defined(Q_OS_MAC)
    return OsTypeMac;
#elif defined(Q_OS_UNIX)
    return OsTypeOtherUnix;
#else
    return OsTypeOther;
#endif
}

//! Returns the architecture of the host system.
QTCREATOR_UTILS_EXPORT OsArch hostArchitecture();

//! Returns the architecture the running binary was compiled for.
QTCREATOR_UTILS_EXPORT OsArch binaryArchitecture();

// OPENMV-DIFF //
//! Returns true if an x86_64 binary is running under Rosetta on Apple silicon.
//! Upstream removed this in Qt Creator 20; OpenMV re-adds it because
//! hostArchitecture() reports the binary's arch (x86_64) under Rosetta, so it
//! cannot alone distinguish Apple-silicon hardware when the IDE runs translated.
QTCREATOR_UTILS_EXPORT bool isRunningUnderRosetta();
// OPENMV-DIFF //

// OPENMV-DIFF //
// QTCREATOR_UTILS_EXPORT constexpr bool isWindowsHost() { return hostOs() == OsTypeWindows; }
// OPENMV-DIFF //
constexpr bool isWindowsHost() { return hostOs() == OsTypeWindows; }
// OPENMV-DIFF //
// OPENMV-DIFF //
// QTCREATOR_UTILS_EXPORT constexpr bool isLinuxHost() { return hostOs() == OsTypeLinux; }
// OPENMV-DIFF //
constexpr bool isLinuxHost() { return hostOs() == OsTypeLinux; }
// OPENMV-DIFF //
// OPENMV-DIFF //
// QTCREATOR_UTILS_EXPORT constexpr bool isMacHost() { return hostOs() == OsTypeMac; }
// OPENMV-DIFF //
constexpr bool isMacHost() { return hostOs() == OsTypeMac; }
// OPENMV-DIFF //
// OPENMV-DIFF //
// QTCREATOR_UTILS_EXPORT constexpr bool isAnyUnixHost()
// OPENMV-DIFF //
constexpr bool isAnyUnixHost()
// OPENMV-DIFF //
{
#ifdef Q_OS_UNIX
    return true;
#else
    return false;
#endif
}

// OPENMV-DIFF //
//! Returns the case sensitivity of file names on the host file system.
//! Upstream removed this in Qt Creator 20; OpenMV re-adds it for host-global
//! path prefix comparisons (Windows and macOS are case-insensitive).
constexpr Qt::CaseSensitivity fileNameCaseSensitivity()
{
    return (hostOs() == OsTypeWindows || hostOs() == OsTypeMac)
               ? Qt::CaseInsensitive
               : Qt::CaseSensitive;
}
// OPENMV-DIFF //

QTCREATOR_UTILS_EXPORT QString withExecutableSuffix(const QString &executable);

// OPENMV-DIFF //
// QTCREATOR_UTILS_EXPORT constexpr QChar pathListSeparator()
// OPENMV-DIFF //
constexpr QChar pathListSeparator()
// OPENMV-DIFF //
{
    return OsSpecificAspects::pathListSeparator(hostOs());
}

// OPENMV-DIFF //
// QTCREATOR_UTILS_EXPORT constexpr Qt::KeyboardModifier controlModifier()
// OPENMV-DIFF //
constexpr Qt::KeyboardModifier controlModifier()
// OPENMV-DIFF //
{
    return OsSpecificAspects::controlModifier(hostOs());
}

QTCREATOR_UTILS_EXPORT std::optional<quint64> totalMemoryInstalledInBytes();

QTCREATOR_UTILS_EXPORT const FilePath &root(); // Do not use.

} // namespace Utils::HostOsInfo
