#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2024-2025 NXP
# Copyright 2025 Oidis
#
# SPDX-License-Identifier: BSD-3-Clause
"""This module provides interfaces for working with Dapper debug probes.

Components
---------
* DapperFactory: Factory class for creating Dapper instances
* DapperProbeInfo: Class containing probe information
* WebixDapper: Main Dapper implementation class
* WebixDapperWasm: WASM-based Dapper implementation
* Uint8Array: Type for handling byte arrays
* Interface: Enumeration of available interfaces
"""
# noqa: D205, D212, D415

from .core import Uint8Array
from .interfaces import Interface
from .webix_dapper import DapperFactory, DapperProbeInfo, WebixDapper
from .webix_dapper_wasm import WebixDapperWasm

if False:  # pylint: disable=using-constant-test,unused-import
    # help PyInstaller to find modules not imported directly, but dynamically
    from .interfaces import (  # pylint: disable=unused-import
        hid_interface,
        usb_v1_interface,
        usb_v2_interface,
    )


__all__ = [
    "DapperFactory",
    "DapperProbeInfo",
    "WebixDapper",
    "WebixDapperWasm",
    "Uint8Array",
    "Interface",
]
