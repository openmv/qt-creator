#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2024-2025 NXP
# Copyright 2025 Oidis
#
# SPDX-License-Identifier: BSD-3-Clause
"""HID interface implementation for communicating with USB HID devices."""

import ctypes
import logging
import platform
from typing import Any

from ..core import Uint8Array
from . import Interface

logger = logging.getLogger(__name__)

try:
    import hid

    IMPORTS_AVAILABLE = True
except ImportError:
    logger.debug("HID library not available. HID interface will not be available.")
    hid = type("hid", (), {"device": None})  # bypass for Interface[T] type checker
    IMPORTS_AVAILABLE = False

deep_trace: bool = False


class HidInterface(Interface[hid.device]):
    """HID interface implementation for communicating with USB HID devices.

    This class provides functionality for interfacing with USB HID devices,
    specifically NXP debug probes like LPC-Link2, MCU-Link and DAP-Link.
    It handles device enumeration, connection management and data transfer.
    """

    @classmethod
    def is_available(cls) -> bool:
        """Returns true if interface is available."""
        return IMPORTS_AVAILABLE

    @classmethod
    def priority(cls) -> int:
        """Returns priority. The lower the number, the higher the priority."""
        return 3 if platform.system() == "Darwin" or platform.system() == "Windows" else 10

    def __init__(
        self, device: hid.device, info: dict  # pylint: disable=c-extension-no-member
    ) -> None:
        """Initialize HID interface.

        :param device: HID device instance
        :param info: Dictionary containing device information
        """
        super().__init__(device)
        self._type = "hid"
        self.serial_no = info["serial_number"]
        self.vid = info["vendor_id"]
        self.pid = info["product_id"]
        self.vendor = info["manufacturer_string"]
        self.product = info["product_string"]
        self.device_info = info

        self.packet_size = 64

    @staticmethod
    def list_probes() -> list[Interface]:
        """List available HID probes.

        :return: List of available HID interfaces
        """
        probes: list[Interface] = []

        devices = hid.enumerate()  # pylint: disable=c-extension-no-member
        for device_info in devices:
            if device_info["vendor_id"] in {0x1FC9, 0xD28}:  # nxp vid, dap link vid
                if device_info["product_id"] in {
                    0x0090,
                    0x0143,
                    0x204,
                }:  # lpc-link, mcu-link, dap-link
                    if device_info["usage_page"] == 0xFF00:  # vendor defined usage page
                        device = hid.device(  # pylint: disable=c-extension-no-member, not-callable
                            vendor_id=device_info["vendor_id"],
                            product_id=device_info["product_id"],
                            path=device_info["path"],
                        )
                        probes.append(HidInterface(device, device_info))

        return probes

    def open(self) -> None:
        """Open the HID interface connection.

        :raises RuntimeError: If device is not initialized
        """
        super().open()

        self._device.open_path(self.device_info["path"])

    def close(self) -> None:
        """Close the HID interface connection."""
        if self._device is not None:
            self._device.close()

    def write(self, data: Uint8Array) -> None:
        """Write data to HID interface.

        :param data: Data to write
        """
        write_data = ([0] + list(data.buffer))[0:64]
        self._device.write(write_data)
        if deep_trace:
            print(
                "write: ("
                + str(len(data.buffer))
                + ") ["
                + ",".join(f"{x:02X}" for x in write_data[0:64])
                + "]"
            )

    def read(self) -> Uint8Array:
        """Read data from HID interface.

        :return: Data read from interface
        :raises RuntimeError: If device endpoint is not opened
        """
        if self._device is None:
            raise RuntimeError("Device endpoint needs to be opened first.")
        data = self._device.read(self.packet_size)
        if deep_trace:
            print(
                "read:  (" + str(len(data)) + ") [" + ",".join(f"{x:02X}" for x in data[0:64]) + "]"
            )

        return Uint8Array((ctypes.c_uint8 * len(data))(*data))

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        """Exit the context manager.

        :param exc_type: Exception type
        :param exc_val: Exception value
        :param exc_tb: Exception traceback
        """
        if self._device is not None:
            self._device.close()
