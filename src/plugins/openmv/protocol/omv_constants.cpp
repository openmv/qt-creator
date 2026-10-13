/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Protocol Constants
 *
 * This module defines all the constants used in the OpenMV Protocol
 * including opcodes, status codes, flags, and other definitions.
 */

#include "omv_constants.h"

namespace omv {

QString opcodeName(quint8 opcode)
{
    switch (opcode) {
    case OMVPOpcode::PROTO_SYNC:     return QStringLiteral("PROTO_SYNC");
    case OMVPOpcode::PROTO_GET_CAPS: return QStringLiteral("PROTO_GET_CAPS");
    case OMVPOpcode::PROTO_SET_CAPS: return QStringLiteral("PROTO_SET_CAPS");
    case OMVPOpcode::PROTO_STATS:    return QStringLiteral("PROTO_STATS");

    case OMVPOpcode::SYS_RESET:      return QStringLiteral("SYS_RESET");
    case OMVPOpcode::SYS_BOOT:       return QStringLiteral("SYS_BOOT");
    case OMVPOpcode::SYS_INFO:       return QStringLiteral("SYS_INFO");
    case OMVPOpcode::SYS_EVENT:      return QStringLiteral("SYS_EVENT");

    case OMVPOpcode::CHANNEL_LIST:   return QStringLiteral("CHANNEL_LIST");
    case OMVPOpcode::CHANNEL_POLL:   return QStringLiteral("CHANNEL_POLL");
    case OMVPOpcode::CHANNEL_LOCK:   return QStringLiteral("CHANNEL_LOCK");
    case OMVPOpcode::CHANNEL_UNLOCK: return QStringLiteral("CHANNEL_UNLOCK");
    case OMVPOpcode::CHANNEL_SHAPE:  return QStringLiteral("CHANNEL_SHAPE");
    case OMVPOpcode::CHANNEL_SIZE:   return QStringLiteral("CHANNEL_SIZE");
    case OMVPOpcode::CHANNEL_READ:   return QStringLiteral("CHANNEL_READ");
    case OMVPOpcode::CHANNEL_WRITE:  return QStringLiteral("CHANNEL_WRITE");
    case OMVPOpcode::CHANNEL_IOCTL:  return QStringLiteral("CHANNEL_IOCTL");
    case OMVPOpcode::CHANNEL_EVENT:  return QStringLiteral("CHANNEL_EVENT");
    }

    // Unknown / out of range
    return QString();
}

QString eventTypeName(quint16 evt)
{
    switch (evt) {
    case OMVPEventType::CHANNEL_REGISTERED:
        return QStringLiteral("CHANNEL_REGISTERED");
    case OMVPEventType::CHANNEL_UNREGISTERED:
        return QStringLiteral("CHANNEL_UNREGISTERED");
    case OMVPEventType::SOFT_REBOOT:
        return QStringLiteral("SOFT_REBOOT");
    }

    return QString();
}

} // namespace omv
