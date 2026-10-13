#!/usr/bin/env python
# -*- coding: UTF-8 -*-
#
# Copyright 2024-2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

# pylint: disable=invalid-name, import-error

"""Add hidden imports, binaries, and data for this package."""

import importlib_metadata
from PyInstaller.utils.hooks import (
    PY_DYLIB_PATTERNS,
    collect_data_files,
    collect_dynamic_libs,
    copy_metadata,
)

hidden_imports_set = {"spsdk_pyocd", "pyocd"}
for ep in importlib_metadata.entry_points(group="pyocd.probe"):
    hidden_imports_set.add(ep.module)
hiddenimports = list(hidden_imports_set)

datas = collect_data_files("pyocd.debug.sequences")
datas += copy_metadata("spsdk_pyocd")
datas += copy_metadata("pyocd")

binaries = collect_dynamic_libs("cmsis_pack_manager", search_patterns=PY_DYLIB_PATTERNS + ["*.so"])
