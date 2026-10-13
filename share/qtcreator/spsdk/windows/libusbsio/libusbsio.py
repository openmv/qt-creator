#!/usr/bin/env python3
#
# Copyright 2021-2022 NXP
# SPDX-License-Identifier: BSD-3-Clause
#
# NXP USBSIO Library to control SPI, I2C and GPIO bus over USB
#

import functools
import logging
import os
import platform
import sys
from ctypes import (CDLL, POINTER, Structure, c_char_p, c_int32, c_uint8,
                    c_uint16, c_uint32, c_void_p, c_wchar_p)
from typing import Generator, List, Tuple


class LibUsbStructure(Structure):
    @property
    def __dict__(self):  # type: ignore            
        ret = {}
        for x in self._fields_:
            key = x[0]
            val = getattr(self, key)
            if issubclass(x[1], Structure):
                val = vars(val)
            ret[key] = val
        return ret
    
    def __getitem__(self, key):
        if key not in [x[0] for x in self._fields_]:
            raise AttributeError(f"Key {key} is not in {self.__class__.__name__} structure.")
        return getattr(self, key)
    

class LIBUSBSIO_Exception(Exception):
    def __init__(self, message):
        if not message:
            message = "Unknown Exception"
        message = "LIBUSBSIO: " + message
        super().__init__(message)

