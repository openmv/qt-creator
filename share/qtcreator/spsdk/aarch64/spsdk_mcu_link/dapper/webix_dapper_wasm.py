#!/usr/bin/env python
# -*- coding: utf-8 -*-
#
# Copyright 2024 NXP
# Copyright 2025 NXP
#
# SPDX-License-Identifier: BSD-3-Clause

import ctypes
import logging
import os
import sys
import types
from time import sleep
from typing import Any, Callable, Optional, Tuple, Union, cast

import wasmtime
from wasmtime import (
    Config,
    Engine,
    Func,
    FuncType,
    Instance,
    Limits,
    Linker,
    Memory,
    MemoryType,
    Module,
    Store,
)

from .core import Int32Array, Uint8Array

logger = logging.getLogger(__name__)
if logger.level == logging.NOTSET:
    logger.setLevel(logging.INFO)
    # logger.setLevel(logging.DEBUG)

if not logger.handlers:
    handler = logging.StreamHandler()
    formatter = logging.Formatter("%(levelname)s:%(name)s:%(message)s")
    handler.setFormatter(formatter)
    logger.addHandler(handler)


class WasmExceptionInfo:
    """Represents information about a WebAssembly exception."""

    def __init__(self, exc_ptr: int, memory: wasmtime.Memory, store: wasmtime.Store) -> None:
        """Initialize WasmExceptionInfo.

        :param exc_ptr: Pointer to the exception in WASM memory
        :param memory: WASM memory instance
        :param store: WASM store instance
        """
        self.exc_ptr = exc_ptr
        self.ptr = exc_ptr - 24
        self.memory = memory
        self.store = store

    def get_type(self) -> int:
        """Get the exception type.

        :return: Integer representing the exception type
        """
        return int.from_bytes(
            self.memory.read(self.store, self.ptr + 4, self.ptr + 4 + 4), "little"
        )

    def get_destructor(self) -> int:
        """Get the destructor function pointer.

        :return: Integer representing the destructor function pointer
        """
        return int.from_bytes(
            self.memory.read(self.store, self.ptr + 8, self.ptr + 8 + 4), "little"
        )

    def set_caught(self, caught: bool) -> None:
        """Set whether the exception has been caught.

        :param caught: Boolean indicating if the exception was caught
        """
        caught_val = 1 if caught else 0
        self.memory.write(self.store, bytes([caught_val & 0xFF]), self.ptr + 12)

    def set_type(self, ex_type: int) -> None:
        """Set the exception type.

        :param ex_type: Integer representing the exception type
        """
        self.memory.write(self.store, ex_type.to_bytes(4, "little"), self.ptr + 4)

    def set_destructor(self, destructor: int) -> None:
        """Set the destructor function pointer.

        :param destructor: Integer representing the destructor function pointer
        """
        self.memory.write(self.store, destructor.to_bytes(4, "little"), self.ptr + 8)

    def set_adjusted_ptr(self, adjusted_ptr: int) -> None:
        """Set the adjusted pointer value.

        :param adjusted_ptr: Integer representing the adjusted pointer value
        """
        self.memory.write(self.store, adjusted_ptr.to_bytes(4, "little"), self.ptr + 16)

    def init(self, ex_type: int, destructor: int) -> None:
        """Initialize the exception info with type and destructor.

        :param ex_type: Integer representing the exception type
        :param destructor: Integer representing the destructor function pointer
        """
        self.set_adjusted_ptr(0)
        self.set_type(ex_type)
        self.set_destructor(destructor)


