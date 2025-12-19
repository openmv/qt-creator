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
#include <QtCore/QAtomicInt>
#include <optional>

namespace omv {

class OMVDebug
{
public:
    // Global enable/disable for all OmvDebug instances.
    static void setEnabled(bool on);
    static bool isEnabled();

    // Default ctor uses global flag.
    OMVDebug();

    // Optional per-call gating in addition to the global flag.
    explicit OMVDebug(bool enabled);

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
