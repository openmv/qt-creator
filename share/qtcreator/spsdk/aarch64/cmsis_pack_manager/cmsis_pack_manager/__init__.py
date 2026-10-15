__all__ = ["lib", "ffi"]

import os
from .ffi import ffi

lib = ffi.dlopen(os.path.join(os.path.dirname(__file__), 'libcmsis_pack_manager.so'))
del os