class LIBUSBSIO(object):
    """# Class encapsulating functionality of the NXP USBSIO binary library.
       The library enables to access SPI or I2C bus and GPIO ports over a USB interface
       of the NXP LPC-link2 and MCU-Link bridge devices.
    """

    # platforms
    (P_WIN32, P_WIN64, P_LINUX64, P_LINUX32, P_MACOS_X64, P_MACOS_ARM64) = (0,1,2,3,4,5)

    # Library error codes
    OK = 0
    ERR_HID_LIB = -1             # HID library error.
    ERR_BAD_HANDLE = -2          # Handle passed to the function is invalid.
    ERR_SYNCHRONIZATION = -3     # Thread Synchronization error.
    ERR_MEM_ALLOC = -4           # Memory allocation error.
    ERR_MUTEX_CREATE = -5        # Mutex creation error.

    # I2C hardware interface errors
    ERR_FATAL = -0x11            # Fatal error occurred
    ERR_I2C_NAK = -0x12          # Transfer aborted due to NACK
    ERR_I2C_BUS = -0x13          # Transfer aborted due to bus error
    ERR_I2C_SLAVE_NAK = -0x14    # NAK received after SLA+W or SLA+R
    ERR_I2C_ARBLOST = -0x15      # I2C bus arbitration lost to other master

    # Errors from firmware's HID-SIO bridge module
    ERR_TIMEOUT = -0x20          # Transaction timed out
    ERR_INVALID_CMD = -0x21      # Invalid HID_SIO Request or Request not supported in this version.
    ERR_INVALID_PARAM = -0x22    # Invalid parameters are provided for the given Request.
    ERR_PARTIAL_DATA = -0x23     # Partial transfer completed.

    # I2C clock rates in kbps
    I2C_CLOCK_STANDARD_MODE  =  100000
    I2C_CLOCK_FAST_MODE      =  400000
    I2C_CLOCK_FAST_MODE_PLUS = 1000000

    # I2C Port normal transfer option flags
    I2C_TRANSFER_OPTIONS_START_BIT      = 0x01  # Generate start condition before transmitting
    I2C_TRANSFER_OPTIONS_STOP_BIT       = 0x02  # Generate stop condition at the end of transfer
    I2C_TRANSFER_OPTIONS_BREAK_ON_NACK  = 0x04  # Stop transmitting the data after NAK
    I2C_TRANSFER_OPTIONS_NACK_LAST_BYTE = 0x08  # NACK the last Byte received.
    I2C_TRANSFER_OPTIONS_NO_ADDRESS     = 0x40  # Skip address transmission

    # I2C Port fast transfer option flags
    I2C_FAST_XFER_OPTION_IGNORE_NACK    = 0x01  # Ignore NACK during data transfer. By default transfer is aborted.
    I2C_FAST_XFER_OPTION_LAST_RX_ACK    = 0x02  # ACK last Byte received. By default we NACK last Byte we receive per I2C specification.

    # SPI Port option flags
    SPI_CONFIG_OPTION_DATA_SIZE_8   = 0x07     # SPI Data Size is 8 Bits
    SPI_CONFIG_OPTION_DATA_SIZE_16  = 0x0F     # SPI Data Size is 16 Bits
    SPI_CONFIG_OPTION_POL_0         = 0 << 6   # SPI Clock Default Polarity is Low
    SPI_CONFIG_OPTION_POL_1         = 1 << 6   # SPI Clock Default Polarity is High
    SPI_CONFIG_OPTION_PHA_0         = 0 << 7   # SPI Data is captured on the first clock transition of the frame
    SPI_CONFIG_OPTION_PHA_1         = 1 << 7   # SPI Data is captured on the second clock transition of the frame
    def SPI_CONFIG_OPTION_PRE_DELAY(x) -> int: # SPI Pre Delay in micro seconds max of 255
        return (x & 0xff) << 8
    def SPI_CONFIG_OPTION_POST_DELAY(x) -> int: # SPI Post Delay in micro seconds max of 255
        return (x & 0xff) << 16

    def buffprint(buff:bytes) -> str:
        '''Internal buffer dump for logging purposes'''
        if not buff:
            return "None"
        elif(buff == buff[:1].hex()*len(buff)):
            return "0x%s * %d" % (buff[:1], len(buff))
        else:
            return "0x"+buff.hex()


    # I2C Port configuration information.
    class I2C_PORTCONFIG_T(LibUsbStructure):
        _fields_ = [
            ("ClockRate", c_uint32),  # I2C Clock speed
            ("Options", c_uint32)     # Configuration options
        ]

    # I2C Fast transfer parameter structure.
    class I2C_FAST_XFER_T(LibUsbStructure):
        _fields_ = [
            ("txSz", c_uint16),          # Number of bytes in transmit array, 0=>only receive
            ("rxSz", c_uint16),       # Number of bytes to received, 0=>only transmit
            ("options", c_uint16),    # Fast transfer options
            ("slaveAddr", c_uint16),  #    7-bit I2C Slave address
            ("txBuff", POINTER(c_uint8)),    # Pointer to array of bytes to be transmitted
            ("rxBuff", POINTER(c_uint8)),    # Pointer to array of bytes to be transmitted
        ]

    # SPI transfer parameter structure
    class SPI_XFER_T(LibUsbStructure):
        _fields_ = [
            ("length", c_uint16),      # Number of bytes in transmit and receive
            ("options", c_uint8),     # Transfer options
            ("device", c_uint8),      # SPI slave device, use @ref LPCUSBSIO_GEN_SPI_DEVICE_NUM macro to derive device number from a GPIO port and pin number */
            ("txBuff", POINTER(c_uint8)),    # Pointer to array of bytes to be transmitted
            ("rxBuff", POINTER(c_uint8)),    # Pointer to array of bytes to be transmitted
        ]

    # SPI Port configuration information
    class SPI_PORTCONFIG_T(LibUsbStructure):
        _fields_ = [
            ("busSpeed", c_uint32),   # SPI bus speed
            ("Options", c_uint32)     # Configuration options
        ]

    # HIDAPI Enumeration Info struct
    class HIDAPI_DEVICE_INFO_T(LibUsbStructure):
        class EX_T(LibUsbStructure):
            _fields_ = [
                ("is_valid", c_uint16),
                ("output_report_length", c_uint16),
                ("input_report_length", c_uint16),
                ("usage_page", c_uint16),
                ("usage", c_uint16)
            ]       

        _fields_ = [
            ("path", c_char_p),
            ("serial_number", c_wchar_p),
            ("manufacturer_string", c_wchar_p),
            ("product_string", c_wchar_p),
            ("interface_number", c_int32),
            ("vendor_id", c_uint16),
            ("product_id", c_uint16),
            ("release_number", c_uint16),
            ("ex", EX_T)
        ]


    # search for the DLL
    def _lookup_dll_path(subdir: str, dllname: str):
        ''' search for the DLL specified by a sub-directoruy and DLL name'''
        def probe(*parts):
            p = os.path.join(*parts)
            return p if os.path.isfile(p) else None

        here = os.path.dirname(__file__)
        # 1. bin subdirectory of this Python module
        # 2. bin subdirectory in parent's libusbsio repo
        # 3. DLL file local to this Python module
        # 4. DLL file local to current working dir
        p = probe(here, "bin", subdir, dllname) or \
            probe(here, "..", "..", "bin", subdir, dllname) or \
            probe(here, dllname) or \
            os.path.join(".", dllname)
        return p

    def __init__(self, dllpath:str=None, loglevel:int=logging.NOTSET, autoload=True):
        '''# LIBUSBSIO class constructor

        ## Args
        - `dllpath`  Path to LIBUSBSIO dynamic library. Use None to autodetect based on operating system.
        - `loglevel` Internal logging level, defaults to logging.NOTSET.
        - `autoload` Set to load the DLL library automatically.
        '''
        self.logger:logging.Logger = logging.getLogger('libusbsio')
        self.logger.setLevel(loglevel)

        self._dll = None
        self._dllpath:str = None
        self._devIx:int = None
        self._ports_open:List[int] = []
        self._h = None
        self._vidpid = (0,0)
        self._platf:int = None

        if autoload:
            self.LoadDLL(dllpath)

    def __del__(self):
        if self._h:
            self.Close()
        if self._dll:
            del self._dll
            self._dll = None
            self._dllpath = None
            self.logger.info("SIO library unloaded")

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.Close()

    def LoadDLL(self, dllpath=None, raiseOnError=True):
        '''# Load USBSIO library
        Load the USBSIO library from given path (if directory path is given) or from
        the given library file (if file path is given). In case a directory path
        is give, the library name is determined automatically for current OS platform.

        ## Args
        - `dllpath` Path to DLL to be loaded or None to load from default path

        ## Returns
        - None if success
        - Exception object in case of error when `raiseOnError` is False

        ## Raises
        Raises LIBUSBSIO_Exception when DLL cannot be loaded and when `raiseOnError` is True
        '''
        # default library name and other platform-dependent stuff
        if platform.system().lower().startswith('win'):
            dllname = "libusbsio.dll"
            if platform.architecture()[0].startswith('64'):
                platf = LIBUSBSIO.P_WIN64
                dfltdir = "x64"
                packing = 8
            else:
                platf = LIBUSBSIO.P_WIN32
                dfltdir = "Win32"
                packing = 4
        elif platform.system().lower().startswith('lin'):
            dllname = "libusbsio.so"
            is_64bits = sys.maxsize > 2**32
            dfltdir = "linux_" + platform.machine()
            if is_64bits:
                platf = LIBUSBSIO.P_LINUX64
                packing = 8
            else:
                platf = LIBUSBSIO.P_LINUX32
                packing = 4
        elif platform.system().lower().startswith('dar'):
            dfltdir = "osx_" + platform.machine()
            if platform.machine() == "arm64":
                platf = LIBUSBSIO.P_MACOS_ARM64
            else:
                platf = LIBUSBSIO.P_MACOS_X64
            dllname = "libusbsio.dylib"
            packing = 8
        else:
            ex = LIBUSBSIO_Exception("Unknown platform to load proper library.")
            if raiseOnError:
                raise ex
            return ex

        # remember platform
        self._platf = platf

        # dll path
        if dllpath:
            if os.path.isdir(dllpath):
                dllpath = os.path.join(dllpath, dllname)
        else:
            dllpath = LIBUSBSIO._lookup_dll_path(dfltdir, dllname)

        if not os.path.isfile(dllpath):
            ex = LIBUSBSIO_Exception("DLL path invalid '%s'." % dllpath)
            if raiseOnError:
                raise ex
            return ex

        # structure packing
        LIBUSBSIO.I2C_PORTCONFIG_T._pack_ = packing
        LIBUSBSIO.I2C_FAST_XFER_T._pack_ = packing
        LIBUSBSIO.SPI_XFER_T._pack_ = packing
        LIBUSBSIO.SPI_PORTCONFIG_T._pack_ = packing
        LIBUSBSIO.HIDAPI_DEVICE_INFO_T._pack_ = packing

        self.logger.info("Loading SIO library: %s" % dllpath)
        self._dllpath = dllpath
        self._dll = CDLL(dllpath)
        self._devIx = None
        self._h = None

        if not self._dll:
            ex = LIBUSBSIO_Exception("DLL could not be loaded '%s'." % dllpath)
            if raiseOnError:
                raise ex
            return ex

        self._GetNumPorts = self._dll.LPCUSBSIO_GetNumPorts
        self._GetNumPorts.argtypes = [c_uint32, c_uint32]
        self._GetNumPorts.restype = c_uint32

        self._Open = self._dll.LPCUSBSIO_Open
        self._Open.argtypes = [c_uint32]
        self._Open.restype = c_void_p

        self._Close = self._dll.LPCUSBSIO_Close
        self._Close.argtypes = [c_void_p]
        self._Close.restype = c_int32

        self._GetVersion = self._dll.LPCUSBSIO_GetVersion
        self._GetVersion.argtypes = [c_void_p]
        self._GetVersion.restype = c_char_p

        self._Error = self._dll.LPCUSBSIO_Error
        self._Error.argtypes = [c_void_p]
        self._Error.restype = c_wchar_p

        self._GetNumI2CPorts = self._dll.LPCUSBSIO_GetNumI2CPorts
        self._GetNumI2CPorts.argtypes = [c_void_p]
        self._GetNumI2CPorts.restype = c_uint32

        self._GetNumSPIPorts = self._dll.LPCUSBSIO_GetNumSPIPorts
        self._GetNumSPIPorts.argtypes = [c_void_p]
        self._GetNumSPIPorts.restype = c_uint32

        self._GetNumGPIOPorts = self._dll.LPCUSBSIO_GetNumGPIOPorts
        self._GetNumGPIOPorts.argtypes = [c_void_p]
        self._GetNumGPIOPorts.restype = c_uint32

        self._GetMaxDataSize = self._dll.LPCUSBSIO_GetMaxDataSize
        self._GetMaxDataSize.argtypes = [c_void_p]
        self._GetMaxDataSize.restype = c_uint32

        self._GetLastError = self._dll.LPCUSBSIO_GetLastError
        self._GetLastError.argtypes = [c_void_p]
        self._GetLastError.restype = c_uint32

        self._I2C_Open = self._dll.I2C_Open
        self._I2C_Open.argtypes = [c_void_p, POINTER(LIBUSBSIO.I2C_PORTCONFIG_T), c_uint8]
        self._I2C_Open.restype = c_void_p

        self._I2C_Close = self._dll.I2C_Close
        self._I2C_Close.argtypes = [c_void_p]
        self._I2C_Close.restype = c_int32

        self._I2C_Reset = self._dll.I2C_Reset
        self._I2C_Reset.argtypes = [c_void_p]
        self._I2C_Reset.restype = c_int32

        self._I2C_Close = self._dll.I2C_Close
        self._I2C_Close.argtypes = [c_void_p]
        self._I2C_Close.restype = c_int32

        self._I2C_DeviceRead = self._dll.I2C_DeviceRead
        self._I2C_DeviceRead.argtypes = [c_void_p, c_uint8, POINTER(c_uint8), c_uint16, c_uint8]
        self._I2C_DeviceRead.restype = c_int32

        self._I2C_DeviceWrite = self._dll.I2C_DeviceWrite
        self._I2C_DeviceWrite.argtypes = [c_void_p, c_uint8, POINTER(c_uint8), c_uint16, c_uint8]
        self._I2C_DeviceWrite.restype = c_int32

        self._I2C_FastXfer = self._dll.I2C_FastXfer
        self._I2C_FastXfer.argtypes = [c_void_p, POINTER(LIBUSBSIO.I2C_FAST_XFER_T)]
        self._I2C_FastXfer.restype = c_int32

        self._SPI_Open = self._dll.SPI_Open
        self._SPI_Open.argtypes = [c_void_p, POINTER(LIBUSBSIO.SPI_PORTCONFIG_T), c_uint8]
        self._SPI_Open.restype = c_void_p

        self._SPI_Close = self._dll.SPI_Close
        self._SPI_Close.argtypes = [c_void_p]
        self._SPI_Close.restype = c_int32

        self._SPI_Transfer = self._dll.SPI_Transfer
        self._SPI_Transfer.argtypes = [c_void_p, POINTER(LIBUSBSIO.SPI_XFER_T)]
        self._SPI_Transfer.restype = c_int32

        self._SPI_Reset = self._dll.SPI_Reset
        self._SPI_Reset.argtypes = [c_void_p]
        self._SPI_Reset.restype = c_int32

        self._GPIO_ReadPort = self._dll.GPIO_ReadPort
        self._GPIO_ReadPort.argtypes = [c_void_p, c_uint8, POINTER(c_uint32)]
        self._GPIO_ReadPort.restype = c_int32

        self._GPIO_WritePort = self._dll.GPIO_WritePort
        self._GPIO_WritePort.argtypes = [c_void_p, c_uint8, POINTER(c_uint32)]
        self._GPIO_WritePort.restype = c_int32

        self._GPIO_SetPort = self._dll.GPIO_SetPort
        self._GPIO_SetPort.argtypes = [c_void_p, c_uint8, c_uint32]
        self._GPIO_SetPort.restype = c_int32

        self._GPIO_ClearPort = self._dll.GPIO_ClearPort
        self._GPIO_ClearPort.argtypes = [c_void_p, c_uint8, c_uint32]
        self._GPIO_ClearPort.restype = c_int32

        self._GPIO_GetPortDir = self._dll.GPIO_GetPortDir
        self._GPIO_GetPortDir.argtypes = [c_void_p, c_uint8, POINTER(c_uint32)]
        self._GPIO_GetPortDir.restype = c_int32

        self._GPIO_SetPortOutDir = self._dll.GPIO_SetPortOutDir
        self._GPIO_SetPortOutDir.argtypes = [c_void_p, c_uint8, c_uint32]
        self._GPIO_SetPortOutDir.restype = c_int32

        self._GPIO_SetPortInDir = self._dll.GPIO_SetPortInDir
        self._GPIO_SetPortInDir.argtypes = [c_void_p, c_uint8, c_uint32]
        self._GPIO_SetPortInDir.restype = c_int32

        self._GPIO_SetPin = self._dll.GPIO_SetPin
        self._GPIO_SetPin.argtypes = [c_void_p, c_uint8, c_uint8]
        self._GPIO_SetPin.restype = c_int32

        self._GPIO_ClearPin = self._dll.GPIO_ClearPin
        self._GPIO_ClearPin.argtypes = [c_void_p, c_uint8, c_uint8]
        self._GPIO_ClearPin.restype = c_int32

        self._GPIO_TogglePin = self._dll.GPIO_TogglePin
        self._GPIO_TogglePin.argtypes = [c_void_p, c_uint8, c_uint8]
        self._GPIO_TogglePin.restype = c_int32

        self._GPIO_GetPin = self._dll.GPIO_GetPin
        self._GPIO_GetPin.argtypes = [c_void_p, c_uint8, c_uint8]
        self._GPIO_GetPin.restype = c_int32

        self._GPIO_ConfigIOPin = self._dll.GPIO_ConfigIOPin
        self._GPIO_ConfigIOPin.argtypes = [c_void_p, c_uint8, c_uint8, c_uint32]
        self._GPIO_ConfigIOPin.restype = c_int32

        if 1:
            self._HIDAPI_Enumerate = self._dll.HIDAPI_Enumerate
            self._HIDAPI_Enumerate.argtypes = [c_uint32,c_uint32,c_int32]
            self._HIDAPI_Enumerate.restype = c_void_p

            self._HIDAPI_EnumerateNext = self._dll.HIDAPI_EnumerateNext
            self._HIDAPI_EnumerateNext.argtypes = [c_void_p, POINTER(LIBUSBSIO.HIDAPI_DEVICE_INFO_T)]
            self._HIDAPI_EnumerateNext.restype = c_int32

            self._HIDAPI_EnumerateRewind = self._dll.HIDAPI_EnumerateRewind
            self._HIDAPI_EnumerateRewind.argtypes = [c_void_p]
            self._HIDAPI_EnumerateRewind.restype = c_int32

            self._HIDAPI_EnumerateFree = self._dll.HIDAPI_EnumerateFree
            self._HIDAPI_EnumerateFree.argtypes = [c_void_p]
            self._HIDAPI_EnumerateFree.restype = c_int32

            self._HIDAPI_DeviceOpen = self._dll.HIDAPI_DeviceOpen
            self._HIDAPI_DeviceOpen.argtypes = [c_char_p]
            self._HIDAPI_DeviceOpen.restype = c_void_p

            self._HIDAPI_DeviceClose = self._dll.HIDAPI_DeviceClose
            self._HIDAPI_DeviceClose.argtypes = [c_void_p]
            self._HIDAPI_DeviceClose.restype = c_int32

            self._HIDAPI_DeviceWrite = self._dll.HIDAPI_DeviceWrite
            self._HIDAPI_DeviceWrite.argtypes = [c_void_p, c_void_p, c_int32, c_int32]
            self._HIDAPI_DeviceWrite.restype = c_int32

            self._HIDAPI_DeviceRead = self._dll.HIDAPI_DeviceRead
            self._HIDAPI_DeviceRead.argtypes = [c_void_p, c_void_p, c_int32, c_int32]
            self._HIDAPI_DeviceRead.restype = c_int32

        # optional, not implemented for all platforms/versions
        try:
            self._GetDeviceInfo = self._dll.LPCUSBSIO_GetDeviceInfo
            self._GetDeviceInfo.argtypes = [c_int32, POINTER(LIBUSBSIO.HIDAPI_DEVICE_INFO_T)]
            self._GetDeviceInfo.restype = c_int32
        except:
            pass

        # no exception means success
        return None

    def IsDllLoaded(self) -> bool:
        return self._dll

    def _check_dll_loaded(self) -> bool:
        '''Internal check routine used in the decorator'''
        if not self._dll:
            raise LIBUSBSIO_Exception("DLL not loaded.")
        return True

    def _check_dll_open(self) -> bool:
        '''Internal check routine used in the decorator'''
        if not self._dll:
            raise LIBUSBSIO_Exception("DLL not loaded.")
        if not self._h:
            raise LIBUSBSIO_Exception("DLL is not open.")
        return True

    def need_dll_loaded(func):
        '''# Need DLL decorator
        Decorator assuring the USBSIO library is loaded.

        ## Args
        - `func` function being decorated

        ## Returns
        The wrapper function.
        '''
        @functools.wraps(func)
        def wrapper(self, *args, **kwargs):
            self._check_dll_loaded()
            return func(self, *args, **kwargs)
        return wrapper

    def need_dll_open(func):
        '''# Need USBSIO open decorator
        Decorator assuring the USBSIO library is loaded and connection is open.

        ## Args
        `func` function being decorated

        ## Returns
        The wrapper function.
        '''
        @functools.wraps(func)
        def wrapper(self, *args, **kwargs):
            self._check_dll_open()
            return func(self, *args, **kwargs)
        return wrapper

    # Known USBSIO device types
    VIDPID_REDLINK  = (0x21BD, 0x0006)
    VIDPID_LPCLINK2 = (0x1FC9, 0x0090)
    VIDPID_MCULINK  = (0x1FC9, 0x0143)
    VIDPID_ALL_NXP  = (0x1FC9, 0)

    # all NXP USB devices
    VIDPID_NXP = (0x1FC9, 0)

    @need_dll_loaded
    def GetNumPorts(self, vidpids:'list[tuple[int,int]]' = None) -> int:
        '''# Get number of USBSIO ports
        Gets the number of USBSIO ports that are available on the controller. Multiple PID+VID tuples
        may be specified to search multiple types of devices. The first device in the list which is detected
        is the one which will be used and which returns the number of its communication ports.

        ## Args
        - `vidpids` - List of (VID,PID) tuples identifying the device types to probe. Use None to probe default devices (VIDPID_LPCLINK2 and VIDPID_MCULINK)

        ## Returns
        The number of ports available on the USBSIO controller.
        '''
        if not vidpids:
            vidpids = [ LIBUSBSIO.VIDPID_ALL_NXP ]

        ret:int = 0
        for vp in vidpids:
            ret = self._GetNumPorts(vp[0], vp[1])
            self.logger.debug("Probing VID=0x%04x PID=0x%04x returned %d" % (vp[0], vp[1], ret))
            if(ret):
                self._vidpid = vp
                break

        return ret

    @need_dll_loaded
    def GetDeviceInfo(self, dev:int) -> 'LIBUSBSIO.HIDAPI_DEVICE_INFO_T|None':
        '''# Get HID_API information about enumerated device
        Call this function after GetNumPorts before Open to retrieve low-level HID_API information about the enumerated device.

        ## Args
        - `dev` - Index of the device port to be open.

        ## Returns
        Device information or None in case of error.
        '''
        info = LIBUSBSIO.HIDAPI_DEVICE_INFO_T()
        try:
            ret = self._GetDeviceInfo(dev, info)
        except AttributeError:
            ret = LIBUSBSIO.ERR_FATAL
        if ret == LIBUSBSIO.OK:
            return info
        else:
            return None

    def IsOpen(self) -> bool:
        return bool(self.IsDllLoaded() and self._h)
    
    def IsAnyPortOpen(self) -> bool:
        return self.IsOpen() and len(self._ports_open) > 0

    @need_dll_loaded
    def Open(self, dev:int) -> bool:
        '''# Open USBSIO port
        This function opens the indexed port and binds the object with a port handle. Valid values for
        the index of port can be from 0 to the value obtained using GetNumPorts. The handle will be
        closed automatically in this object's destructor.

        ## Args
        - `dev` - Index of the port to be open.

        ## Returns
        Boolean True if open was successful. False if not successful.
        '''
        self.logger.info("Opening SIODevice%d" % dev)
        self._h = self._Open(dev)
        self._devIx = dev
        self.logger.debug("SIODevice%d Open returns %s" % (dev, self._h))
        self._ports_open = []
        return bool(self._h)

    @need_dll_loaded
    def Close(self) -> int:
        '''# Close USBSIO port
        This function closes the USBSIO port handle bound to this object.

        ## Returns
        Zero on success. Negative error code if operation failed.
        '''
        ret = 1
        if self._h:
            ret = self._Close(self._h)
            self.logger.info("Closed SIODevice%d code=%d" % (self._devIx, ret))
            self._h = None
            self._devIx = None
        return ret

    @need_dll_open
    def GetVersion(self) -> str:
        '''# Get USBSIO version

        ## Returns
        Library version string and USBSIO firmware version. Split the two strings by '/'.
        '''
        s = self._GetVersion(self._h)
        return str(s, 'utf-8')

    @need_dll_open
    def Error(self) -> str:
        '''# Get last error

        ## Returns
        Last error string.
        '''
        s = self._Error(self._h)
        return str(s)

    @need_dll_open
    def GetNumI2CPorts(self) -> int:
        '''# Get I2C ports count
        Get number of I2C ports available to be open by I2C_Open call.

        ## Returns
        Number of I2C ports available.
        '''
        ret = self._GetNumI2CPorts(self._h)
        return ret

    @need_dll_open
    def GetNumSPIPorts(self) -> int:
        '''# Get SPI ports count
        Get number of SPI ports available to be open by SPI_Open call.

        ## Returns
        Number of SPI ports available.
        '''
        ret = self._GetNumSPIPorts(self._h)
        return ret

    @need_dll_open
    def GetNumGPIOPorts(self) -> int:
        '''# Get GPIO ports count
        Get number of GPIO ports available.

        ## Returns
        Number of GPIO ports available.
        '''
        ret = self._GetNumGPIOPorts(self._h)
        return ret

    @need_dll_open
    def GetMaxDataSize(self) -> int:
        '''# Get maximum data size
        Get maximum number of bytes supported for I2C/SPI transfers by the USBSIO device.

        ## Returns
        Maximum size of I2C/SPI transfer.
        '''
        ret = self._GetMaxDataSize(self._h)
        return ret

    @need_dll_open
    def GetLastError(self) -> int:
        '''# Get last error

        ## Returns
        Last error code.
        '''
        ret = self._GetLastError(self._h)
        return ret

    class PORT:
        def __init__(self, libsio):
            self._sio: LIBUSBSIO = libsio
            self._h = None
            self.logger = None

        def _check_port_open(self, name):
            '''Internal I2C or SPI port validation'''
            if not self._sio:
                raise LIBUSBSIO_Exception("Invalid LIBUSBSIO port.")
            if not self._sio._h:
                raise LIBUSBSIO_Exception("DLL is not open.")
            if not self._h:
                raise LIBUSBSIO_Exception("%s port is not open." % name)
            return True

    def _I2C_NormalXferOptions(options:int, start:bool, stop:bool, ignoreNAK:bool, nackLastByte:bool, noAddress:bool) -> int:
        '''Convert I2C Read/Write transfer parameters to low-level option flags'''
        if start:
            options |= LIBUSBSIO.I2C_TRANSFER_OPTIONS_START_BIT
        if stop:
            options |= LIBUSBSIO.I2C_TRANSFER_OPTIONS_STOP_BIT
        if not ignoreNAK:
            options |= LIBUSBSIO.I2C_TRANSFER_OPTIONS_BREAK_ON_NACK
        if nackLastByte:
            options |= LIBUSBSIO.I2C_TRANSFER_OPTIONS_NACK_LAST_BYTE
        if noAddress:
            options |= LIBUSBSIO.I2C_TRANSFER_OPTIONS_NO_ADDRESS
        return options

    def _I2C_FastXferOptions(options:int, ignoreNAK:bool, nackLastByte:bool) -> int:
        '''Convert I2C Fast transfer parameters to low-level option flags'''
        if ignoreNAK:
            options |= LIBUSBSIO.I2C_FAST_XFER_OPTION_IGNORE_NACK
        if not nackLastByte:
            options |= LIBUSBSIO.I2C_FAST_XFER_OPTION_LAST_RX_ACK
        return options

    @need_dll_open
    def I2C_Open(self, clockRate:int=100000, options:int=0, portNum:int=0) -> 'LIBUSBSIO.I2C':
        '''# Open I2C port
        This function opens the requested I2C port with a specified clock rate.

        ## Args
        - `clockRate` The I2C clock rate.
        - `portNum`   I2C port number.

        ## Returns
        An object representing the open I2C port. `None` in case of failure.
        '''
        i2c = LIBUSBSIO.I2C(self, clockRate, options, portNum)
        if not i2c._h:
            i2c = None
        else:
            self._ports_open.append(i2c._h)
        return i2c

    class I2C(PORT):
        '''Class representing the open I2C port'''

        def __init__(self, libsio, clockRate:int, options:int, portNum:int):
            super().__init__(libsio)
            cfg = LIBUSBSIO.I2C_PORTCONFIG_T()
            cfg.ClockRate = clockRate
            cfg.Options = options
            self._portNum = portNum
            self.logger = logging.getLogger(f"libusbsio.i2c.{portNum}")
            self.logger.info("Opening I2C%d" % portNum)
            self._h = self._sio._I2C_Open(self._sio._h, cfg, portNum)
            if not self._h:
                self.logger.info("Opening I2C%d Failed" % portNum)

        def __del__(self):
            if self._h:
                self.Close()

        def need_port_open(func):
            '''# Need I2C port decorator
            Decorator assuring the USBSIO library is loaded, connection is open and I2C port is open.

            ## Args
            `func` function being decorated

            ## Returns
            The wrapper function.
            '''
            @functools.wraps(func)
            def wrapper(self, *args, **kwargs):
                self._check_port_open("I2C")
                return func(self, *args, **kwargs)
            return wrapper

        @need_port_open
        def Close(self) -> int:
            '''# Close I2C port
            Close I2C port bound to this object.

            ## Returns
            Zero on success. Negative error code if operation failed.
            '''
            ret = self._sio._I2C_Close(self._h)
            if ret == 0:
                self._sio._ports_open.remove(self._h)
            self.logger.info("Closed I2C%d code=%d" % (self._portNum, ret))
            self._h = None
            self._sio = None
            return ret

        @need_port_open
        def Reset(self) -> int:
            '''# Reset I2C port
            Reset the I2C master module.

            ## Returns
            Zero on success. Negative error code if operation failed.
            '''
            ret = self._sio._I2C_Reset(self._h)
            return ret

        @need_port_open
        def DeviceRead(self, devAddr:int, rxSize:int, start:bool=True, stop:bool=True, ignoreNAK:bool=False, nackLastByte:bool=True, noAddress:bool=False) -> Tuple[bytes,int]:
            '''# I2C Read
            Perform I2C Read operation.

            ## Args
            - `devAddr`      - Device I2C address
            - `rxSize`       - Number of bytes to read
            - `start`        - Generate start condition before the transfer
            - `stop`         - Generate stop condition at the end of transfer
            - `ignoreNAK`    - If 0: Stop reading when the device nACKs
                               If 1: Continue reading data in bulk without caring about ACK or nACK from device.
            - `nackLastByte` - If 0: sends ACK for every byte read.
                               If 1: generate nACK for the last byte read.
            - `noAddress`    - If 1: ignore the `devAddr` and generate special I2C frame that doesn't contain an address.

            ## Returns
            Tuple of received data buffer and operation result code indicating number of bytes received
            or an error code if negative.
            '''
            rxBuff = (c_uint8 * rxSize)()
            options = LIBUSBSIO._I2C_NormalXferOptions(0, start, stop, ignoreNAK, nackLastByte, noAddress)

            if(self.logger.isEnabledFor(logging.DEBUG)):
                self.logger.debug("I2C%d reading %d" % (self._portNum, rxSize))

            ret = self._sio._I2C_DeviceRead(self._h, devAddr, rxBuff, rxSize, options)
            if(ret > 0):
                rxData = bytes(rxBuff[0:ret])
            else:
                rxData = b''

            if(self.logger.isEnabledFor(logging.DEBUG)):
                self.logger.debug("I2C%d read status=%d, received: %s" % (self._portNum, ret, LIBUSBSIO.buffprint(rxData)))
            return (rxData, ret)

        @need_port_open
        def DeviceWrite(self, devAddr:int, txData:bytes, txSize:int=0, start:bool=True, stop:bool=True, ignoreNAK:bool=False, noAddress:bool=False) -> int:
            '''# I2C Write
            Perform I2C Write operation.

            ## Args
            - `devAddr`      - Device I2C address
            - `txData`       - Data to write
            - `txSize`       - Number of bytes to write (auto-inferred from txData if zero)
            - `start`        - Generate start condition before transmitting
            - `stop`         - Generate stop condition at the end of transfer
            - `ignoreNAK`    - If 0: Stop transmitting the data in the buffer when the device nACKs
                               If 1: Continue transmitting data in bulk without caring about ACK or nACK from device.
            - `nackLastByte` - If 0: the USBSIO sends ACKs for every byte read.
                               If 1: generate nACK for the last byte read - this might be required by some I2C slave devices.
            - `noAddress`    - If 1: ignore the `devAddr` and generate special I2C frame that doesn't require an address.  ignored.
                               Use for example when transferring a large frame split to multiple transfers.

            ## Returns
            Number of bytes transmitted or an error code if negative.
            '''
            if not txSize:
                txSize = len(txData)
            txBuff = (c_uint8 * txSize)(*txData)
            options = LIBUSBSIO._I2C_NormalXferOptions(0, start, stop, ignoreNAK, False, noAddress)

            if(self.logger.isEnabledFor(logging.DEBUG)):
                self.logger.debug("I2C%d writing [%d]: %s" % (self._portNum, txSize, LIBUSBSIO.buffprint(txData)))

            ret = self._sio._I2C_DeviceWrite(self._h, devAddr, txBuff, txSize, options)

            if(self.logger.isEnabledFor(logging.DEBUG)):
                self.logger.debug("I2C%d write status=%d" % (self._portNum, ret))
            return ret

        @need_port_open
        def FastXfer(self, devAddr:int, txData:bytes=None, txSize:int=0, rxSize:int=0, ignoreNAK:bool=False, nackLastByte:bool=True) -> Tuple[bytes,int]:
            '''# I2C Transfer
            Perform I2C Write operation followed by a restart condition and read operation.

            ## Args
            - `devAddr`      - Device I2C address
            - `txData`       - Data to write
            - `txSize`       - Number of bytes to write (auto-inferred from txData if zero)
            - `rxSize`       - Number of bytes to read
            - `ignoreNAK`    - If 0: Stop transmitting the data in the buffer when the device nACKs
                               If 1: Continue transmitting data in bulk without caring about ACK or nACK from device.
            - `nackLastByte` - If 0: the USBSIO sends ACKs for every byte read.
                               If 1: generate nACK for the last byte read - this might be required by some I2C slave devices.

            ## Returns
            Tuple of received data buffer and operation result code indicating number of bytes received
            or an error code if negative.
            '''
            if (not txSize) and txData:
                txSize = len(txData)
            txBuff = (c_uint8 * txSize)(*txData) if txSize else None
            rxBuff = (c_uint8 * rxSize)()
            xfer = LIBUSBSIO.I2C_FAST_XFER_T()
            xfer.txSz = txSize
            xfer.rxSz = rxSize
            xfer.options = LIBUSBSIO._I2C_FastXferOptions(0, ignoreNAK, nackLastByte)
            xfer.slaveAddr = devAddr
            xfer.txBuff = txBuff
            xfer.rxBuff = rxBuff

            if(self.logger.isEnabledFor(logging.DEBUG)):
                self.logger.debug("I2C%d xfer: writing[%d]: %s, reading:%d" % (self._portNum, txSize, LIBUSBSIO.buffprint(txData), rxSize))

            ret = self._sio._I2C_FastXfer(self._h, xfer)
            if(ret > 0):
                rxData = bytes(rxBuff)
            else:
                rxData = b''

            if(self.logger.isEnabledFor(logging.DEBUG)):
                self.logger.debug("I2C%d status=%d, received: %s" % (self._portNum, ret, LIBUSBSIO.buffprint(rxData)))
            return (rxData, ret)

    def _SPI_OpenOptions(options:int, dataSize:int, cpol:int, cpha:int, preDelay:int, postDelay:int) -> int:
        '''Convert SPI Open parameters to low-level option flags'''
        if cpol:
            options |= LIBUSBSIO.SPI_CONFIG_OPTION_POL_1
        if cpha:
            options |= LIBUSBSIO.SPI_CONFIG_OPTION_PHA_1
        if not (options & 0xf):
            options |= (dataSize-1) & 0xf
        if not (options & LIBUSBSIO.SPI_CONFIG_OPTION_PRE_DELAY(0xff)):
            options |= LIBUSBSIO.SPI_CONFIG_OPTION_PRE_DELAY(preDelay)
        if not (options & LIBUSBSIO.SPI_CONFIG_OPTION_POST_DELAY(0xff)):
            options |= LIBUSBSIO.SPI_CONFIG_OPTION_POST_DELAY(postDelay)
        return options

    @need_dll_open
    def SPI_Open(self, busSpeed:int, portNum:int=0, dataSize:int=8, cpol:int=0, cpha:int=0, preDelay:int=0, postDelay:int=0) -> 'LIBUSBSIO.SPI':
        '''# Open SPI port
        This function opens the requested SPI port with a specified bus speed and clock polarity options.

        ## Args
        - `busSpeed`  The SPI clock speed.
        - `portNum`   SPI port number.
        - `dataSize`  SPI data size in bits.
        - `cpol`      Clock polarity low(0) or high(1)
        - `cpha`      Data captured with the first(0) clock edge or the second(1) clock edge
        - `preDelay`  Data Pre-delay in micro seconds max of 255
        - `postDelay` Data Post-delay in micro seconds max of 255

        ## Returns
        An object representing the open SPI port. `None` in case of failure.
        '''
        spi = LIBUSBSIO.SPI(self, busSpeed, LIBUSBSIO._SPI_OpenOptions(0, dataSize, cpol, cpha, preDelay, postDelay), portNum)
        if not spi._h:
            spi = None
        else:
            self._ports_open.append(spi._h)
        return spi

    class SPI(PORT):
        '''Class representing the open SPI port'''

        def __init__(self, libsio, busSpeed:int, options:int, portNum:int):
            '''# SPI constructor
            This function is called internally by the LIBUSBSIO.SPI_Open call.
            '''
            super().__init__(libsio)
            cfg = LIBUSBSIO.SPI_PORTCONFIG_T()
            cfg.busSpeed = busSpeed
            cfg.Options = options
            self._options = options
            self._portNum = portNum
            self.logger = logging.getLogger(f"libusbsio.spi.{portNum}")
            self.logger.info("Opening SPI%d" % portNum)
            self._h = self._sio._SPI_Open(self._sio._h, cfg, portNum)
            if not self._h:
                self.logger.info("Opening SPI%d Failed" % portNum)

        def __del__(self):
            '''# SPI destructor
            The SPI port handle is closed if previously open.
            '''
            if self._h:
                self.Close()

        def need_port_open(func):
            '''# Need SPI port decorator
            Decorator assuring the USBSIO library is loaded, connection is open and SPI port is open.

            ## Args
            `func` function being decorated

            ## Returns
            The wrapper function.
            '''
            @functools.wraps(func)
            def wrapper(self, *args, **kwargs):
                self._check_port_open("SPI")
                return func(self, *args, **kwargs)
            return wrapper

        @need_port_open
        def Close(self) -> int:
            '''# Close SPI port
            Close SPI port bound to this object.

            ## Returns
            Zero on success. Negative error code if operation failed.
            '''
            ret:int = self._sio._SPI_Close(self._h)
            if ret == 0:
                self._sio._ports_open.remove(self._h)
            self.logger.info("Closed SPI%d code=%d" % (self._portNum, ret))
            self._h = None
            self._sio = None
            return ret

        @need_port_open
        def Reset(self) -> int:
            '''# Reset the SPI module
            Reset the SPI master module.

            ## Returns
            Zero on success. Low-level library error code otherwise.
            '''
            ret:int = self._sio._SPI_Reset(self._h)
            return ret

        @need_port_open
        def Transfer(self, devSelectPort:int, devSelectPin:int, txData:bytes, size:int=0, options:int=0) -> Tuple[bytes,int]:
            '''# SPI Data Transfer
            Perform the SPI transmit operation of given size while receiving incoming data.

            ## Args:
            - `devSelectPort` GPIO port of the slave-select signal.
            - `devSelectPin`  GPIO pin of the slave-select signal.
            - `txData`        Data to transmit, zeroes will be sent if this is None.
            - `size`          Size of the transfer. Auto-inferred from txData if omitted.
            - `options`       Transfer options, unused.

            ## Returns
            Tuple of received data buffer and operation result code indicating number of bytes received
            or an error code if negative.
            '''
            if not size and txData:
                size = len(txData)
            if not txData:
                txData = b"\x00" * size
            txBuff = (c_uint8 * size)(*txData)
            rxBuff = (c_uint8 * size)()
            xfer = LIBUSBSIO.SPI_XFER_T()
            xfer.length = size
            xfer.options = options
            xfer.device = (((devSelectPort & 0x07) << 5) | (devSelectPin & 0x1F))
            xfer.txBuff = txBuff
            xfer.rxBuff = rxBuff

            if(self.logger.isEnabledFor(logging.DEBUG)):
                self.logger.debug("SPI%d SSEL%d.%d transmitting[%d]: %s" % (self._portNum, devSelectPort, devSelectPin, size, LIBUSBSIO.buffprint(txData)))

            ret:int = self._sio._SPI_Transfer(self._h, xfer)
            rxData = bytes(rxBuff)

            if(self.logger.isEnabledFor(logging.DEBUG)):
                self.logger.debug("SPI%d status=%d, received: %s" % (self._portNum, ret, LIBUSBSIO.buffprint(rxData)))
            return (rxData, ret)

    @need_dll_open
    def GPIO_ReadPort(self, port:int) -> Tuple[int,int]:
        '''# Read GPIO port
        Read all pins of the GPIO port as a 32bit number.

        ## Args:
        - `port` GPIO port number

        ## Returns
        Tuple of 32bit GPIO port value and operation result code, negative in case of error.
        '''
        val = c_uint32(0)
        ret = self._GPIO_ReadPort(self._h, port, val)
        return (val.value, ret)

    @need_dll_open
    def GPIO_WritePort(self, port:int, value:int) -> Tuple[int,int]:
        '''# Write GPIO port
        Write status of all GPIO port pins.

        ## Args:
        - `port`  GPIO port number
        - `value` Port value to be written

        ## Returns
        Tuple of 32bit GPIO port value read-back after the `value` is written and operation result code, negative in case of error.
        '''
        val = c_uint32(value)
        ret = self._GPIO_WritePort(self._h, port, val)
        return (val.value, ret)

    @need_dll_open
    def GPIO_SetPort(self, port:int, setpins:int) -> int:
        '''# Set GPIO port pins
        Set selected GPIO port pins high.

        ## Args:
        - `port`    GPIO port number
        - `setpins` Mask of pins which are to be set high

        ## Returns
        Operation result code, negative in case of error.
        '''
        ret = self._GPIO_SetPort(self._h, port, setpins)
        return ret

    @need_dll_open
    def GPIO_ClearPort(self, port:int, clrpins:int) -> int:
        '''# Clear GPIO port pins
        Clear selected GPIO port pins.

        ## Args:
        - `port`    GPIO port number
        - `clrpins` Mask of pins which are to be cleared

        ## Returns
        Operation result code, negative in case of error.
        '''
        ret = self._GPIO_ClearPort(self._h, port, clrpins)
        return ret

    @need_dll_open
    def GPIO_GetPortDir(self, port:int) -> Tuple[int,int]:
        '''# Get GPIO pins' direction
        Read direction status of all pins of the GPIO port.

        ## Args:
        - `port`    GPIO port number

        ## Returns
        Tuple of 32bit GPIO port direction value (0=input, 1=output) and operation result code, negative in case of error.
        '''
        val = c_uint32(0)
        ret = self._GPIO_GetPortDir(self._h, port, val)
        return (val.value, ret)

    @need_dll_open
    def GPIO_SetPortOutDir(self, port:int, outpins:int) -> int:
        '''# Set GPIO output pins
        Set selected GPIO port pins to output direction.

        ## Args:
        - `port`    GPIO port number
        - `outpins` Mask of pins which are to be set as output

        ## Returns
        Operation result code, negative in case of error.
        '''
        ret = self._GPIO_SetPortOutDir(self._h, port, outpins)
        return ret

    @need_dll_open
    def GPIO_SetPortInDir(self, port:int, inpins:int) -> int:
        '''# Set GPIO input pins
        Set selected GPIO port pins to input direction.

        ## Args:
        - `port`    GPIO port number
        - `outpins` Mask of pins which are to be set as input

        ## Returns
        Operation result code, negative in case of error.
        '''
        ret = self._GPIO_SetPortInDir(self._h, port, inpins)
        return ret

    @need_dll_open
    def GPIO_SetPin(self, port:int, pin:int) -> int:
        '''# Set GPIO pin
        Set selected GPIO port pin high.

        ## Args:
        - `port` GPIO port number
        - `pin`  Number (0..31) of a pin to be set high.

        ## Returns
        Operation result code, negative in case of error.
        '''
        ret = self._GPIO_SetPin(self._h, port, pin)
        return ret

    @need_dll_open
    def GPIO_ClearPin(self, port:int, pin:int) -> int:
        '''# Clear GPIO pin
        Clear selected GPIO port pin.

        ## Args:
        - `port` GPIO port number
        - `pin`  Number (0..31) of a pin to be cleared.

        ## Returns
        Operation result code, negative in case of error.
        '''
        ret = self._GPIO_ClearPin(self._h, port, pin)
        return ret

    @need_dll_open
    def GPIO_TogglePin(self, port:int, pin:int) -> int:
        '''# Toggle GPIO pin
        Toggle selected GPIO port pin.

        ## Args:
        - `port` GPIO port number
        - `pin`  Number (0..31) of a pin to be toggled.

        ## Returns
        Operation result code, negative in case of error.
        '''
        ret = self._GPIO_TogglePin(self._h, port, pin)
        return ret

    @need_dll_open
    def GPIO_GetPin(self, port:int, pin:int) -> int:
        '''# Get GPIO pin state
        Get selected GPIO port pin state as a boolean value.

        ## Args:
        - `port` GPIO port number
        - `pin`  Number (0..31) of a pin to be read.

        ## Returns
        GPIO Pin value.
        '''
        ret = self._GPIO_GetPin(self._h, port, pin)
        return ret

    @need_dll_open
    def GPIO_ConfigIOPin(self, port:int, pin:int, mode:int) -> int:
        '''# Internal pin configuration
        Re-confugure pin peripheral mode and define electrical properties.
        Advanced method, do not use unless you know what you are doing.

        ## Args:
        - `port` GPIO port number
        - `pin`  Number (0..31) of a pin to be configured.
        - `mode` Pin mode value, device-specific.

        ## Returns
        Operation result code, negative in case of error.
        '''
        ret = self._GPIO_ConfigIOPin(self._h, port, pin, mode)
        return ret

    @need_dll_loaded
    def HIDAPI_Enumerate(self, vidpid:'tuple[int,int]' = None, read_ex_info:bool = False) -> Generator[HIDAPI_DEVICE_INFO_T, None, None]:
        '''# USB HID enumeration generator

        ## Args:
        - `vidpid`   Tuple of [VID,PID] identifying the device types to enumerate.
                     Use None to enumerate all devices.
        - `read_ex_info` Set to True to open each enumerated device and retrieve additional
                     information in the `ex` members of the enumerated items.

        ## Returns
        Generator function 'G' suitable to be iterated using a `for ITEM in G` cycle.

        Each generated `ITEM` contains the `prop` member which is a dictionary of all valid
        properties. The properties are also accessible directly as ctype structure members:
        - `path` - system path used to open the device using HIDAPI_DeviceOpen
        - `serial_number` - serial number string
        - `manufacturer_string` - manufacturer name string
        - `product_string` - product name string
        - `interface_number` - internal USB interface number
        - `vendor_id` - device VID
        - `product_id` - device PID
        - `release_number` - device release number
        - `ex` - sub-structure with extra information retrieved by opening the device.
           You need to set the `read_ex_info` parameter to True to obtain this information.
        - `ex.is_valid` - indicates if extra information is valid.
        - `ex.output_report_length` - maximum size of output report payload
        - `ex.input_report_length` - maximum size of input report payload
        - `ex.usage_page` - device's usage_page set in the report descriptor
        - `ex.usage` - the first usage entry seen in the report descriptor
        '''

        if not vidpid:
            vidpid = (0,0)    # enumerate all devices

        info = LIBUSBSIO.HIDAPI_DEVICE_INFO_T()
        h = self._HIDAPI_Enumerate(vidpid[0], vidpid[1], read_ex_info)
        if h:
            self.logger.info("HID enumeration[%d]: initialized" % h)
            i = 0
            while(self._HIDAPI_EnumerateNext(h, info) != 0):
                self.logger.debug("HID enumeration[%d]: device #%d: %s" % (h, i, info.product_string))
                i = i+1
                try:
                    yield info
                except GeneratorExit:
                    break
            self.logger.info("HID enumeration[%d]: finished, total %d devices" % (h, i))
            self._HIDAPI_EnumerateFree(h)
        else:
            self.logger.info("HID enumeration failed to initialize")

    class HID_DEVICE:
        def __init__(self, libsio, openpath:str=None):
            self._sio = libsio
            self._h = None
            self.logger = logging.getLogger(f"libusbsio.hidapi.dev")
            if openpath:
                self.Open(openstr)

        def __del__(self):
            if self._h:
                self.Close()

        def _check_device_open(self):
            '''Internal HID_DEVICE validation'''
            if not self._sio:
                raise LIBUSBSIO_Exception("LIBUSBSIO library not loaded.")
            if not self._h:
                raise LIBUSBSIO_Exception("HID DEVICE is not open.")
            return True

        def need_device_open(func):
            '''# Need HID device decorator
            Decorator assuring the HID_DEVICE is valid

            ## Args
            - `func` function being decorated

            ## Returns
            The wrapper function.
            '''
            @functools.wraps(func)
            def wrapper(self, *args, **kwargs):
                self._check_device_open()
                return func(self, *args, **kwargs)
            return wrapper

        def Open(self, path:str) -> bool:
            '''# Open HID device
            Open low-level hid device.

            ## Args
            - `path` HID device path as specified in `path` property of object returned by HIDAPI_Enumerate().

            ## Returns
            True if successfull.
            '''
            if not self._sio:
                raise LIBUSBSIO_Exception("LIBUSBSIO library not loaded.")
            if not path:
                raise LIBUSBSIO_Exception("HID Device path must be specified.")
            self.logger.info("Opening HID device at path: '%s'" % path)
            self._h = self._sio._HIDAPI_DeviceOpen(path)
            if self._h:
                self.logger.info("HID device %d is now open" % self._h)
            else:
                self.logger.error("HID device '%s' opening failed." % path)
            return self._h != 0

        @need_device_open
        def Close(self) -> bool:
            '''# Close HID device
            Close low-level hid device.

            ## Returns
            True if successfull.
            '''
            h = self._h
            self._h = None
            ret = self._sio._HIDAPI_DeviceClose(h)
            self.logger.info("HID device %d closed" % h)
            return ret == 0

        @need_device_open
        def Write(self, data:bytes, size:int = 0, timeout_ms:int = 0) -> int:
            '''# Write to HID device
            Write output data report to device.

            ## Args
            - `data`       Data to be sent, use byte buffer.
            - `size`       Size of the data to be sent, `len(data)` is used when zero.
            - `timeout_ms` Write timeout. Host tries to repeat failed write operations until this timeout expires.

            ## Returns
            Total number of bytes written to to device. Note that this may be smaller or even larger value than
            the `size` depending on the physical HID output report size.
            '''
            if not size:
                size = len(data)

            if(self.logger.isEnabledFor(logging.DEBUG)):
                self.logger.debug("HID device %d writing[%d]: %s" % (self._h, size, LIBUSBSIO.buffprint(data)))

            buff = (c_uint8 * size)(*data)
            ret = self._sio._HIDAPI_DeviceWrite(self._h, buff, size, timeout_ms)
            self.logger.debug("HID device %d wrote %d bytes" % (self._h, ret))
            return ret

        @need_device_open
        def Read(self, size:int, timeout_ms:int) -> Tuple[bytes,int]:
            '''# Read from HID device
            Read input data report from device.

            ## Args
            - `data`       Data to be sent, use byte buffer.
            - `size`       Size of the data to be read.
            - `timeout_ms` Write timeout. Host tries to repeat failed write operations until this timeout expires.

            ## Returns
            Tuple of data read and a result code. If result code is a positive number, it indicates number of data
            bytes received, otherwise it is an error code.
            '''
            buff = (c_uint8 * size)()
            ret = self._sio._HIDAPI_DeviceRead(self._h, buff, size, timeout_ms)

            if(ret > 0):
                data = bytes(buff[0:ret])
            else:
                data = b''

            if(self.logger.isEnabledFor(logging.DEBUG)):
                self.logger.debug("HID device %d read[%d]: %s" % (self._h, ret, LIBUSBSIO.buffprint(data)))
            return (data, ret)

    @need_dll_loaded
    def HIDAPI_DeviceCreate(self) -> HID_DEVICE:
        '''# Create HID device object.
        Create an un-initialized hid device wrapper object.

        ## Returns
        HID device object.
        '''
        return LIBUSBSIO.HID_DEVICE(self)

    @need_dll_loaded
    def HIDAPI_DeviceOpen(self, path:str) -> HID_DEVICE:
        '''# Open HID device
        Open low-level hid device and return its wrapper object.

        ## Args
        - `path` HID device path as specified in `path` property of object returned by HIDAPI_Enumerate().

        ## Returns
        Open HID device object or `None` if device could not be open.
        '''
        dev = LIBUSBSIO.HID_DEVICE(self)
        if not dev.Open(path):
            dev = None
        return dev


# the LIBUSBSIO singleton object
_LIBUSBSIO_SINGLETON:LIBUSBSIO = None

def usbsio(dllpath:str=None, loglevel:int=logging.NOTSET, autoload=True) -> LIBUSBSIO:
    '''# LIBUSBSIO singleton object
    The returned instance acts as kind-of 'singleton' object, created
    upon the first call of this function. All subsequent calls returns
    the same instance.

    ## Args
    All arguments are passed to LIBUSBSIO constructor when the instance
    is created for the first time. The arguments are ignored when the instance
    already exists.
    - `dllpath`  Path to LIBUSBSIO dynamic library. Use None to autodetect based on operating system.
    - `loglevel` Internal logging level, defaults to logging.NOTSET.
    - `autoload` Set to load the DLL library automatically.
    '''
    global _LIBUSBSIO_SINGLETON
    if not _LIBUSBSIO_SINGLETON:
        _LIBUSBSIO_SINGLETON = LIBUSBSIO(dllpath=dllpath, loglevel=loglevel, autoload=autoload)
    return _LIBUSBSIO_SINGLETON
