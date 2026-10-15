#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2024-2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

"""Main module for MCU-Link."""

import logging
from typing import Optional

from spsdk.debuggers.debug_probe import (
    DebugProbeCoreSightOnly,
    DebugProbes,
    ProbeDescription,
    SPSDKDebugProbeError,
    SPSDKDebugProbeNotOpenError,
    SPSDKDebugProbeTransferError,
)

from .dapper import DapperFactory, Interface, WebixDapper

TRACE_ENABLED = True
logger = logging.getLogger(__name__)


class DebugProbeMCULink(DebugProbeCoreSightOnly):
    """SPSDK Debug Probe NXP MCU-Link probe class."""

    NAME = "mcu-link"

    def __init__(self, hardware_id: str, options: Optional[dict] = None) -> None:
        """The Debug Probe class initialization."""
        super().__init__(hardware_id, options)
        self.probe: Optional[WebixDapper] = None
        self.configure_logger()
        logger.info("The SPSDK MCU-Link Interface has been initialized")

    def configure_logger(self, logging_level: int = logging.ERROR) -> None:
        """Configure MCU-Link logger."""
        logging.getLogger("dapper").setLevel(logging_level)
        trace_loggers = {
            name: logger
            for name, logger in logging.root.manager.loggerDict.items()  # pylint: disable=E1101
            if name.startswith("dapper") and name.endswith(".trace")
        }
        disable_trace = False
        if logging_level > logging.DEBUG:
            disable_trace = True
        # Update all dapper trace loggers
        for trace_logger in trace_loggers.values():
            if isinstance(trace_logger, logging.Logger):
                trace_logger.disabled = disable_trace

    @classmethod
    def get_connected_probes(
        cls,
        hardware_id: Optional[str] = None,
        options: Optional[dict] = None,  # pylint: disable=unused-argument
    ) -> DebugProbes:
        """Functions returns the list of all connected probes in system.

        There is option to look for just for one debug probe defined by its hardware ID.

        :param hardware_id: None to list all probes, otherwise the the only probe with
            matching hardware id is listed.
        :param options: The options dictionary
        :return: List of ProbeDescription
        """
        probes = DebugProbes()

        try:
            connected_probes: list[Interface] = DapperFactory.list_probes()
        except Exception as e:  # pylint: disable=broad-except
            logger.debug(f"Probing connected probes over MCU-Link failed: {str(e)}")
            connected_probes = []

        for probe in connected_probes:
            if not hardware_id or hardware_id == probe.serial_no:
                probes.append(
                    ProbeDescription(
                        interface="mcu-link",
                        hardware_id=probe.serial_no,
                        description=probe.description,
                        probe=DebugProbeMCULink,
                    )
                )

        return probes

    @classmethod
    def get_options_help(cls) -> dict[str, str]:
        """Get full list of options of debug probe.

        :return: Dictionary with individual options. Key is parameter name and value the help text.
        """
        return super().get_options_help()

    def open(self) -> None:
        """Debug probe open.

        General opening function for SPSDK library to support various DEBUG PROBES.
        The function is used to initialize the connection to target and enable using debug probe
        for DAT purposes.
        """
        try:
            self.probe = DapperFactory.create_probe(self.hardware_id)
            self.probe.stdout_handler = lambda _: None
            self.probe.stderr_handler = logger.error
        except Exception as e:
            raise SPSDKDebugProbeError(f"Failed to initialize MCU-Link probe: {str(e)}") from e

    def connect(self) -> None:
        """Connect to target.

        The MCU-Link connecting function for SPSDK library to support various DEBUG PROBES.
        The function is used to initialize the connection to target

        :raises SPSDKDebugProbeError: The MCU-Link cannot establish communication with target
        """
        if self.probe is None:
            raise SPSDKDebugProbeError("Debug probe needs to be opened first")
        try:
            self.probe.connect()
        except Exception as e:
            raise SPSDKDebugProbeError(
                f"The MCU-Link cannot establish communication with target: {str(e)}"
            ) from e

    def close(self) -> None:
        """Close MCU-Link probe.

        The MCU-Link closing function for SPSDK library to support various DEBUG PROBES.
        """
        if self.probe:
            self.probe.close()

    def coresight_reg_read(self, access_port: bool = True, addr: int = 0) -> int:
        """Read coresight register over MCU-Link probe.

        It reads coresight register function for SPSDK library to support various DEBUG PROBES.

        :param access_port: if True, the Access Port (AP) register will be read(default), otherwise the Debug Port
        :param addr: the register address
        :return: The read value of addressed register (4 bytes)
        """
        if self.probe is None:
            raise SPSDKDebugProbeNotOpenError("The MCU-Link debug probe is not opened yet")

        try:
            ret = self.probe.core_sight_read(access_port, addr)
            if TRACE_ENABLED:
                logger.debug(
                    f"Coresight read {'AP' if access_port else 'DP'}, address: {addr:08X}, data: {ret:08X}"
                )
            return ret
        except Exception as e:
            self.probe.reinit_target()
            raise SPSDKDebugProbeTransferError(
                f"The coresight read operation failed({str(e)})"
            ) from e

    def coresight_reg_write(self, access_port: bool = True, addr: int = 0, data: int = 0) -> None:
        """Write coresight register over MCU-Link probe.

        It writes coresight register function for SPSDK library to support various DEBUG PROBES.

        :param access_port: if True, the Access Port (AP) register will be written(default), otherwise the Debug Port
        :param addr: the register address
        :param data: the data to be written into register
        """
        if self.probe is None:
            raise SPSDKDebugProbeNotOpenError("The MCU-Link debug probe is not opened yet")

        try:
            self.probe.core_sight_write(access_port, addr, data)
            if TRACE_ENABLED:
                logger.debug(
                    f"Coresight write {'AP' if access_port else 'DP'}, address: {addr:08X}, data: {data:08X}"
                )
        except Exception as e:
            self.probe.reinit_target()
            raise SPSDKDebugProbeTransferError(
                f"The Coresight write operation failed({str(e)})"
            ) from e

    def assert_reset_line(self, assert_reset: bool = False) -> None:
        """Nothing to do, controlled directly by reset override."""

    def reset(self) -> None:
        """Control target reset."""
        if self.probe is None:
            raise SPSDKDebugProbeNotOpenError("The MCU-Link debug probe is not opened yet")

        try:
            self.probe.reset()
        except Exception as e:
            raise SPSDKDebugProbeError(f"The MCU-Link reset operation failed: {str(e)}") from e

    def __str__(self) -> str:
        """Return information about the device."""
        if self.probe is None:
            return "MCU-Link debug probe"
        return f"MCU-Link debug probe at {str(self.probe.get_probe_id())}"
