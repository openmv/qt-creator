/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Protocol Camera Interface
 *
 * This module provides a logging class.
 */

#pragma once

#include <QtCore/QDebug>
#include <QtCore/QString>
#include <QtCore/QAtomicInt>
#include <functional>
#include <optional>

namespace omv {

class OMVDebug
{
public:
    // Global enable/disable for all OmvDebug instances.
    static void setEnabled(bool on);
    static bool isEnabled();

    // Where formatted log lines go. Set once by the host (the IDE routes them to
    // the Serial Terminal). When unset, lines are dropped rather than printed to
    // the default Qt message handler / system console.
    static void setSink(std::function<void(const QString &)> sink);

    // Default ctor uses global flag.
    OMVDebug();

    // Optional per-call gating in addition to the global flag.
    explicit OMVDebug(bool enabled);

    // Flushes the accumulated line to the sink.
    ~OMVDebug();

    // The QDebug writes into m_buffer, so the object must not be copied/moved
    // (relies on C++17 guaranteed copy elision in the omvDebug() helpers).
    OMVDebug(const OMVDebug &) = delete;
    OMVDebug &operator=(const OMVDebug &) = delete;

    template <typename T>
    OMVDebug &operator<<(const T &value)
    {
        if (m_dbg) {
            (*m_dbg) << value;
        }
        return *this;
    }

    // QDebug manipulators (Qt::endl, etc.)
    using Manipulator = QDebug &(*)(QDebug &);
    OMVDebug &operator<<(Manipulator manip);

    // Common QDebug modifiers
    OMVDebug &noquote();
    OMVDebug &nospace();
    OMVDebug &space();

private:
    static QAtomicInt s_enabled;
    static std::function<void(const QString &)> s_sink;

    QString m_buffer;
    std::optional<QDebug> m_dbg;
};

// Convenience helper (mirrors qDebug() style)
inline OMVDebug omvDebug()
{
    return OMVDebug();
}

// Convenience helper with extra gating
inline OMVDebug omvDebug(bool enabled)
{
    return OMVDebug(enabled);
}

} // namespace omv
