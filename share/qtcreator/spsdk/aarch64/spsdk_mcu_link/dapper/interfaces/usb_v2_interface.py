#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2024-2025 NXP
# Copyright 2025 Oidis
#
# SPDX-License-Identifier: BSD-3-Clause
"""USB Interface implementation for communicating with USB devices."""

import ctypes
import logging
from typing import Optional

from ..core import Uint8Array
from . import Interface

logger = logging.getLogger(__name__)

try:
    import libusb_package
    import usb

    IMPORTS_AVAILABLE = True
except ImportError:
    logger.debug("usb or libusb_package not available. USB v2 interface will not be available.")
    usb = type(
        "usb", (), {"core": type("core", (), {"Device": None})}
    )  # bypass for Interface[T] type checker
    IMPORTS_AVAILABLE = False


class UsbV2Interface(Interface[usb.core.Device]):
    """USB Interface implementation for communicating with USB devices.

    This class provides functionality for USB communication including device enumeration,
    reading/writing data, and managing USB endpoints. It supports NXP specific interfaces
    and handles USB device configuration.
    """

    @classmethod
    def is_available(cls) -> bool:
        """Returns true if interface is available."""
        return IMPORTS_AVAILABLE

    @classmethod
    def priority(cls) -> int:
        """Returns priority. The lower the number, the higher the priority."""
        return 1

    def __init__(self, device: usb.core.Device) -> None:
        """Initialize USB interface.

        :param device: USB device instance
        """
        super().__init__(device)
        self._type = "usb_v2"
        self._endpoint_in: Optional[usb.core.Endpoint] = None
        self._endpoint_out: Optional[usb.core.Endpoint] = None

        self.serial_no = device.serial_number
        self.vendor = device.manufacturer
        self.product = device.product
        self.vid = device.idVendor
        self.pid = device.idProduct
        self.packet_size = 64

    @staticmethod
    def list_probes() -> list[Interface]:
        """List available USB probes.

        :return: List of available USB interfaces
        """
        probes: list[Interface] = []
        # todo(mkelnar) supported_vendor_ids will be changed
        for vid in list([0x1FC9]):
            usb_devices = libusb_package.find(find_all=True, idVendor=vid)
            for usb_device in usb_devices:
                if usb_device.bDeviceClass in {0x00, 0xEF}:  # not HID
                    try:
                        config = usb_device.get_active_configuration()
                        ifaces = usb.util.find_descriptor(config, find_all=True)
                        for iface in ifaces:
                            if iface.bInterfaceClass == 0xFF:  # nxp specific interface
                                probes.append(UsbV2Interface(usb_device))
                    finally:
                        usb_device._finalize_object()  # pylint: disable=protected-access
        return probes

    def open(self) -> None:
        """Open the USB interface connection.

        :raises RuntimeError: If unable to find USB device endpoints
        """
        super().open()

        logger.info(f"Device to open: {self._device.product} ({self._device.manufacturer})")

        cfg = self._device.get_active_configuration()
        interfaces = cfg[(0, 0)]

        self._endpoint_in = usb.util.find_descriptor(
            interfaces,
            custom_match=lambda e: (
                usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_IN
            ),
        )
        self._endpoint_out = usb.util.find_descriptor(
            interfaces,
            custom_match=lambda e: (
                usb.util.endpoint_direction(e.bEndpointAddress) == usb.util.ENDPOINT_OUT
            ),
        )

        if self._endpoint_in is None or self._endpoint_out is None:
            raise RuntimeError("Unable to fine USB device endpoints.")

    def close(self) -> None:
        """Close the USB interface connection."""
        self._endpoint_in = None
        self._endpoint_out = None
        self._device.finalize()

    def write(self, data: Uint8Array) -> None:
        """Write data to USB interface.

        :param data: Data to write
        :raises RuntimeError: If device endpoint is not opened or data is not Uint8Array
        """
        if self._endpoint_out is None:
            raise RuntimeError("Device endpoint needs to be opened first.")
        if not isinstance(data, Uint8Array):
            raise RuntimeError("Data must be an instance of Uint8Array.")
        self._endpoint_out.write(data.buffer, self.packet_size)

    def read(self) -> Uint8Array:
        """Read data from USB interface.

        :return: Data read from interface
        :raises RuntimeError: If device endpoint is not opened
        """
        if self._endpoint_in is None:
            raise RuntimeError("Device endpoint needs to be opened first.")
        data = self._endpoint_in.read(self.packet_size)
        return Uint8Array((ctypes.c_uint8 * len(data))(*data))