class WebixDapperWasm:
    def __init__(self, context_path: Optional[str] = None) -> None:
        self.trace = False
        self.with_stack_control = False
        config = Config()
        config.cranelift_opt_level = "speed"
        config.strategy = "cranelift"
        engine = Engine(config)
        self.store = Store(engine)
        memory = Memory(
            self.store, MemoryType(limits=Limits(min=1, max=int(2147483648 / (64 * 1024))))
        )

        self.linker = Linker(self.store.engine)
        self.linker.define(self.store, "env", "memory", memory)
        if context_path is None:
            context_path = os.path.abspath(
                os.path.join(os.path.dirname(__file__), "webix-dapper-wasm.wasm")
            )
        self.module = Module.from_file(self.linker.engine, context_path)

        self.instance = Instance(self.store, self.module, self.construct_imports())
        self.exports: dict[str, Callable[..., Any]] = cast(
            dict[str, Callable[..., Any]], self.instance.exports(self.store)
        )
        self.memory: wasmtime.Memory = cast(wasmtime.Memory, self.exports["memory"])

        ptr = self.memory.data_ptr(self.store)
        size = self.memory.data_len(self.store)

        if not ptr:
            raise RuntimeError("Invalid memory pointer")

        c_array = (ctypes.c_ubyte * size).from_address(ctypes.addressof(ptr.contents))
        self.HEAPU8 = Uint8Array(c_array)

        self.registered_types: dict[str, Any] = {}
        self.type_dependencies: dict[int, Any] = {}
        self.awaiting_dependencies: dict[str, Any] = {}
        self.emval_handles: list[Union[int, bool, float, None]] = []
        self.emval_freelist: list[int] = []
        self.emval_method_callers: list[Callable] = []
        self.emval_symbols: dict[str, Any] = {}
        self.struct_registrations: dict[str, Any] = {}

        def from_wire_type(handle: int) -> Any:
            rv = self.emval_to_value(handle)
            self._emval_decref(handle)
            return rv

        def read_emval_ptr(ptr: int) -> Any:
            return from_wire_type(
                int.from_bytes(self.memory.read(self.store, ptr, ptr + 4), "little")
            )

        self.EmValType = {
            "name": "emscripten::val",
            "fromWireType": from_wire_type,
            "toWireType": lambda destructors, value: self.emval_to_handle(value),
            "argPackAdvance": 8,
            "readValueFromPointer": read_emval_ptr,
            "destructorFunction": lambda: None,
        }

        self._stack_save = self.create_export_wrapper("stackSave")
        self._stack_restore = self.create_export_wrapper("stackRestore")
        self.dynCall_ii = self.create_export_wrapper("dynCall_ii")
        self.dynCall_iiii = self.create_export_wrapper("dynCall_iiii")
        self.dynCall_iii = self.create_export_wrapper("dynCall_iii")
        self.dynCall_vi = self.create_export_wrapper("dynCall_vi")
        self.dynCall_vii = self.create_export_wrapper("dynCall_vii")
        self.dynCall_viii = self.create_export_wrapper("dynCall_viii")
        self.dynCall_diiii = self.create_export_wrapper("dynCall_diiii")
        self.dynCall_vid = self.create_export_wrapper("dynCall_vid")
        self.dynCall_viiiii = self.create_export_wrapper("dynCall_viiiii")
        self.dynCall_v = self.create_export_wrapper("dynCall_v")
        self.dynCall_i = self.create_export_wrapper("dynCall_i")
        self.dynCall_id = self.create_export_wrapper("dynCall_id")
        self.dynCall_f = self.create_export_wrapper("dynCall_f")
        self.dynCall_ff = self.create_export_wrapper("dynCall_ff")
        self.dynCall_viiiiii = self.create_export_wrapper("dynCall_viiiiii")
        self.dynCall_fii = self.create_export_wrapper("dynCall_fii")
        self.dynCall_viif = self.create_export_wrapper("dynCall_viif")
        self.dynCall_fi = self.create_export_wrapper("dynCall_fi")
        self.dynCall_fif = self.create_export_wrapper("dynCall_fif")
        self.dynCall_jiji = self.create_export_wrapper("dynCall_jiji")
        self.dynCall_iidiiii = self.create_export_wrapper("dynCall_iidiiii")
        self.dynCall_viijii = self.create_export_wrapper("dynCall_viijii")
        self.dynCall_viiii = self.create_export_wrapper("dynCall_viiii")
        self.dynCall_iiiiiiii = self.create_export_wrapper("dynCall_iiiiiiii")
        self.dynCall_iiiiiiiiiii = self.create_export_wrapper("dynCall_iiiiiiiiiii")
        self.dynCall_iiiii = self.create_export_wrapper("dynCall_iiiii")
        self.dynCall_jiiii: Callable[..., Any] = self.create_export_wrapper("dynCall_jiiii")
        self.dynCall_iiiiiiiiiiiii = self.create_export_wrapper("dynCall_iiiiiiiiiiiii")
        self.dynCall_fiii = self.create_export_wrapper("dynCall_fiii")
        self.dynCall_diii = self.create_export_wrapper("dynCall_diii")
        self.dynCall_viiiiiii = self.create_export_wrapper("dynCall_viiiiiii")
        self.dynCall_iiiiiii = self.create_export_wrapper("dynCall_iiiiiii")
        self.dynCall_iiiiiiiiiiii = self.create_export_wrapper("dynCall_iiiiiiiiiiii")
        self.dynCall_viiiiiiii = self.create_export_wrapper("dynCall_viiiiiiii")
        self.dynCall_viiiiiiiii = self.create_export_wrapper("dynCall_viiiiiiiii")
        self.dynCall_viiiiiiiiii = self.create_export_wrapper("dynCall_viiiiiiiiii")
        self.dynCall_viiiiiiiiiii = self.create_export_wrapper("dynCall_viiiiiiiiiii")
        self.dynCall_viiiiiiiiiiii = self.create_export_wrapper("dynCall_viiiiiiiiiiii")
        self.dynCall_viiiiiiiiiiiii = self.create_export_wrapper("dynCall_viiiiiiiiiiiii")
        self.dynCall_viiiiiiiiiiiiii = self.create_export_wrapper("dynCall_viiiiiiiiiiiiii")
        self.dynCall_viiiiiiiiiiiiiii = self.create_export_wrapper("dynCall_viiiiiiiiiiiiiii")
        self.dynCall_iiiiii = self.create_export_wrapper("dynCall_iiiiii")
        self.dynCall_iiiiiiiii = self.create_export_wrapper("dynCall_iiiiiiiii")
        self.dynCall_iiiiiiiiii = self.create_export_wrapper("dynCall_iiiiiiiiii")
        self.dynCall_iiiiij = self.create_export_wrapper("dynCall_iiiiij")
        self.dynCall_iiiiid = self.create_export_wrapper("dynCall_iiiiid")
        self.dynCall_iiiiijj = self.create_export_wrapper("dynCall_iiiiijj")
        self.dynCall_iiiiiijj = self.create_export_wrapper("dynCall_iiiiiijj")

    def __getitem__(self, item: str) -> Any:
        if item == "HEAPU8":
            return self.HEAPU8

        raise KeyError(f"{item} not found")

    def emval_to_value(self, handle: int) -> Any:
        if handle is None:
            raise ValueError(f"Cannot use deleted val. handle = {handle}")
        return self.emval_handles[handle]

    def emval_to_handle(self, value: Any) -> int:
        if value is None:
            ret_val = 2
        elif value is True:
            ret_val = 6
        elif value is False:
            ret_val = 8
        else:
            handle = self.emval_freelist.pop() if self.emval_freelist else len(self.emval_handles)

            if handle is None:
                raise ValueError(f"Cannot use deleted val. handle = {handle}")

            if handle < len(self.emval_handles):
                self.emval_handles[handle] = value
            else:
                self.emval_handles.extend([0] * (handle - len(self.emval_handles)))
                self.emval_handles.append(value)

            if handle + 1 < len(self.emval_handles):
                self.emval_handles[handle + 1] = 1
            else:
                self.emval_handles.extend([0] * (handle + 1 - len(self.emval_handles)))
                self.emval_handles.append(1)

            ret_val = handle

        return ret_val

    def init_emval(self) -> None:
        self.emval_handles.append(0)
        self.emval_handles.append(1)
        self.emval_handles.append(None)
        self.emval_handles.append(1)
        self.emval_handles.append(None)
        self.emval_handles.append(1)
        self.emval_handles.append(True)
        self.emval_handles.append(1)
        self.emval_handles.append(False)
        self.emval_handles.append(1)

    def _emval_decref(self, handle: int) -> None:
        item = self.emval_handles[handle + 1]
        if isinstance(item, int):
            if handle > 9 == item - 1:
                self.emval_handles[handle] = None
                self.emval_freelist.append(handle)

    def _emval_incref(self, handle: int) -> None:
        item = self.emval_handles[handle + 1]
        if isinstance(item, int):
            if handle > 9:
                item += 1
                self.emval_handles[handle + 1] = item

    def runtime_init(self) -> None:
        self.exports["__wasm_call_ctors"](self.store)
        self.init_emval()

    def create_export_wrapper(self, name: str) -> Callable[..., Any]:
        if name in self.exports:
            f = self.exports[name]

            def wrapper(*args: Tuple[Any, ...]) -> Any:
                return f(*args)

            return wrapper
        return lambda *args: None

    def invoke(self, name: str, *args: Tuple[Any, ...]) -> Any:
        return self.exports[name](self.store, *args)

    def invoke_dyn(self, name: str, *args: Tuple[Any, ...]) -> Any:
        return getattr(self, name)(*args)

    def invoke_jiiii(self, index: int, a1: int, a2: int, a3: int, a4: int) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_jiiii(self.store, index, a1, a2, a3, a4)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_diii(self, index: int, a1: int, a2: int, a3: int) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_diii(self.store, index, a1, a2, a3)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_fiii(self, index: int, a1: int, a2: int, a3: int) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_fiii(self.store, index, a1, a2, a3)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_v(self, index: int) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_v(self.store, index)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_vi(self, index: int, a1: int) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_vi(self.store, index, a1)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_vii(self, index: int, a1: int, a2: int) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_vii(self.store, index, a1, a2)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viii(self, index: int, a1: int, a2: int, a3: int) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viii(self.store, index, a1, a2, a3)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viiii(self, index: int, a1: int, a2: int, a3: int, a4: int) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viiii(self.store, index, a1, a2, a3, a4)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viiiii(self, index: int, a1: int, a2: int, a3: int, a4: int, a5: int) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viiiii(self.store, index, a1, a2, a3, a4, a5)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viiiiii(
        self, index: int, a1: int, a2: int, a3: int, a4: int, a5: int, a6: int
    ) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viiiiii(self.store, index, a1, a2, a3, a4, a5, a6)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viiiiiii(
        self, index: int, a1: int, a2: int, a3: int, a4: int, a5: int, a6: int, a7: int
    ) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viiiiiii(self.store, index, a1, a2, a3, a4, a5, a6, a7)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viiiiiiii(
        self, index: int, a1: int, a2: int, a3: int, a4: int, a5: int, a6: int, a7: int, a8: int
    ) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viiiiiiii(self.store, index, a1, a2, a3, a4, a5, a6, a7, a8)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viiiiiiiii(
        self,
        index: int,
        a1: int,
        a2: int,
        a3: int,
        a4: int,
        a5: int,
        a6: int,
        a7: int,
        a8: int,
        a9: int,
    ) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viiiiiiiii(self.store, index, a1, a2, a3, a4, a5, a6, a7, a8, a9)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viiiiiiiiii(
        self,
        index: int,
        a1: int,
        a2: int,
        a3: int,
        a4: int,
        a5: int,
        a6: int,
        a7: int,
        a8: int,
        a9: int,
        a10: int,
    ) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viiiiiiiiii(self.store, index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viiiiiiiiiii(
        self,
        index: int,
        a1: int,
        a2: int,
        a3: int,
        a4: int,
        a5: int,
        a6: int,
        a7: int,
        a8: int,
        a9: int,
        a10: int,
        a11: int,
    ) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viiiiiiiiiii(
                self.store, index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11
            )
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viiiiiiiiiiii(
        self,
        index: int,
        a1: int,
        a2: int,
        a3: int,
        a4: int,
        a5: int,
        a6: int,
        a7: int,
        a8: int,
        a9: int,
        a10: int,
        a11: int,
        a12: int,
    ) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viiiiiiiiiiii(
                self.store, index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12
            )
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viiiiiiiiiiiii(
        self,
        index: int,
        a1: int,
        a2: int,
        a3: int,
        a4: int,
        a5: int,
        a6: int,
        a7: int,
        a8: int,
        a9: int,
        a10: int,
        a11: int,
        a12: int,
        a13: int,
    ) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viiiiiiiiiiiii(
                self.store, index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13
            )
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viiiiiiiiiiiiii(
        self,
        index: int,
        a1: int,
        a2: int,
        a3: int,
        a4: int,
        a5: int,
        a6: int,
        a7: int,
        a8: int,
        a9: int,
        a10: int,
        a11: int,
        a12: int,
        a13: int,
        a14: int,
    ) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viiiiiiiiiiiiii(
                self.store, index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14
            )
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_viiiiiiiiiiiiiii(
        self,
        index: int,
        a1: int,
        a2: int,
        a3: int,
        a4: int,
        a5: int,
        a6: int,
        a7: int,
        a8: int,
        a9: int,
        a10: int,
        a11: int,
        a12: int,
        a13: int,
        a14: int,
        a15: int,
    ) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_viiiiiiiiiiiiiii(
                self.store, index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15
            )
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_vid(self, index: int, a1: int, a2: int) -> None:
        sp = self.stack_save()
        try:
            self.dynCall_vid(self.store, index, a1, a2)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_i(self, index: int) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_i(self.store, index)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_ii(self, index: int, a1: int) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_ii(self.store, index, a1)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_iii(self, index: int, a1: int, a2: int) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_iii(self.store, index, a1, a2)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_iiii(self, index: int, a1: int, a2: int, a3: int) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_iiii(self.store, index, a1, a2, a3)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_iiiii(self, index: int, a1: int, a2: int, a3: int, a4: int) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_iiiii(self.store, index, a1, a2, a3, a4)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_iiiiii(self, index: int, a1: int, a2: int, a3: int, a4: int, a5: int) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_iiiiii(self.store, index, a1, a2, a3, a4, a5)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_iiiiiii(
        self, index: int, a1: int, a2: int, a3: int, a4: int, a5: int, a6: int
    ) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_iiiiiii(self.store, index, a1, a2, a3, a4, a5, a6)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_iiiiiiii(
        self, index: int, a1: int, a2: int, a3: int, a4: int, a5: int, a6: int, a7: int
    ) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_iiiiiiii(self.store, index, a1, a2, a3, a4, a5, a6, a7)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_iiiiiiiii(
        self, index: int, a1: int, a2: int, a3: int, a4: int, a5: int, a6: int, a7: int, a8: int
    ) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_iiiiiiiii(self.store, index, a1, a2, a3, a4, a5, a6, a7, a8)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_iiiiiiiiii(
        self,
        index: int,
        a1: int,
        a2: int,
        a3: int,
        a4: int,
        a5: int,
        a6: int,
        a7: int,
        a8: int,
        a9: int,
    ) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_iiiiiiiiii(self.store, index, a1, a2, a3, a4, a5, a6, a7, a8, a9)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_iiiiiiiiiii(
        self,
        index: int,
        a1: int,
        a2: int,
        a3: int,
        a4: int,
        a5: int,
        a6: int,
        a7: int,
        a8: int,
        a9: int,
        a10: int,
    ) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_iiiiiiiiiii(
                self.store, index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10
            )
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_iiiiiiiiiiii(
        self,
        index: int,
        a1: int,
        a2: int,
        a3: int,
        a4: int,
        a5: int,
        a6: int,
        a7: int,
        a8: int,
        a9: int,
        a10: int,
        a11: int,
    ) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_iiiiiiiiiiii(
                self.store, index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11
            )
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_iiiiiiiiiiiii(
        self,
        index: int,
        a1: int,
        a2: int,
        a3: int,
        a4: int,
        a5: int,
        a6: int,
        a7: int,
        a8: int,
        a9: int,
        a10: int,
        a11: int,
        a12: int,
    ) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_iiiiiiiiiiiii(
                self.store, index, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12
            )
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def invoke_id(self, index: int, a1: int) -> Any:
        sp = self.stack_save()
        try:
            return self.dynCall_id(self.store, index, a1)
        except RuntimeError as e:
            self.stack_restore(sp)
            raise e

    def environ_sizes_get(self, penviron_count: int, penviron_buf_size: int) -> int:
        logger.debug(f"environ_sizes_get {penviron_count}, {penviron_buf_size}")
        return 0

    def environ_get(self, environ: int, environ_buf: int) -> int:
        logger.debug(f"environ_get {environ}, {environ_buf}")
        return 0

    def fd_write(self, fd: int, iov: int, iovcnt: int, pnum: int) -> int:
        num = 0

        for _ in range(iovcnt):
            ptr = int.from_bytes(self.memory.read(self.store, iov, iov + 4), "little")
            length = int.from_bytes(self.memory.read(self.store, iov + 4, iov + 4 + 4), "little")
            iov += 8
            for j in range(length):
                byte = self.memory.read(self.store, ptr + j, ptr + j + 1)[0]
                if fd == 1:
                    print(chr(byte), end="")
                elif fd == 2:
                    print(chr(byte), end="", file=sys.stderr)
                else:
                    raise ValueError(f"Unknown file descriptor {fd}")
            num += length

        self.memory.write(self.store, num.to_bytes(4, "little"), pnum)

        return 0

    def fd_seek(self, fd: int, offset: int, whence: int, newoffset: int) -> int:
        # pylint: disable=unused-argument
        return 0

    def fd_close(self, fd: int) -> None:
        # pylint: disable=unused-argument
        logger.warning("fd_close called")

    def dynCall(self, sig: str, ptr: int, *args: Tuple[Any, ...]) -> Any:
        if args is None:
            args = []
        if not any(f"dynCall_{sig}" in item.name for item in self.module.exports):
            raise RuntimeError(f'{"dynCall_" + sig} is not defined, bad function pointer')
        return getattr(self, f"dynCall_{sig}")(self.store, ptr, *args)

    def dynCaller(self, sig: str, ptr: int) -> Callable[..., Any]:
        return lambda *args: self.dynCall(sig, ptr, *args)

    def read_latin_1_string(self, ptr: int, max_bytes: int = -1) -> str:
        chars = []
        c = ptr
        while True:
            char = self.memory.read(self.store, c, c + 1)
            if char == b"\x00" or (c >= ptr + max_bytes and max_bytes != -1):
                break
            chars.append(char.decode())
            c += 1
        return "".join(chars)

    def heap_32_vector_to_array(self, arg_count: int, raw_arg_types_addr: int) -> list[int]:
        array = [0] * arg_count
        read = self.memory.read
        for i in range(arg_count):
            start = raw_arg_types_addr + i * 4
            array[i] = int.from_bytes(read(self.store, start, start + 4), "little")
        return array

    def get_function_name(self, name: str) -> str:
        signature: str = name.strip()
        if (args_index := signature.find("(")) != -1:
            if signature[len(signature) - 1] == ")":
                raise RuntimeError("Parentheses issue in function name found")
            return signature[0:args_index]

        return signature

    def embind_require_function(self, signature: int, raw_function: int) -> Callable:
        signature_translated = self.read_latin_1_string(signature)

        def make_dyn_caller() -> Callable:
            return self.dynCaller(signature_translated, raw_function)

        return make_dyn_caller()

    def expose_public_symbol(self, name: str, value: Any, num_arguments: int) -> None:
        setattr(self, name, value)
        if num_arguments is not None:
            getattr(self, name).num_arguments = num_arguments

    def replace_public_symbol(self, name: str, value: Any, num_arguments: int) -> None:
        if not hasattr(self, name):
            raise RuntimeError(f"symbol {name} not found for replacement")

        setattr(self, name, value)
        getattr(self, name).argCount = num_arguments

    def when_dependent_types_are_resolved(
        self, my_types: list[int], dependent_types: list[int], get_type_converters: Callable
    ) -> None:
        for my_type in my_types:
            self.type_dependencies[my_type] = dependent_types

        def on_complete(type_converters_in: Any) -> None:
            my_type_converters = get_type_converters(type_converters_in)
            if len(my_type_converters) != len(my_types):
                raise TypeError("Mismatched type converter count")
            for index, type_item in enumerate(my_types):
                self.register_type(type_item, my_type_converters[index], {})

        type_converters = [0] * len(dependent_types)
        unregistered_types = []
        registered = 0
        for i, dt in enumerate(dependent_types):
            if (dt_id := f"{dt}") in self.registered_types:
                type_converters[i] = self.registered_types[dt_id]
            else:
                unregistered_types.append(dt_id)
                if dt_id not in self.awaiting_dependencies:
                    self.awaiting_dependencies[dt_id] = []

                def aw_dep(i_in: int = i, dt_id_in: str = dt_id) -> None:
                    nonlocal registered
                    type_converters[i_in] = self.registered_types[dt_id_in]
                    registered += 1
                    if registered == len(unregistered_types):
                        on_complete(type_converters)

                self.awaiting_dependencies[dt_id].append(aw_dep)
        if len(unregistered_types) == 0:
            on_complete(type_converters)

    def run_destructors(self, destructors: list[Callable]) -> None:
        while len(destructors) > 0:
            ptr = destructors.pop(0)
            des = destructors.pop(0)
            des(ptr)

    def craft_invoker_function(
        self,
        human_name: str,
        arg_types: list[Any],
        class_type: Any,
        cpp_invoker_func: Callable,
        cpp_target_func: int,
        is_async: int,
    ) -> Callable:
        # pylint: disable=unused-argument
        if (arg_count := len(arg_types)) < 2:
            raise RuntimeError("argTypes array size mismatch")
        is_class_method_func = arg_types[1] is not None and class_type is not None
        needs_destructor_stack = False  # TBD
        returns = arg_types[0]["name"] != "void"
        closure_args = [
            human_name,
            lambda: print("BINDING ERROR"),
            cpp_invoker_func,
            cpp_target_func,
            self.run_destructors,
            arg_types[0],
            arg_types[1],
        ]
        for i in range(arg_count - 2):
            closure_args.append(arg_types[i + 2])

        closure_args.append({"iam": "ASYNCIFY"})

        if not needs_destructor_stack:
            for i in range(1 if is_class_method_func else 2, len(arg_types)):
                if arg_types[i]["destructorFunction"] is not None:
                    closure_args.append(arg_types[i]["destructorFunction"])

        def executor0(
            invoker: Callable = cpp_invoker_func,
            invoke_fcn: int = cpp_target_func,
            f_args: list[Any] = arg_types,
            is_r: bool = returns,
        ) -> Any:
            rv = invoker(invoke_fcn)
            if is_r:
                ret = f_args[0]["fromWireType"](rv)
                return ret
            return None

        def executor1(
            arg0: Any,
            invoker: Callable = cpp_invoker_func,
            invoke_fcn: int = cpp_target_func,
            f_args: list[Any] = arg_types,
            is_r: bool = returns,
        ) -> Any:
            arg_0_wired = f_args[2]["toWireType"](None, arg0)
            rv = invoker(invoke_fcn, arg_0_wired)
            if is_r:
                ret = f_args[0]["fromWireType"](rv)
                return ret
            return None

        def executor2(
            arg0: Any,
            arg1: Any,
            invoker: Callable = cpp_invoker_func,
            invoke_fcn: int = cpp_target_func,
            f_args: list[Any] = arg_types,
            is_r: bool = returns,
        ) -> Any:
            arg_0_wired = f_args[2]["toWireType"](None, arg0)
            arg_1_wired = f_args[3]["toWireType"](None, arg1)
            rv = invoker(invoke_fcn, arg_0_wired, arg_1_wired)
            if is_r:
                ret = f_args[0]["fromWireType"](rv)
                return ret
            return None

        def executor3(
            arg0: Any,
            arg1: Any,
            arg2: Any,
            invoker: Callable = cpp_invoker_func,
            invoke_fcn: int = cpp_target_func,
            f_args: list[Any] = arg_types,
            is_r: bool = returns,
        ) -> Any:
            arg_0_wired = f_args[2]["toWireType"](None, arg0)
            arg_1_wired = f_args[3]["toWireType"](None, arg1)
            arg_2_wired = f_args[4]["toWireType"](None, arg2)
            rv = invoker(invoke_fcn, arg_0_wired, arg_1_wired, arg_2_wired)
            if is_r:
                ret = f_args[0]["fromWireType"](rv)
                return ret
            return None

        if arg_count - 2 == 0:
            return lambda: executor0(cpp_invoker_func, cpp_target_func, arg_types)
        if arg_count - 2 == 1:
            return lambda *args: executor1(args[0], cpp_invoker_func, cpp_target_func, arg_types)
        if arg_count - 2 == 2:
            return lambda *args: executor2(
                args[0], args[1], cpp_invoker_func, cpp_target_func, arg_types
            )
        if arg_count - 2 == 3:
            return lambda *args: executor3(
                args[0], args[1], args[2], cpp_invoker_func, cpp_target_func, arg_types
            )
        raise RuntimeError(f"arg_count={arg_count} not implemented in executor")

    def shared_register_type(
        self, raw_type: int, registered_instance: Any, options: dict[str, Any]
    ) -> None:
        rt_id = f"{raw_type}"
        name = registered_instance["name"]
        if raw_type <= 0:
            raise RuntimeError(f"type {name} must have positive pointer")
        if rt_id in self.registered_types:
            if (
                "ignoreDuplicateRegistrations" in options
                and options["ignoreDuplicateRegistrations"]
            ):
                return
            print("duplicate register check?")
        self.registered_types[rt_id] = registered_instance
        if raw_type in self.type_dependencies:
            del self.type_dependencies[raw_type]
        if rt_id in self.awaiting_dependencies:
            callbacks = self.awaiting_dependencies[rt_id]
            del self.awaiting_dependencies[rt_id]
            for cb in callbacks:
                cb()

    def register_type(self, raw_type: int, registered_instance: Any, options: Any = None) -> None:
        if options is None:
            options = {}
        if not isinstance(registered_instance, dict):
            raise ValueError("registered_instance must be a dict")
        if "argPackAdvance" not in registered_instance:
            raise RuntimeError("Registered instance does not have argPackAdvance")
        self.shared_register_type(raw_type, registered_instance, options)

    def get_string_or_symbol(self, address: int) -> str:
        if (addr_id := f"{address}") in self.emval_symbols:
            return self.emval_symbols[addr_id]

        return self.read_latin_1_string(address)

    def _emval_get_global(self, name: int) -> Any:
        if name == 0:
            return self.emval_to_handle("irl")
        name_translated = self.get_string_or_symbol(name)
        return self.emval_to_handle(getattr(self, name_translated))

    def require_registered_type(self, raw_type: int, human_name: str) -> Any:
        # pylint: disable=unused-argument
        impl = self.registered_types[f"{raw_type}"]
        # check if impl exists in types
        return impl

    def _emval_take_value(self, value_type: int, arg: Any) -> int:
        value_obj = self.require_registered_type(value_type, "_em_take_value")
        v = value_obj["readValueFromPointer"](arg)
        return self.emval_to_handle(v)

    def emval_lookup_types(self, arg_count: int, arg_types: int) -> list[Any]:
        a = [0] * arg_count
        for i in range(arg_count):
            ptr = arg_types + i * 4
            lookup_type = int.from_bytes(self.memory.read(self.store, ptr, ptr + 4), "little")
            a[i] = self.require_registered_type(lookup_type, f"parameter {i}")
        return a

    def emval_return_value(self, return_type: Any, destructors_ref: int, handle: Any) -> int:
        destructors: list[int] = []
        result = return_type["toWireType"](destructors, handle)
        if len(destructors) > 0:
            self.memory.write(
                self.store, self.emval_to_handle(destructors).to_bytes(4, "little"), destructors_ref
            )
        # todo(mkelnar) void callbacks still requires f64 return value? issue in code or wasmtime behaviour?
        return result if result is not None else 0

    def _emval_get_method_caller(self, arg_count: int, arg_types: int, kind: int) -> int:
        logger.debug(f"emval get_method_caller: {arg_count} {arg_types} {kind}")
        val_types: list[Any] = self.emval_lookup_types(arg_count, arg_types)
        ret_type = val_types.pop(0)
        arg_count -= 1

        fcn_args: list[Any] = [ret_type]

        for i in range(arg_count):
            fcn_args.append(val_types[i])

        # if ret_type["isVoid"]:
        #     args.append(self.emval_return_value)

        def x_executor0(
            obj: Any, func: Callable, destructors_ref: int, args: int, r_type: int = ret_type
        ) -> Any:
            # pylint: disable=unused-argument
            rv = func()
            erv = self.emval_return_value(r_type, destructors_ref, rv)
            return erv

        def x_executor1(
            obj: Any,
            func: Callable,
            destructors_ref: int,
            args: int,
            f_args: Optional[list[Any]] = None,
        ) -> Any:
            # pylint: disable=unused-argument
            if f_args is None:
                f_args = fcn_args
            arg0 = f_args[1]["readValueFromPointer"](args)
            rv = func(arg0)
            return self.emval_return_value(f_args[0], destructors_ref, rv)

        def x_executor2(
            obj: Any,
            func: Callable,
            destructors_ref: int,
            args: int,
            f_args: Optional[list[Any]] = None,
        ) -> Any:
            # pylint: disable=unused-argument
            if f_args is None:
                f_args = fcn_args
            arg0 = f_args[1]["readValueFromPointer"](args)
            offset = f_args[1]["argPackAdvance"]
            arg1 = f_args[2]["readValueFromPointer"](args + offset)
            rv = func(arg0, arg1)
            return self.emval_return_value(f_args[0], destructors_ref, rv)

        def x_executor3(
            obj: Any,
            func: Callable,
            destructors_ref: int,
            args: int,
            f_args: Optional[list[Any]] = None,
        ) -> Any:
            # pylint: disable=unused-argument
            if f_args is None:
                f_args = fcn_args
            arg0 = f_args[1]["readValueFromPointer"](args)
            offset = f_args[1]["argPackAdvance"]
            arg1 = f_args[2]["readValueFromPointer"](args + offset)
            offset += f_args[2]["argPackAdvance"]
            arg2 = f_args[3]["readValueFromPointer"](args + offset)
            rv = func(arg0, arg1, arg2)
            return self.emval_return_value(f_args[0], destructors_ref, rv)

        def emval_add_method_caller(caller: Callable) -> int:
            caller_id = len(self.emval_method_callers)
            self.emval_method_callers.append(caller)
            return caller_id

        logger.debug(
            f"methodCaller<( {','.join([t['name'] for t in val_types])}) => {ret_type['name']}>"
        )

        if arg_count == 0:

            def x_caller_0(obj: Any, func: Callable, destructors_ref: int, args: Any) -> Any:
                return x_executor0(obj, func, destructors_ref, args)

            return emval_add_method_caller(x_caller_0)
        if arg_count == 1:

            def x_caller_1(obj: Any, func: Callable, destructors_ref: int, args: Any) -> Any:
                return x_executor1(obj, func, destructors_ref, args)

            return emval_add_method_caller(x_caller_1)
        if arg_count == 2:

            def x_caller_2(obj: Any, func: Callable, destructors_ref: int, args: Any) -> Any:
                return x_executor2(obj, func, destructors_ref, args)

            return emval_add_method_caller(x_caller_2)
        if arg_count == 3:

            def x_caller_3(obj: Any, func: Callable, destructors_ref: int, args: Any) -> Any:
                return x_executor3(obj, func, destructors_ref, args)

            return emval_add_method_caller(x_caller_3)

        raise RuntimeError(f"arg_count={arg_count} not implemented in _emval_get_method_caller")

    def _emval_as(self, handle: int, return_type: int, destructors_ref: int) -> float:
        handle = self.emval_to_value(handle)
        rt = self.require_registered_type(return_type, "emval::as")
        # to float because of API
        return self.emval_return_value(rt, destructors_ref, handle) * 1.0

    def _emval_call(self, caller: int, handle: int, destructors_ref: int, args: int) -> float:
        caller_fcn = self.emval_method_callers[caller]
        handle = self.emval_to_value(handle)
        cvr = caller_fcn(None, handle, destructors_ref, args)
        # to float because of API
        return 1.0 * cvr

    def _emval_call_method(
        self, caller: int, handle: int, method_name: int, destructors_ref: int, args: int
    ) -> float:
        method_name_translated = self.get_string_or_symbol(method_name)
        caller_fcn = self.emval_method_callers[caller]
        handle_value = self.emval_to_value(handle)
        rv = caller_fcn(handle, handle_value[method_name_translated], destructors_ref, args)
        # to float because of API
        return rv * 1.0

    def _emval_await(self, promise: int) -> Any:
        return promise  # simple await bypass until api will be ready, self.emval_to_value(promise) otherwise in future

    def _emval_new_cstring(self, value: int) -> Any:
        handle = self.get_string_or_symbol(value)
        return self.emval_to_handle(handle)

    def _emval_get_property(self, handle: int, key: int) -> int:
        handle_val = self.emval_to_value(handle)
        key_val = self.emval_to_value(key)
        return self.emval_to_handle(handle_val[key_val])

    def _emval_get_module_property(self, name: int) -> int:
        name_val = self.get_string_or_symbol(name)
        rv = self.emval_to_handle(self[name_val])
        return rv

    def _embind_register_void(self, raw_type: int, name: int) -> None:
        name_val = self.read_latin_1_string(name)
        self.register_type(
            raw_type,
            {
                "isVoid": True,
                "name": name_val,
                "argPackAdvance": 0,
                "fromWireType": lambda: None,
                "toWireType": lambda destructors, o: None,
            },
        )

    def _embind_register_bool(
        self, raw_type: int, name: int, true_value: int, false_value: int
    ) -> None:
        name_val = self.read_latin_1_string(name)
        logger.debug(f"emval register_bool: {raw_type} {name_val}")
        self.register_type(
            raw_type,
            {
                "name": name_val,
                "fromWireType": bool,
                "toWireType": lambda destructors, o: true_value if o else false_value,
                "argPackAdvance": 8,
                "readValueFromPointer": lambda ptr, rt=raw_type: self.registered_types[f"{rt}"][
                    "fromWireType"
                ](int.from_bytes(self.memory.read(self.store, ptr, ptr + 1), "little")),
                "destructorFunction": lambda: None,
            },
        )

    def integer_read_value_from_pointer(self, ptr: int, size: int, min_range: int) -> int:
        # pylint: disable=unused-argument
        if size == 1:
            return int.from_bytes(self.memory.read(self.store, ptr, ptr + 1), "little")
        if size == 2:
            return int.from_bytes(self.memory.read(self.store, ptr, ptr + 2), "little")
        if size == 4:
            return int.from_bytes(self.memory.read(self.store, ptr, ptr + 4), "little")
        raise ValueError(f"invalid integer width ({size}): {ptr}")

    def float_read_value_from_pointer(self, ptr: int, size: int) -> None:
        raise NotImplementedError("not implemented fload_read_value_from_pointer")

    def _embind_register_integer(
        self, primitive_type: int, name: int, size: int, min_range: int, max_range: int
    ) -> None:
        name_val = self.read_latin_1_string(name)
        logger.debug(f"emval register_integer: {name_val}")
        if max_range == -1:
            max_range = 4294967295

        def from_wire_type(value: int) -> int:
            if min_range == 0:
                bit_shift = 32 - 8 * size
                return value << bit_shift
            return value

        def to_wire_type(destructors: list[int], value: int) -> int:
            # pylint: disable=unused-argument
            return value

        self.register_type(
            primitive_type,
            {
                "name": name_val,
                "fromWireType": from_wire_type,
                "toWireType": to_wire_type,
                "argPackAdvance": 8,
                "readValueFromPointer": lambda ptr, mr=min_range: self.integer_read_value_from_pointer(
                    ptr, 4, mr
                ),
                "destructorFunction": lambda: None,
            },
        )

    def _embind_register_bigint(
        self,
        primitive_type: int,
        name: int,
        size: int,
        min_range: int,
        max_range: int,
        a: int,
        b: int,
    ) -> None:
        # pylint: disable=unused-argument
        name_val = self.read_latin_1_string(name)
        logger.debug(f"emval register_bigint: {name_val}")

    def _embind_register_float(self, primitive_type: int, name: int, size: int) -> None:
        # pylint: disable=unused-argument
        name_val = self.read_latin_1_string(name)
        logger.debug(f"emval register_float: {name_val}")
        self.register_type(
            primitive_type,
            {
                "name": name_val,
                "fromWireType": lambda value: value,
                "toWireType": lambda destructors, value: value,
                "argPackAdvance": 8,
                "readValueFromPointer": self.float_read_value_from_pointer,
                "destructorFunction": lambda: None,
            },
        )

    def _embind_register_emval(self, raw_type: int) -> None:
        logger.debug(f"emval register_emval: {raw_type}")
        self.register_type(raw_type, self.EmValType)

    def utf8_array_to_string(self, ptr: int, max_bytes: int) -> str:
        # todo(mkelnar) implement UTF8 decoder
        return self.read_latin_1_string(ptr, max_bytes)

    def string_to_utf8_array(self, str_data: str, out_ptr: int, max_bytes_to_write: int) -> int:
        if not max_bytes_to_write > 0:
            logger.warning("exceeds max bytes to write for std::string")
            return 0
        start = out_ptr
        # end = start + max_bytes_to_write - 1
        # todo(mkelnar) add true utf8 encoding here
        for char in str_data:
            c = ord(char)
            self.memory.write(self.store, c.to_bytes(1, "little"), out_ptr)
            out_ptr += 1

        val = 0
        self.memory.write(self.store, val.to_bytes(1, "little"), out_ptr)
        return out_ptr - start

    def utf8_to_string(self, ptr: int, max_bytes: int) -> str:
        return self.utf8_array_to_string(ptr, max_bytes) if ptr else ""

    def string_to_utf8(self, str_data: str, out_ptr: int, max_bytes_to_write: int) -> int:
        return self.string_to_utf8_array(str_data, out_ptr, max_bytes_to_write)

    def length_bytes_utf16(self, str_data: str) -> int:
        # pylint: disable=unused-argument
        return 1

    def length_bytes_utf32(self, str_data: str) -> int:
        # pylint: disable=unused-argument
        return 1

    def utf16_to_string(self) -> None:
        raise NotImplementedError("not implemented utf16_to_string")

    def string_to_utf16(self, str_data: str, out_ptr: int, max_bytes_to_write: int) -> None:
        raise NotImplementedError("not implemented string_to_utf16")

    def utf32_to_string(self) -> None:
        raise NotImplementedError("not implemented utf32_to_string")

    def string_to_utf32(self, str_data: str, out_ptr: int, max_bytes_to_write: int) -> None:
        raise NotImplementedError("not implemented string_to_utf32")

    def _embind_register_std_string(self, raw_type: int, name: int) -> None:
        name_val = self.read_latin_1_string(name)
        logger.debug(f"emval register_std_string: {name_val}")
        std_string_is_utf8 = name_val == "std::string"

        def from_wire_type(value: int) -> str:
            length = int.from_bytes(self.memory.read(self.store, value, value + 4), "little")
            payload = value + 4
            raw_data = self.memory.read(self.store, payload, payload + length)
            str_data = []
            if std_string_is_utf8:
                decode_start_ptr = 0
                for i in range(length + 1):
                    current_byte = raw_data[i : i + 1]
                    if i == length or current_byte == b"\x00":
                        string_segment = raw_data[decode_start_ptr:i].decode("utf-8")
                        str_data.append(string_segment)
                        decode_start_ptr = i + 1
            else:
                raise NotImplementedError("non std::string strings not implemented")
            self.exports["free"](self.store, value)
            return "".join(str_data)

        def to_wire_type(destructors: int, o: str) -> int:
            # pylint: disable=unused-argument
            value = f"{o}"
            length = len(value)
            base = self.exports["malloc"](self.store, 4 + length + 1)
            ptr = base + 4

            self.memory.write(self.store, length.to_bytes(4, "little"), base)

            if std_string_is_utf8:
                self.string_to_utf8(value, ptr, length + 1)
            else:
                raise NotImplementedError("simple string write not supported yet")

            # todo(mkelnar) destructor handling for string is not working at this time, suppressed for now
            # if destructors is not None:
            #     destructors.push(lambda b=base: self.exports["free"](self.store, b))
            return base

        self.register_type(
            raw_type,
            {
                "name": name_val,
                "fromWireType": from_wire_type,
                "toWireType": to_wire_type,
                "argPackAdvance": 8,
                "readValueFromPointer": lambda ptr, rt=raw_type: self.registered_types[f"{rt}"][
                    "fromWireType"
                ](int.from_bytes(self.memory.read(self.store, ptr, ptr + 4), "little")),
                "destructorFunction": lambda ptr: self.exports["free"](self.store, ptr),
            },
        )

    def _embind_register_std_wstring(self, raw_type: int, char_size: int, name: int) -> None:
        # pylint: disable=unused-argument
        name_val = self.read_latin_1_string(name)
        logger.debug(f"emval register_std_wstring: {name_val}")
        # decode_string = self.utf16_to_string
        # encode_string = self.string_to_utf16
        # length_bytes_utf = self.length_bytes_utf16
        # read_char_at = lambda ptr: int.from_bytes(self.memory.read(self.store, ptr, ptr + 2), 'little')
        # if char_size == 4:
        #     decode_string = self.utf32_to_string
        #     encode_string = self.string_to_utf32
        #     length_bytes_utf = self.length_bytes_utf32
        #     read_char_at = lambda ptr: int.from_bytes(self.memory.read(self.store, ptr, ptr + 4), 'little')

        self.register_type(
            raw_type,
            {
                "name": name_val,
                "fromWireType": lambda value: "PPP",
                "toWireType": lambda value: "UU",
                "argPackAdvance": 8,
                "readValueFromPointer": lambda ptr, del_type=raw_type: self.registered_types[
                    f"{del_type}"
                ]["fromWireType"](
                    int.from_bytes(self.memory.read(self.store, ptr, ptr + 4), "little")
                ),
                "destructorFunction": lambda ptr: self.exports["free"](self.store, ptr),
            },
        )

    def _embind_register_value_object(
        self,
        raw_type: int,
        name: int,
        constructor_signature: int,
        raw_constructor: int,
        destructor_signature: int,
        raw_destructor: int,
    ) -> None:
        logger.debug(f"emval register_value_object: {name}")
        rt_id = f"{raw_type}"
        self.struct_registrations[rt_id] = {
            "name": self.read_latin_1_string(name),
            "rawConstructor": self.embind_require_function(constructor_signature, raw_constructor),
            "rawDestructor": self.embind_require_function(destructor_signature, raw_destructor),
            "fields": [],
        }

    def _embind_register_value_object_field(
        self,
        struct_type: int,
        field_name: int,
        getter_return_type: int,
        getter_signature: int,
        getter: int,
        getter_context: int,
        setter_argument_type: int,
        setter_signature: int,
        setter: int,
        setter_context: int,
    ) -> None:
        logger.debug(f"emval register_value_object_field: {struct_type}.{field_name}")
        rt_id = f"{struct_type}"
        self.struct_registrations[rt_id]["fields"].append(
            {
                "fieldName": self.read_latin_1_string(field_name),
                "getterReturnType": getter_return_type,
                "getter": self.embind_require_function(getter_signature, getter),
                "getterContext": getter_context,
                "setterArgumentType": setter_argument_type,
                "setter": self.embind_require_function(setter_signature, setter),
                "setterContext": setter_context,
            }
        )

    def _embind_finalize_value_object(self, struct_type: int) -> None:
        logger.debug(f"emval finalize_value_object: {struct_type}")
        rt_id = f"{struct_type}"
        reg = self.struct_registrations[rt_id]
        del self.struct_registrations[rt_id]
        raw_constructor = reg["rawConstructor"]
        raw_destructor = reg["rawDestructor"]
        field_records = reg["fields"]
        field_types = [field["getterReturnType"] for field in field_records] + [
            field["setterArgumentType"] for field in field_records
        ]

        def proc_arg_types(proc_types: list[Any]) -> Any:
            fields: dict[str, dict[str, Callable]] = {}
            for i, field in enumerate(field_records):
                field_name = field["fieldName"]
                getter_return_type = proc_types[i]
                getter = field["getter"]
                getter_context = field["getterContext"]
                setter_argument_type = proc_types[i + len(field_records)]
                setter = field["setter"]
                setter_context = field["setterContext"]

                def read(
                    ptr: int,
                    grt: Any = getter_return_type,
                    gt: Callable = getter,
                    gt_ctx: Any = getter_context,
                ) -> Any:
                    return grt["fromWireType"](gt(gt_ctx, ptr))

                def write(
                    ptr: int,
                    o: Any,
                    st: Callable = setter,
                    st_ctx: Any = setter_context,
                    st_arg: Any = setter_argument_type,
                ) -> None:
                    destructors: list[Callable] = []
                    st(st_ctx, ptr, st_arg["toWireType"](destructors, o))

                fields[field_name] = {"read": read, "write": write}

            def from_wire_type(ptr: int, dtor: Callable = raw_destructor) -> Any:
                rv: dict[str, Any] = {}
                for fi, item_field in fields.items():
                    rv[fi] = item_field["read"](ptr)
                dtor(ptr)
                return rv

            def to_wire_type(
                destructors: list[int],
                o: Any,
                ctor: Callable = raw_constructor,
                dtor: int = raw_destructor,
            ) -> int:
                for fn in fields:
                    if fn not in o:
                        raise ValueError(f"Missing field: {fn}")
                ptr = ctor()
                for fn, item_field in fields.items():
                    item_field["write"](ptr, o[fn])

                if destructors is not None:
                    destructors.append(dtor)
                    destructors.append(ptr)
                return ptr

            def read_pointer(ptr: int) -> None:
                # pylint: disable=unused-argument
                raise NotImplementedError("not implemented read_pointer")

            return [
                {
                    "name": reg["name"],
                    "fromWireType": from_wire_type,
                    "toWireType": to_wire_type,
                    "argPackAdvance": 8,
                    "readValueFromPointer": read_pointer,
                    "destructorFunction": raw_destructor,
                }
            ]

        self.when_dependent_types_are_resolved([struct_type], field_types, proc_arg_types)

    def _embind_register_memory_view(self, raw_type: int, data_type_index: int, name: int) -> None:
        name_val = self.read_latin_1_string(name)
        logger.debug(f"emval register_memory_view: {name_val}:{data_type_index}")
        type_mapping = [
            "b",  # Int8
            Uint8Array,  # Uint8
            "h",  # Int16
            "H",  # Uint16
            Int32Array,  # Int32
            "I",  # Uint32
            "f",  # float32
            "d",  # float64
            "q",  # Int64
            "Q",  # Uint64
        ]
        ta = cast(Callable, type_mapping[data_type_index])

        def decode_memory_view(handle: int) -> Union[Uint8Array, Int32Array]:
            if ta not in {Uint8Array, Int32Array}:
                raise NotImplementedError("Not implemented memory decoder")
            sub = self.memory.read(self.store, handle, handle + 8)
            size = int.from_bytes(sub[0:4], "little")
            data = int.from_bytes(sub[4:8], "little")
            result = ta(self.HEAPU8, data, size)
            return result

        self.register_type(
            raw_type,
            {
                "name": name_val,
                "fromWireType": decode_memory_view,
                "argPackAdvance": 8,
                "readValueFromPointer": decode_memory_view,
            },
            {"ignoreDuplicateRegistrations": True},
        )

    def _embind_register_function(
        self,
        name: int,
        arg_count: int,
        raw_arg_types_addr: int,
        signature: int,
        raw_invoker: int,
        fn: int,
        is_async: int,
    ) -> None:
        arg_types = self.heap_32_vector_to_array(arg_count, raw_arg_types_addr)
        name_val = self.read_latin_1_string(name)
        name_val = self.get_function_name(name_val)

        logger.debug(
            f"embind register_function: {name_val}, arg_count: {arg_count}, async: {is_async}"
        )

        invoker = self.embind_require_function(signature, raw_invoker)
        self.expose_public_symbol(
            name_val, lambda: print(f"Cannot call {name_val} due to unbound types"), arg_count - 1
        )

        def get_arg_types(arg_types_list: list[Any]) -> list[Any]:
            invoker_args_array = [arg_types_list[0], None] + arg_types_list[1:]
            self.replace_public_symbol(
                name_val,
                self.craft_invoker_function(
                    name_val, invoker_args_array, None, invoker, fn, is_async
                ),
                arg_count - 1,
            )
            return []

        self.when_dependent_types_are_resolved([], arg_types, get_arg_types)

    def stack_alloc(self, ptr: int) -> int:
        return self.exports["stackAlloc"](self.store, ptr)

    def stack_save(self) -> int:
        if self.with_stack_control:
            return self._stack_save(self.store)
        return 0

    def stack_restore(self, stack: int) -> None:
        if self.with_stack_control:
            self._stack_restore(self.store, stack)

    def with_stack_save(self, fcn_handler: Callable) -> Any:
        stack = self.stack_save()
        rv = fcn_handler()
        self.stack_restore(stack)
        return rv

    def get_exception_message_common(self, ptr: int) -> str:
        type_addr_addr = self.stack_alloc(4)
        message_addr_addr = self.stack_alloc(4)
        self.exports["__get_exception_message"](self.store, ptr, type_addr_addr, message_addr_addr)
        type_addr = int.from_bytes(
            self.memory.read(self.store, type_addr_addr, type_addr_addr + 4), "little"
        )
        message_addr = int.from_bytes(
            self.memory.read(self.store, message_addr_addr, message_addr_addr + 4), "little"
        )
        ex_type = self.utf8_to_string(type_addr, 500)
        message = ex_type
        if message_addr:
            message = self.utf8_to_string(message_addr, 500)
        return message

    def get_exception_message(self, ptr: int) -> str:
        def fcn_caller() -> str:
            return self.get_exception_message_common(ptr)

        return self.with_stack_save(fcn_caller)

    def __cxa_throw(self, ptr: int, ex_type: int, destructor: int) -> None:
        exc_info = WasmExceptionInfo(ptr, self.memory, self.store)
        exc_info.init(ex_type, destructor)
        exc_data = self.get_exception_message(ptr)
        raise RuntimeError(f"{str(exc_data)}")

    def __cxa_begin_catch(self, ptr: int) -> None:
        # pylint: disable=unused-argument
        raise NotImplementedError("Not implemented __cxa_begin_catch")

    def __cxa_end_catch(self) -> None:
        raise NotImplementedError("Not implemented __cxa_end_catch")

    def __cxa_rethrow(self) -> None:
        raise NotImplementedError("Not implemented __cxa_rethrow")

    def __assert_fail(self, condition: int, filename: int, line: int, func: int) -> None:
        # pylint: disable=unused-argument
        raise NotImplementedError("Not implemented __assert_fail")

    def __cxa_find_matching_catch_2(self) -> None:
        raise NotImplementedError("Not implemented __cxa_find_matching_catch_2")

    def __cxa_find_matching_catch_3(self, arg0: int) -> None:
        # pylint: disable=unused-argument
        raise NotImplementedError("Not implemented __cxa_find_matching_catch_3")

    def __cxa_uncaught_exceptions(self) -> None:
        raise NotImplementedError("Not implemented __cxa_uncaught_exceptions")

    def __resume_exception(self, ptr: int) -> None:
        # pylint: disable=unused-argument
        raise NotImplementedError("Not implemented __resumeException")

    def _emval_run_destructors(self, handle: int) -> None:
        # pylint: disable=unused-argument
        raise NotImplementedError("Not implemented _emval_run_destructors")

    def abort(self) -> None:
        raise RuntimeError("native code called abort()")

    def emscripten_memcpy_js(self, dest: int, src: int, num: int) -> None:
        # pylint: disable=unused-argument
        raise NotImplementedError("Not implemented emscripten_memcpy_js")

    def emscripten_resize_heap(self, size: int) -> None:
        # pylint: disable=unused-argument
        raise NotImplementedError("Not implemented emscripten_resize_heap")

    def emscripten_sleep(self, ms: int) -> None:
        sleep(ms / 1000)

    def strftime_l(self, s: int, maxsize: int, fmt: int, tm: int, loc: int) -> None:
        # pylint: disable=unused-argument
        raise NotImplementedError("Not implemented strftime_l")

    def construct_imports(self) -> list[Func]:
        import_array = []

        wasm_imports: dict[str, Callable] = {
            "__assert_fail": self.__assert_fail,
            "__cxa_begin_catch": self.__cxa_begin_catch,
            "__cxa_end_catch": self.__cxa_end_catch,
            "__cxa_find_matching_catch_2": self.__cxa_find_matching_catch_2,
            "__cxa_find_matching_catch_3": self.__cxa_find_matching_catch_3,
            "__cxa_rethrow": self.__cxa_rethrow,
            "__cxa_throw": self.__cxa_throw,
            "__cxa_uncaught_exceptions": self.__cxa_uncaught_exceptions,
            "__resumeException": self.__resume_exception,
            "_embind_finalize_value_object": self._embind_finalize_value_object,
            "_embind_register_bigint": self._embind_register_bigint,
            "_embind_register_bool": self._embind_register_bool,
            "_embind_register_emval": self._embind_register_emval,
            "_embind_register_float": self._embind_register_float,
            "_embind_register_function": self._embind_register_function,
            "_embind_register_integer": self._embind_register_integer,
            "_embind_register_memory_view": self._embind_register_memory_view,
            "_embind_register_std_string": self._embind_register_std_string,
            "_embind_register_std_wstring": self._embind_register_std_wstring,
            "_embind_register_value_object": self._embind_register_value_object,
            "_embind_register_value_object_field": self._embind_register_value_object_field,
            "_embind_register_void": self._embind_register_void,
            "_emval_as": self._emval_as,
            "_emval_await": self._emval_await,
            "_emval_call": self._emval_call,
            "_emval_call_method": self._emval_call_method,
            "_emval_decref": self._emval_decref,
            "_emval_get_global": self._emval_get_global,
            "_emval_get_method_caller": self._emval_get_method_caller,
            "_emval_get_module_property": self._emval_get_module_property,
            "_emval_get_property": self._emval_get_property,
            "_emval_incref": self._emval_incref,
            "_emval_new_cstring": self._emval_new_cstring,
            "_emval_run_destructors": self._emval_run_destructors,
            "_emval_take_value": self._emval_take_value,
            "abort": self.abort,
            "emscripten_sleep": self.emscripten_sleep,
            "emscripten_memcpy_js": self.emscripten_memcpy_js,
            "emscripten_resize_heap": self.emscripten_resize_heap,
            "environ_get": self.environ_get,
            "environ_sizes_get": self.environ_sizes_get,
            "fd_close": self.fd_close,
            "fd_seek": self.fd_seek,
            "fd_write": self.fd_write,
            "invoke_diii": self.invoke_diii,
            "invoke_fiii": self.invoke_fiii,
            "invoke_id": self.invoke_id,
            "invoke_i": self.invoke_i,
            "invoke_ii": self.invoke_ii,
            "invoke_iii": self.invoke_iii,
            "invoke_iiii": self.invoke_iiii,
            "invoke_iiiii": self.invoke_iiiii,
            "invoke_iiiiii": self.invoke_iiiiii,
            "invoke_iiiiiii": self.invoke_iiiiiii,
            "invoke_iiiiiiii": self.invoke_iiiiiiii,
            "invoke_iiiiiiiii": self.invoke_iiiiiiiii,
            "invoke_iiiiiiiiii": self.invoke_iiiiiiiiii,
            "invoke_iiiiiiiiiii": self.invoke_iiiiiiiiiii,
            "invoke_iiiiiiiiiiii": self.invoke_iiiiiiiiiiii,
            "invoke_iiiiiiiiiiiii": self.invoke_iiiiiiiiiiiii,
            "invoke_jiiii": self.invoke_jiiii,
            "invoke_v": self.invoke_v,
            "invoke_vi": self.invoke_vi,
            "invoke_vid": self.invoke_vid,
            "invoke_vii": self.invoke_vii,
            "invoke_viii": self.invoke_viii,
            "invoke_viiii": self.invoke_viiii,
            "invoke_viiiii": self.invoke_viiiii,
            "invoke_viiiiii": self.invoke_viiiiii,
            "invoke_viiiiiii": self.invoke_viiiiiii,
            "invoke_viiiiiiii": self.invoke_viiiiiiii,
            "invoke_viiiiiiiii": self.invoke_viiiiiiiii,
            "invoke_viiiiiiiiii": self.invoke_viiiiiiiiii,
            "invoke_viiiiiiiiiii": self.invoke_viiiiiiiiiii,
            "invoke_viiiiiiiiiiii": self.invoke_viiiiiiiiiiii,
            "invoke_viiiiiiiiiiiii": self.invoke_viiiiiiiiiiiii,
            "invoke_viiiiiiiiiiiiii": self.invoke_viiiiiiiiiiiiii,
            "invoke_viiiiiiiiiiiiiii": self.invoke_viiiiiiiiiiiiiii,
            "strftime_l": self.strftime_l,
        }
        for imp in self.module.imports:
            if (name := imp.name) in wasm_imports:
                import_array.append(Func(self.store, cast(FuncType, imp.type), wasm_imports[name]))
            else:
                raise RuntimeError(f"{str(name)} not found in wasm imports")
        return import_array

    def register_handler(self, fcn_handler: Callable) -> None:
        setattr(self, fcn_handler.__name__, types.MethodType(fcn_handler, self))
