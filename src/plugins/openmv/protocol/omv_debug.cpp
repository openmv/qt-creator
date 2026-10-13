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
std::function<void(const QString &)> OMVDebug::s_sink;

void OMVDebug::setEnabled(bool on)
{
    s_enabled.storeRelaxed(on ? 1 : 0);
}

bool OMVDebug::isEnabled()
{
    return s_enabled.loadRelaxed() != 0;
}

void OMVDebug::setSink(std::function<void(const QString &)> sink)
{
    s_sink = std::move(sink);
}

OMVDebug::OMVDebug()
    : m_buffer()
    , m_dbg(isEnabled() ? std::optional<QDebug>(std::in_place, &m_buffer) : std::nullopt)
{
}

OMVDebug::OMVDebug(bool enabled)
    : m_buffer()
    , m_dbg((enabled && isEnabled()) ? std::optional<QDebug>(std::in_place, &m_buffer) : std::nullopt)
{
}

OMVDebug::~OMVDebug()
{
    if (m_dbg) {
        m_dbg.reset(); // destroy the QDebug so it flushes into m_buffer
        if (s_sink && !m_buffer.isEmpty()) {
            s_sink(m_buffer + QLatin1Char('\n'));
        }
    }
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
