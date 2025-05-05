"""
x690 is a library allowing to decode and encode values according to the ITU
X.690 standard.

For decoding, use the function *decode* provided by this module. When used
as-is on a bytes object, it will return the first decoded value in the blob and
the position of the next value:

>>> from x690 import decode
>>> data, next_index = decode(b"\x02\01\01\x02\01\03")
>>> data
Integer(1)
>>> next_index
3

For a stream of data with an unknown number of items, you can use the
next-index to move over the data:

>>> from x690 import decode
>>> data = b"\\x02\\x01\\x01\\x02\\x01\\x02\\x02\\x01\\x03"
>>> item, next_index = decode(data)
>>> item, next_index
(Integer(1), 3)
>>> while next_index < len(data):
...     item, next_index = decode(data, next_index)
...     item, next_index
(Integer(2), 6)
(Integer(3), 9)

If you expect exactly one element, it is possible to pass the "strict"
argument. This will raise an error if the stream contains any "junk" data.

Finally, a x690 data-stream may contain various types. For this reason, the
"decode" function is type-hinted to return a vague, imprecise type. If you
*know* what your are decoding, you can pass the expected type into the
function:

>>> from x690 import decode
>>> from x690.types import Integer
>>> decode(b"\x02\x01\x01", enforce_type=Integer)
(Integer(1), 3)

This will do two things:

* It runs a type-check upon decoding and will raise a
  :py:exc:`x690.exc.UnexpectedType` error if it does not match.
* Inform the type-checker of the return-type, improving type-checker output.
"""

from .types import decode

try:
    import importlib.metadata as importlib_metadata
except ModuleNotFoundError:  # pragma: no cover
    import importlib_metadata  # type: ignore


__version__ = importlib_metadata.version("x690")
__all__ = [
    "__version__",
    "decode",
]
