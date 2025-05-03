#!/usr/bin/env python3
#
# Copyright 2021-2024 NXP
# SPDX-License-Identifier: BSD-3-Clause
#
# NXP USBSIO Library to control SPI, I2C and GPIO bus over USB
#
__version__ = '2.1.13'
__title__ = 'usblibsio'
__author__ = 'NXP Semiconductors'
__copyright__ = 'Copyright NXP 2021-2024'
__license__ = 'BSD-3-Clause'
__url__ = 'https://www.nxp.com/design/software/development-software/library-for-windows-macos-and-ubuntu-linux:LIBUSBSIO'
__description__ = 'Python interface for the NXP USBSIO Library version %s' % __version__
__long_description__ = '''LIBUSBSIO: The NXP USBSIO Library version %s 
This module implements a Python wrapper around the NXP USBSIO Library enabling to access SPI or
I2C bus and GPIO ports over a USB interface of the NXP LPC-link2 and MCU-Link devices.
''' % __version__

from .libusbsio import LIBUSBSIO, LIBUSBSIO_Exception, usbsio
