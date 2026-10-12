#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2025 NXP
# Copyright 2025 Oidis
#
# SPDX-License-Identifier: BSD-3-Clause
"""Module with Dapper interfaces."""

import importlib
import inspect
import logging
import pkgutil
from pathlib import Path

from .interface import Interface

logger = logging.getLogger("InterfaceFactory")


class InterfaceFactory:
    """Class with factory method providing available interfaces."""

    @staticmethod
    def load_interfaces() -> list[type[Interface]]:
        """Load all available interfaces."""
        interfaces = []
        pkg_dir = Path(__file__).resolve().parent
        for _, module_name, _ in pkgutil.iter_modules([str(pkg_dir)]):
            module = importlib.import_module(f"{__name__}.{module_name}")
            for _, obj in inspect.getmembers(module, inspect.isclass):
                if issubclass(obj, Interface) and obj is not Interface:
                    if obj.is_available():
                        logger.info(f"Found interface {obj.__name__}")
                        interfaces.append(obj)

        interfaces.sort(key=lambda i: i.priority(), reverse=False)
        return interfaces


__all__ = [
    "Interface",
    "InterfaceFactory",
]
