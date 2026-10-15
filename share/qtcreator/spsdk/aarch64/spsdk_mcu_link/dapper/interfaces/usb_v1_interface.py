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
import queue
import threading
from typing import Any, Optional

from ..core import Uint8Array
from . import Interface

logger = logging.getLogger(__name__)

try:
    import libusb_package
    import usb

    IMPORTS_AVAILABLE = True
except ImportError:
    logger.debug("usb or libusb_package not available. USB v1 interface will not be available.")
    usb = type(
        "usb", (), {"core": type("core", (), {"Device": None})}
    )  # bypass for Interface[T] type checker
    IMPORTS_AVAILABLE = False


class UsbV1Interface(Interface[usb.core.Device]):
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
        return 5

    def __init__(self, device: usb.core.Device) -> None:
        """Initialize USB interface.

        :param device: USB device instance
        """
        super().__init__(device)
        self._type = "usb_v1"
        self._endpoint_in: Optional[usb.core.Endpoint] = None
        self._endpoint_out: Optional[usb.core.Endpoint] = None

        self.serial_no = device.serial_number
        self.vendor = device.manufacturer
        self.product = device.product
        self.vid = device.idVendor
        self.pid = device.idProduct
        self.packet_size = 64

        self.thread: Optional[threading.Thread] = None
        self.interface_number = 0
        self.worker_stop_flag = threading.Event()
        self.data_fifo_rx: queue.SimpleQueue[bytes] = queue.SimpleQueue()
        self.read_mutex = threading.Semaphore(0)

    @staticmethod
    def list_probes() -> list[Interface]:
        """List available USB probes.

        :return: List of available USB interfaces
        """
        probes: list[Interface] = []
        # todo(mkelnar) supported_vendor_ids will be changed
        for vid in list([0x1FC9, 0x0D28]):
            usb_devices = libusb_package.find(find_all=True, idVendor=vid)
            for usb_device in usb_devices:
                if usb_device.bDeviceClass in {0x00, 0xEF}:  # not HID
                    try:
                        config = usb_device.get_active_configuration()
                        ifaces = usb.util.find_descriptor(config, find_all=True)
                        hid_ifaces: list[Any] = []
                        for iface in ifaces:
                            if iface.bInterfaceClass == 0x03 and iface.bInterfaceSubClass == 0x00:
                                hid_ifaces.append(iface)

                        if len(hid_ifaces) > 0:
                            probes.append(UsbV1Interface(usb_device))
                    except usb.core.USBError:
                        pass
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
        interface = None
        descriptors = usb.util.find_descriptor(cfg, find_all=True, bInterfaceClass=0x03)
        for iface in descriptors:
            i_name = usb.util.get_string(self._device, iface.iInterface)
            if any(item in i_name for item in ("CMSIS-DAP V2", "CMSIS-DAP")):
                interface = iface
                break

        if interface is None:
            raise RuntimeError(
                f"Unable to find USB device endpoints for interface {self.serial_no}"
            )

        for endpoint in interface:
            if endpoint.bEndpointAddress & usb.util.ENDPOINT_IN:
                self._endpoint_in = endpoint
            else:
                self._endpoint_out = endpoint

        if self._endpoint_in is None or self._endpoint_out is None:
            raise RuntimeError("Unable to find USB device endpoints.")

        self.interface_number = interface.bInterfaceNumber
        self._claim_interface()

        self._start_worker()

    def _claim_interface(self) -> None:
        try:
            if self._device.is_kernel_driver_active(self.interface_number):
                self._device.detach_kernel_driver(self.interface_number)
        except usb.core.USBError as e:
            logger.warning(f"kernel driver detach failed: {e}")
        except RuntimeError:
            pass

        try:
            usb.util.claim_interface(self._device, self.interface_number)
        except usb.core.USBError as e:
            raise RuntimeError("Unable to claim interface.") from e

    def close(self) -> None:
        """Close the USB interface connection."""
        self.worker_stop_flag.set()
        self.read_mutex.release()
        if isinstance(self.thread, threading.Thread):
            self.thread.join()
        self.worker_stop_flag.clear()
        usb.util.release_interface(self._device, self.interface_number)
        usb.util.dispose_resources(self._device)
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

        self.read_mutex.release()
        self._endpoint_out.write(data.buffer, 10000)

    def read(self) -> Uint8Array:
        """Read data from USB interface.

        :return: Data read from interface
        :raises RuntimeError: If device endpoint is not opened
        """
        if self._endpoint_in is None:
            raise RuntimeError("Device endpoint needs to be opened first.")

        try:
            data = self.data_fifo_rx.get(True, 10000)
        except queue.Empty as e:
            raise RuntimeError("No data available.") from e

        return Uint8Array((ctypes.c_uint8 * len(data))(*data))

    def _start_worker(self) -> None:
        """Worker thread initiator."""
        try:
            while True:
                if self._endpoint_in is not None:
                    self._endpoint_in.read(self._endpoint_in.wMaxPacketSize, 1)
        except usb.core.USBError:
            pass

        self.thread = threading.Thread(
            target=self._worker_rx, name=f"{self._type}_worker_rx ({self.serial_no})"
        )
        self.thread.daemon = True
        self.thread.start()

    def _worker_rx(self) -> None:
        """Receiver thread worker."""
        logger.debug("receiver worker starting")
        try:
            while not self.worker_stop_flag.is_set():
                self.read_mutex.acquire()
                if not self.worker_stop_flag.is_set():
                    if self._endpoint_in is not None:
                        read_data = self._endpoint_in.read(
                            self._endpoint_in.wMaxPacketSize, 10000
                        ).tobytes()
                        self.data_fifo_rx.put(read_data)
        except Exception as e:
            logger.debug(f"receiver worker failed: {e}")

        logger.debug("receiver worker ended")
