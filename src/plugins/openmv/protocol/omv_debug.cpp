/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Protocol Camera Interface
 *
 * This module provides a logging class.
 */

#include "omv_debug.h"

namespace omv {

QAtomicInt OMVDebug::s_enabled(0);

void OMVDebug::setEnabled(bool on)
{
    s_enabled.storeRelaxed(on ? 1 : 0);
}

bool OMVDebug::isEnabled()
{
    return s_enabled.loadRelaxed() != 0;
}

OMVDebug::OMVDebug()
    : m_dbg(isEnabled() ? std::optional<QDebug>(QDebug(QtDebugMsg)) : std::nullopt)
{
}

OMVDebug::OMVDebug(bool enabled)
    : m_dbg((enabled && isEnabled()) ? std::optional<QDebug>(QDebug(QtDebugMsg)) : std::nullopt)
{
}

OMVDebug &OMVDebug::operator<<(Manipulator manip)
{
    if (m_dbg) {
        manip(*m_dbg);
    }
    return *this;
}

OMVDebug &OMVDebug::noquote()
{
    if (m_dbg) {
        m_dbg->noquote();
    }
    return *this;
}

OMVDebug &OMVDebug::nospace()
{
    if (m_dbg) {
        m_dbg->nospace();
    }
    return *this;
}

OMVDebug &OMVDebug::space()
{
    if (m_dbg) {
        m_dbg->space();
    }
    return *this;
}

} // namespace omv
