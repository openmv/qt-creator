// Copyright (C) 2026 OpenMV, LLC.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

// OPENMV-DIFF //
// What other plugins may use of the ACP client: OpenMV IDE hosts the chat in
// its own AI mode instead of the Edit mode's right pane.

#pragma once

#include <QtGlobal>

#if defined(ACPCLIENT_LIBRARY)
#  define ACPCLIENT_EXPORT Q_DECL_EXPORT
#else
#  define ACPCLIENT_EXPORT Q_DECL_IMPORT
#endif

class QWidget;

namespace AcpClient {

// The chat widget (created on first use). The ACP plugin keeps ownership:
// a host must reparent it away again before the plugin is deleted.
ACPCLIENT_EXPORT QWidget *chatWidget();

} // namespace AcpClient
// OPENMV-DIFF //
