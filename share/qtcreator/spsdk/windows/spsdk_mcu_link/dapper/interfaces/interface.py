#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2024-2025 NXP
# Copyright 2025 Oidis
#
# SPDX-License-Identifier: BSD-3-Clause
"""Base class for probe interfaces."""

from abc import abstractmethod
from typing import Generic, TypeVar

from ..core import Uint8Array

T = TypeVar("T")


class Interface(Generic[T]):
    """Base class for probe interfaces.

    This class provides a common interface for working with different types of debug probes.
    It defines basic properties and methods that all probe interfaces should implement.
    """

    def __init__(self, device: T) -> None:
        """Initialize Interface instance.

        :return: None
        """
        self._type: str = "unknown"
        self._device: T = device
        self.vid: int = 0
        self.pid: int = 0
        self.vendor: str = ""
        self.product: str = ""
        self.serial_no: str = ""
        self.packet_size: int = 0

    @classmethod
    @abstractmethod
    def is_available(cls) -> bool:
        """Returns true if interface is available."""
        raise NotImplementedError()

    @classmethod
    def priority(cls) -> int:
        """Returns priority. The lower the number, the higher the priority."""
        return 10

    @staticmethod
    def list_probes() -> list["Interface"]:
        """List available probes.

        :return: List of Interface instances
        """
        raise NotImplementedError("Probes listing not implemented")

    @property
    def type(self) -> str:
        """Get interface type.

        :return: Interface type string
        """
        return self._type

    @property
    def probe_id(self) -> str:
        """Get probe identifier.

        :return: Probe serial number
        :raises RuntimeError: If device is not initialized
        """
        if self._device is None:
            raise RuntimeError("Device not initialized")
        return self._device.serial_number  # type: ignore[attr-defined]

    @property
    def description(self) -> str:
        """Get interface description.

        :return: Description string combining vendor and product
        """
        return f"{self.vendor} {self.product}"

    def open(self) -> None:
        """Open the interface connection.

        :return: None
        :raises RuntimeError: If USB device is undefined
        """
        if self._device is None:
            raise RuntimeError("USB device is undefined.")

    def close(self) -> None:
        """Close the interface connection.

        :return: None
        :raises NotImplementedError: If not implemented in derived class
        """
        raise NotImplementedError(f"{self.__class__}.close() must be implemented.")

    def write(self, data: Uint8Array) -> None:
        """Write data to interface.

        :param data: Data to write
        :return: None
        :raises NotImplementedError: If not implemented in derived class
        """
        raise NotImplementedError(f"{self.__class__}.write() must be implemented.")

    def read(self) -> Uint8Array:
        """Read data from interface.

        :return: Data read from interface
        :raises NotImplementedError: If not implemented in derived class
        """
        raise NotImplementedError(f"{self.__class__}.read() must be implemented.")
