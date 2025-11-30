/* SPDX-License-Identifier: MIT
 *
 * Copyright (c) 2025 OpenMV, LLC.
 *
 * OpenMV Protocol Exceptions
 *
 * This module defines all the custom exceptions used in the OpenMV
 * Protocol implementation for proper error handling and debugging.
 */

#pragma once

#include <QtCore/QString>
#include <stdexcept>

namespace omv {

class OMVPException : public std::runtime_error
{
public:
    /*
        Base exception for OpenMV protocol errors
    */
    explicit OMVPException(const QString &message)
        : std::runtime_error(message.toUtf8().constData()),
        traceback(QString())
    {
    }

    QString traceback;
};

class OMVPTimeoutException : public OMVPException
{
public:
    /*
        Raised when a protocol operation times out
    */
    explicit OMVPTimeoutException(const QString &message)
        : OMVPException(message)
    {
    }
};

class OMVPChecksumException : public OMVPException
{
public:
    /*
        Raised when CRC validation fails
    */
    explicit OMVPChecksumException(const QString &message)
        : OMVPException(message)
    {
    }
};

class OMVPSequenceException : public OMVPException
{
public:
    /*
        Raised when sequence number validation fails
    */
    explicit OMVPSequenceException(const QString &message)
        : OMVPException(message)
    {
    }
};

class OMVPResyncException : public OMVPException
{
public:
    /*
        Raised to indicate that a resync was performed and operation should be retried
    */
    explicit OMVPResyncException(const QString &message = QStringLiteral("Resync performed, retry operation"))
        : OMVPException(message)
    {
    }
};

} // namespace omv
