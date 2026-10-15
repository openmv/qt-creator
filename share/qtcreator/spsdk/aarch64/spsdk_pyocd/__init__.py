#!/usr/bin/env python
# -*- coding: UTF-8 -*-
#
# Copyright 2024-2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause
"""Top-level package for PyOCD SW Debugger."""

__author__ = """NXP"""
__email__ = "spsdk@nxp.com"
__version__ = "0.3.4"

try:
    from spsdk.__version__ import version, version_tuple
except ImportError as exc:
    raise RuntimeError("Unable to detect SPSDK version. Is SPSDK installed?") from exc


MIN_SPSDK_VERSION = (2, 3)
MIN_SPSDK_VERSION_STR = ".".join(str(i) for i in MIN_SPSDK_VERSION)

if version_tuple < MIN_SPSDK_VERSION:
    raise RuntimeError(
        f"You are using old version of SPSDK ({version}). "
        f"Please update SPSDK to at least version: {MIN_SPSDK_VERSION_STR}"
    )

# pylint: disable=wrong-import-position
from .probe import DebugProbePyOCD  # noqa: E402

__all__ = ["DebugProbePyOCD"]
