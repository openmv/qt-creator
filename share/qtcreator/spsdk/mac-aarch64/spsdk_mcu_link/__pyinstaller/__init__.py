#!/usr/bin/env python
# -*- coding: UTF-8 -*-
#
# Copyright 2024-2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

"""Module containing all PyInstaller hooks."""

import os


def get_hook_dirs() -> list[str]:
    """Get list of directories where PyInstaller should look for hooks."""
    return [os.path.dirname(__file__)]
