# Type-Hinting is done in a stub file
"""
Overview
========

This module contains the encoding/decoding logic for data types as defined in
:term:`X.690`.

Each type is made available via a registry dictionary on :py:class:`~.X690Type` and
can be retrieved via :py:meth:`~.X690Type.get`.

Additionally, given a :py:class:`bytes` object, the :py:func:`~.decode`
function can be used to parse the bytes object and return a typed instance
from it. See :py:func:`~.decode` for details about it's behaviour!

.. note::
    The individual type classes in this module do not contain any additional
    documentation. The bulk of this module is documented in :py:class:`~.X690Type`.

    For the rest, the type classes simply define the type identifier tag.

Supporting Additional Classes
=============================

Just by subclassing :py:class:`~.X690Type` and setting correct ``TAG`` and
``TYPECLASS`` values, most of the basic functionality will be covered by the
superclass. X690Type detection, and addition to the registry is automatic.
Subclassing is enough.

By default, a new type which does not override any methods will have it's value
reported as bytes objects. You may want to override at least
:py:meth:`~.X690Type.decode_raw` to convert the raw-bytes into your own data-type.

Example
-------

Let's assume you want to decode/encode a "Person" object with a first-name,
last-name and age. Let's also assume it will be an application-specific type of
a "constructed" nature with our application-local tag 1. Let's further assume
that the value will be a UTF-8 encoded JSON string inside the x690 stream.

We specify the metadata as class-level variables ``TYPECLASS``, ``NATURE`` and
``TAG``. The decoding is handled by implementing a static-method
``decode_raw`` which gets the data-object containing the value and a slice
defining at which position the data is located. The encoding is handled by
implementing the instance-method ``encode_raw``. The instance contains the
Python value in ``self.pyvalue``.

So we can implement this as follows (including a named-tuple as our local
type):

.. code-block:: python

    from typing import NamedTuple
    from x690.types import X690Type
    from json import loads, dumps

    class Person(NamedTuple):
        first_name: str
        last_name: str
        age: int

    class PersonType(X690Type[Person]):
        TYPECLASS = TypeClass.APPLICATION
        NATURE = [TypeNature.CONSTRUCTED]
        TAG = 1

        @staticmethod
        def decode_raw(data: bytes, slc: slice = slice(None)) -> Person:
            values = loads(data[slc].decode("utf8"))
            return Person(
                values["first_name"], values["last_name"], values["age"]
            )

        def encode_raw(self) -> bytes:
            return dumps(self.pyvalue._asdict()).encode("utf8")


"""
# pylint: disable=abstract-method, missing-class-docstring, too-few-public-methods


from datetime import datetime
from itertools import zip_longest
from textwrap import indent
from typing import (
    Any,
    Dict,
    Generic,
    Iterator,
    List,
    Optional,
    Tuple,
    Type,
    TypeVar,
    Union,
    overload,
)

import t61codec  # type: ignore

from .exc import IncompleteDecoding, UnexpectedType, X690Error
from .util import (
    INDENT_STRING,
    TypeClass,
    TypeInfo,
    TypeNature,
    encode_length,
    get_value_slice,
    visible_octets,
    wrap,
)

TWrappedPyType = TypeVar("TWrappedPyType", bound=Any)
TPopType = TypeVar("TPopType", bound=Any)
TConcreteType = TypeVar("TConcreteType", bound="X690Type[Any]")


class _SENTINEL_UNINITIALISED:  # pylint: disable=invalid-name
    """
    Helper for specific sentinel values
    """


#: sentinel value for uninitialised objects (used for lazy decoding)
UNINITIALISED = _SENTINEL_UNINITIALISED()


def decode(
    data: bytes,
    start_index: int = 0,
    enforce_type: Optional[Type[TPopType]] = None,
    strict: bool = False,
) -> Tuple[TPopType, int]:
    """
    Convert a X.690 bytes object into a Python instance, and the location of
    the next object.

    Given a :py:class:`bytes` object and any start-index, inspects and parses
    the octets starting at the given index (as many as required) to determine
    variable type (and corresponding Python class), and length. That class is
    then used to parse the object located in ``data`` at the given index. The
    location of the start of the next (subsequent) object is also determined.

    The return value is a tuple with the decoded object and the start-index
    of the next object.

    Example::

        >>> data = b'\\x02\\x01\\x05\\x11'
        >>> decode(data)
        (Integer(5), 3)
        >>> data = b'some-skippable-bytes\\x02\\x01\\x05\\x11'
        >>> decode(data, 20)
        (Integer(5), 23)
    """
    if start_index >= len(data):
        raise IndexError(
            f"Attempting to read from position {start_index} "
            f"on data with length {len(data)}"
        )

    start_index = start_index or 0
    type_ = TypeInfo.from_bytes(data[start_index])

    try:
        cls = X690Type.get(type_.cls, type_.tag, type_.nature)
    except KeyError:
        cls = UnknownType

    data_slice, next_tlv = get_value_slice(data, start_index)
    output = cls.from_bytes(data, data_slice)
    if cls is UnknownType:
        output.tag = data[start_index]  # type: ignore

    if enforce_type and not isinstance(output, enforce_type):
        raise UnexpectedType(
            f"Unexpected decode result. Expected instance of type "
            f"{enforce_type} but got {type(output)} instead"
        )

    if strict and next_tlv < len(data) - 1:
        remainder = data[next_tlv:]
        raise IncompleteDecoding(
            f"Strict decoding still had {len(remainder)} remaining bytes!",
            remainder=remainder,
        )

    return output, next_tlv  # type: ignore


class X690Type(Generic[TWrappedPyType]):
    """
    The superclass for all supported types.
    """

    __slots__ = ["pyvalue", "_raw_bytes"]
    __registry: Dict[Tuple[str, int, TypeNature], Type["X690Type[Any]"]] = {}

    #: The x690 type-class (universal, application or context)
    TYPECLASS: TypeClass = TypeClass.UNIVERSAL

    #: The x690 "private/constructed" information
    NATURE = [TypeNature.CONSTRUCTED]

    #: The x690 identifier for the type
    TAG: int = -1

    #: The decoded (or to-be encoded) Python value
    pyvalue: Union[TWrappedPyType, _SENTINEL_UNINITIALISED]

    #: The byte representation of "pyvalue" without metadata-header
    _raw_bytes: bytes

    #: The location of the value within "raw_bytes"
    bounds: slice = slice(None)

    def __init_subclass__(cls: Type["X690Type[Any]"]) -> None:
        for nature in cls.NATURE:
            X690Type.__registry[(cls.TYPECLASS, cls.TAG, nature)] = cls

    @property
    def value(self) -> TWrappedPyType:
        """
        Returns the value as a pure Python type
        """
        if not isinstance(self.pyvalue, _SENTINEL_UNINITIALISED):
            return self.pyvalue
        return self.decode_raw(self.raw_bytes, self.bounds)

    @staticmethod
    def decode_raw(data: bytes, slc: slice = slice(None)) -> TWrappedPyType:
        """
        Converts the raw byte-value (without type & length header) into a
        pure Python type

        >>> Integer.decode_raw(b"\\x05")
        5

        :param data: A data-block containing the byte-information
        :param slc: A slice of the data-block that contains the exact
            raw-bytes.
        :return: The value that should be wrapped by the current x690 type.
        """
        return data[slc]  # type: ignore

    @staticmethod
    def get(
        typeclass: str, typeid: int, nature: TypeNature = TypeNature.CONSTRUCTED
    ) -> Type["X690Type[Any]"]:
        """
        Retrieve a Python class by x690 type information

        Classes can be registered by subclassing :py:class:`x690.types.X690Type`
        """
        cls = X690Type.__registry[(typeclass, typeid, nature)]
        return cls

    @staticmethod
    def all() -> List[Type["X690Type[Any]"]]:
        """
        Returns all registered classes
        """
        return list(X690Type.__registry.values())

    @classmethod
    def validate(cls, data: bytes) -> None:
        """
        Given a bytes object, checks if the given class *cls* supports decoding
        this object. If not, raises a ValueError.
        """
        tinfo = TypeInfo.from_bytes(data[0])
        if tinfo.cls != cls.TYPECLASS or tinfo.tag != cls.TAG:
            raise ValueError(
                "Invalid type header! "
                "Expected a %s class with tag "
                "ID 0x%02x, but got a %s class with "
                "tag ID 0x%02x" % (cls.TYPECLASS, cls.TAG, tinfo.cls, data[0])
            )

    @classmethod
    def decode(
        cls: Type[TConcreteType], data: bytes
    ) -> TConcreteType:  # pragma: no cover
        """
        This method takes a bytes object which contains the raw content octets
        of the object. That means, the octets *without* the type information
        and length.

        This function must be overridden by the concrete subclasses.
        """
        slc = get_value_slice(data).bounds
        output = cls.decode_raw(data, slc)
        return cls(output)

    @classmethod
    def from_bytes(
        cls: Type[TConcreteType], data: bytes, slc: slice = slice(None)
    ) -> TConcreteType:
        """
        Creates a new :py:class:`x690.types.X690Type` instance from raw-bytes
        (without type nor length bytes)

        >>> Integer.from_bytes(b"\\x01")
        Integer(1)
        >>> OctetString.from_bytes(b"hello-world")
        OctetString(b'hello-world')
        >>> Boolean.from_bytes(b"\\x00")
        Boolean(False)
        """
        try:
            instance = cls()
        except TypeError as exc:
            raise X690Error(
                "Custom types must have a no-arg constructor allowing "
                "x690.types.UNINITIALISED as value. Custom type %r does not "
                "support this!" % cls
            ) from exc
        instance.raw_bytes = data
        instance.bounds = slc
        return instance

    def __init__(
        self,
        value: Union[TWrappedPyType, _SENTINEL_UNINITIALISED] = UNINITIALISED,
    ) -> None:
        self.pyvalue = value
        self._raw_bytes = b""

    @property
    def raw_bytes(self) -> bytes:
        if self._raw_bytes != b"":
            return self._raw_bytes
        if self.pyvalue is UNINITIALISED:
            return b""
        self._raw_bytes = self.encode_raw()
        return self._raw_bytes

    @raw_bytes.setter
    def raw_bytes(self, value: bytes) -> None:
        self._raw_bytes = value

    def __bytes__(self) -> bytes:  # pragma: no cover
        """
        Convert this instance into a bytes object. This must be implemented by
        subclasses.
        """
        value = self.raw_bytes[self.bounds] or self.encode_raw()
        tinfo = TypeInfo(self.TYPECLASS, self.NATURE[0], self.TAG)
        return bytes(tinfo) + encode_length(len(value)) + value

    def __repr__(self) -> str:
        repr_value = repr(self.value)
        return "%s(%s)" % (self.__class__.__name__, repr_value)

    @property
    def length(self) -> int:
        """
        Return the x690 byte-length of this instance
        """
        return len(self.raw_bytes[self.bounds])

    def encode_raw(self) -> bytes:
        """
        Convert this instance into raw x690 bytes (excluding the type and
        length header)

        >>> import x690.types as t
        >>> Integer(5).encode_raw()
        b'\\x05'
        >>> Boolean(True).encode_raw()
        b'\\x01'
        >>> X690Type(t.UNINITIALISED).encode_raw()
        b''
        """
        if isinstance(self.pyvalue, _SENTINEL_UNINITIALISED):
            return b""
        return self.pyvalue

    def pythonize(self) -> TWrappedPyType:
        """
        Convert this instance to an appropriate pure Python object.
        """
        return self.value

    def pretty(self, depth: int = 0) -> str:  # pragma: no cover
        """
        Returns a readable representation (possibly multiline) of the value.

        The value is indented by *depth* levels of indentation

        By default this simply returns the string representation. But more
        complex values may override this.
        """
        return indent(str(self), INDENT_STRING * depth)


class UnknownType(X690Type[bytes]):
    """
    A fallback type for anything not in X.690.

    Instances of this class contain the raw information as parsed from the
    bytes as the following attributes:

    * ``value``: The value without leading metadata (as bytes value)
    * ``tag``: The *unparsed* "tag". This is the type ID as defined in the
      reference document. See :py:class:`~puresnmp.x690.util.TypeInfo` for
      details.
    * ``typeinfo``: unused (derived from *tag* and only here for consistency
      with ``__repr__`` of this class).
    """

    TAG = 0x99

    def __init__(self, value: bytes = b"", tag: int = -1) -> None:
        super().__init__(value or UNINITIALISED)
        self.tag = tag

    def __repr__(self) -> str:
        typeinfo = TypeInfo.from_bytes(self.tag)
        tinfo = f"{typeinfo.cls}/{typeinfo.nature}/{typeinfo.tag}"
        return f"<{self.__class__.__name__} {self.tag} {self.value!r} {tinfo}>"

    def __eq__(self, other: object) -> bool:
        return (
            isinstance(other, UnknownType)
            and self.value == other.value
            and self.tag == other.tag
        )

    def pretty(self, depth: int = 0) -> str:
        """
        Returns a prettified string with *depth* levels of indentation

        See :py:meth:`~.X690Type.pretty`
        """
        wrapped = wrap(
            visible_octets(self.value), str(type(self)), depth
        ).splitlines()
        if len(wrapped) > 15:
            line_width = len(wrapped[0])
            sniptext = ("<%d more lines>" % (len(wrapped) - 10 - 5)).center(
                line_width - 2
            )
            wrapped = wrapped[:10] + ["┊%s┊" % sniptext] + wrapped[-5:]
        typeinfo = TypeInfo.from_bytes(self.tag)
        lines = [
            "Unknown X690Type",
            f"  │ Tag:       {self.tag}",
            "  │ X690Type Info:",
            f"  │  │ Class: {typeinfo.cls}",
            f"  │  │ Nature: {typeinfo.nature}",
            f"  │  │ Tag: {typeinfo.tag}",
        ] + wrapped
        return indent(
            "\n".join(lines),
            INDENT_STRING * depth,
        )


class Boolean(X690Type[bool]):
    TAG = 0x01
    NATURE = [TypeNature.PRIMITIVE]

    @staticmethod
    def decode_raw(data: bytes, slc: slice = slice(None)) -> bool:
        """
        Converts the raw byte-value (without type & length header) into a
        pure Python type

        Overrides :py:meth:`~.X690Type.decode_raw`
        """
        return data[slc] != b"\x00"

    @classmethod
    def validate(cls, data: bytes) -> None:
        """
        Overrides :py:meth:`.X690Type.validate`
        """
        super().validate(data)
        if data[1] != 1:
            raise ValueError(
                "Unexpected Boolean value. Length should be 1,"
                " it was %d" % data[1]
            )

    def encode_raw(self) -> bytes:
        """
        Overrides :py:meth:`.X690Type.encode_raw`
        """
        return b"\x01" if self.pyvalue else b"\x00"

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Boolean) and self.value == other.value


class Null(X690Type[None]):
    TAG = 0x05
    NATURE = [TypeNature.PRIMITIVE]

    @classmethod
    def validate(cls, data: bytes) -> None:
        """
        Overrides :py:meth:`.X690Type.validate`
        """
        super().validate(data)
        if data[1] != 0:
            raise ValueError(
                "Unexpected NULL value. Length should be 0, it "
                "was %d" % data[1]
            )

    @staticmethod
    def decode_raw(data: bytes, slc: slice = slice(None)) -> None:
        """
        Converts the raw byte-value (without type & length header) into a
        pure Python type

        Overrides :py:meth:`~.X690Type.decode_raw`
        """
        # pylint: disable=unused-argument
        return None

    def encode_raw(self) -> bytes:
        """
        Overrides :py:meth:`.X690Type.encode_raw`

        >>> Null().encode_raw()
        b'\\x00'
        """
        # pylint: disable=no-self-use
        return b"\x00"

    def __bytes__(self) -> bytes:
        return b"\x05\x00"

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Null) and self.value == other.value

    def __repr__(self) -> str:
        return "Null()"

    def __bool__(self) -> bool:
        return False

    def __nonzero__(self) -> bool:  # pragma: no cover
        return False


class OctetString(X690Type[bytes]):
    TAG = 0x04
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]

    def __init__(
        self, value: Union[str, bytes, _SENTINEL_UNINITIALISED] = b""
    ) -> None:
        if isinstance(value, str):
            value = value.encode("ascii")

        # The custom init allows us to pass in str instances instead of only
        # bytes. We still need to pass down "None" if need to detect
        # "not-yet-decoded" values
        if not value:
            value = UNINITIALISED

        super().__init__(value)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, OctetString) and self.value == other.value

    def pretty(self, depth: int = 0) -> str:
        """
        Returns a prettified string with *depth* levels of indentation

        See :py:meth:`~.X690Type.pretty`
        """
        if self.value == b"":
            return repr(self)
        try:
            # We try to decode embedded X.690 items. If we can't, we display
            # the value raw
            embedded: X690Type[Any] = decode(self.value)[0]
            return wrap(embedded.pretty(0), f"Embedded in {type(self)}", depth)
        except:  # pylint: disable=bare-except
            wrapped = wrap(visible_octets(self.value), str(type(self)), depth)
            return wrapped


class Sequence(X690Type[List[X690Type[Any]]]):
    """
    Represents an X.690 sequence type. Instances of this class are iterable and
    indexable.
    """

    TAG = 0x10

    @staticmethod
    def decode_raw(
        data: bytes, slc: slice = slice(None)
    ) -> List[X690Type[Any]]:
        """
        Converts the raw byte-value (without type & length header) into a
        pure Python type

        Overrides :py:meth:`~.X690Type.decode_raw`
        """
        start_index = slc.start or 0
        if not data[slc] or start_index > len(data):
            return []
        item: X690Type[Any]
        item, next_pos = decode(data, start_index)
        items: List[X690Type[Any]] = [item]
        end = slc.stop or len(data)
        while next_pos < end:
            item, next_pos = decode(data, next_pos)
            items.append(item)
        return items

    def encode_raw(self) -> bytes:
        """
        Overrides :py:meth:`.X690Type.encode_raw`
        """
        if isinstance(self.pyvalue, _SENTINEL_UNINITIALISED):
            return b""
        items = [bytes(item) for item in self.pyvalue]
        output = b"".join(items)
        return output

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, Sequence):
            return False
        return self.raw_bytes[self.bounds] == other.raw_bytes[other.bounds]

    def __repr__(self) -> str:
        item_repr = list(self)
        return "Sequence(%r)" % item_repr

    def __len__(self) -> int:
        return len(self.value)

    def __iter__(self) -> Iterator[X690Type[Any]]:
        yield from self.value

    def __getitem__(self, idx: int) -> X690Type[Any]:
        return self.value[idx]

    def pythonize(self) -> List[X690Type[Any]]:
        """
        Overrides :py:meth:`~.X690Type.pythonize`
        """
        return [obj.pythonize() for obj in self]

    def pretty(self, depth: int = 0) -> str:  # pragma: no cover
        """
        Returns a prettified string with *depth* levels of indentation

        See :py:meth:`~.X690Type.pretty`
        """
        lines = [f"{self.__class__.__name__} with {len(self.value)} items:"]
        for item in self.value:
            prettified_item = item.pretty(depth)
            bullet = INDENT_STRING * depth + "⁃ "
            for line in prettified_item.splitlines():
                lines.append(bullet + line)
                bullet = "  "
        return "\n".join(lines)


class Integer(X690Type[int]):
    SIGNED = True
    TAG = 0x02
    NATURE = [TypeNature.PRIMITIVE]

    @classmethod
    def decode_raw(cls, data: bytes, slc: slice = slice(None)) -> int:
        """
        Converts the raw byte-value (without type & length header) into a
        pure Python type

        Overrides :py:meth:`~.X690Type.decode_raw`
        """
        data = data[slc]
        return int.from_bytes(data, "big", signed=cls.SIGNED)

    def encode_raw(self) -> bytes:
        """
        Overrides :py:meth:`.X690Type.encode_raw`
        """
        if isinstance(self.pyvalue, _SENTINEL_UNINITIALISED):
            return b""
        octets = [self.pyvalue & 0b11111111]

        # Append remaining octets for long integers.
        remainder = self.pyvalue
        while remainder not in (0, -1):
            remainder = remainder >> 8
            octets.append(remainder & 0b11111111)

        if remainder == 0 and octets[-1] == 0b10000000:
            octets.append(0)
        octets.reverse()

        # remove leading octet if there is a string of 9 zeros or ones
        while len(octets) > 1 and (
            (octets[0] == 0 and octets[1] & 0b10000000 == 0)
            or (octets[0] == 0b11111111 and octets[1] & 0b10000000 != 0)
        ):
            del octets[0]
        return bytes(octets)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, Integer) and self.value == other.value


class ObjectIdentifier(X690Type[str]):
    """
    Represents an OID.

    Instances of this class support containment checks to determine if one OID
    is a sub-item of another::

        >>> ObjectIdentifier("1.2.3.4.5") in ObjectIdentifier("1.2.3")
        True

        >>> ObjectIdentifier("1.2.4.5.6") in ObjectIdentifier("1.2.3")
        False
    """

    TAG = 0x06
    NATURE = [TypeNature.PRIMITIVE]

    def __init__(
        self, value: Union[str, _SENTINEL_UNINITIALISED] = UNINITIALISED
    ) -> None:
        if (
            not isinstance(value, _SENTINEL_UNINITIALISED)
            and value
            and value.startswith(".")
        ):
            value = value[1:]
        super().__init__(value)

    @property
    def nodes(self) -> Tuple[int, ...]:
        """
        Returns the numerical nodes for this instance as tuple

        >>> ObjectIdentifier("1.2.3").nodes
        (1, 2, 3)
        >>> ObjectIdentifier().nodes
        ()
        """
        if not self.value:
            return tuple()
        return tuple(int(n) for n in self.value.split("."))

    @staticmethod
    def decode_large_value(current_char: int, stream: Iterator[int]) -> int:

        """
        If we encounter a value larger than 127, we have to consume from the
        stram until we encounter a value below 127 and recombine them.

        See: https://msdn.microsoft.com/en-us/library/bb540809(v=vs.85).aspx
        """
        buffer = []
        while current_char > 127:
            buffer.append(current_char ^ 0b10000000)
            current_char = next(stream)
        total = current_char
        for i, digit in enumerate(reversed(buffer)):
            total += digit * 128 ** (i + 1)
        return total

    @staticmethod
    def encode_large_value(value: int) -> List[int]:
        """
        Inverse function of :py:meth:`~.ObjectIdentifier.decode_large_value`
        """
        if value <= 127:
            return [value]
        output = [value & 0b1111111]
        value = value >> 7
        while value:
            output.append(value & 0b1111111 | 0b10000000)
            value = value >> 7
        output.reverse()
        return output

    @staticmethod
    def decode_raw(data: bytes, slc: slice = slice(None)) -> str:
        """
        Converts the raw byte-value (without type & length header) into a
        pure Python type

        Overrides :py:meth:`~.X690Type.decode_raw`
        """
        # Special case for "empty" object identifiers which should be returned
        # as "0"
        data = data[slc]
        if not data:
            return ""

        # unpack the first byte into first and second sub-identifiers.
        data0 = data[0]
        first, second = data0 // 40, data0 % 40
        output = [first, second]

        remaining = iter(data[1:])

        for node in remaining:
            # Each node can only contain values from 0-127. Other values need
            # to be combined.
            if node > 127:
                collapsed_value = ObjectIdentifier.decode_large_value(
                    node, remaining
                )
                output.append(collapsed_value)
                continue
            output.append(node)

        instance = ".".join([str(n) for n in output])
        return instance

    def collapse_identifiers(self) -> Tuple[int, ...]:
        """
        Meld the first two octets into one octet as defined by x.690

        In x.690 ObjectIdentifiers are a sequence of numbers. In the
        byte-representation the first two of those numbers are stored in the
        first byte.

        This function takes a "human-readable" OID tuple and returns a new
        tuple with the first two elements merged (collapsed) together.

        >>> ObjectIdentifier("1.3.6.1.4.1").collapse_identifiers()
        (43, 6, 1, 4, 1)
        >>> ObjectIdentifier().collapse_identifiers()
        ()
        """
        # pylint: disable=no-self-use
        identifiers = self.nodes
        if len(identifiers) == 0:
            return tuple()

        if len(identifiers) > 1:
            # The first two bytes are collapsed according to X.690
            # See https://en.wikipedia.org/wiki/X.690#BER_encoding
            first, second, rest = (
                identifiers[0],
                identifiers[1],
                identifiers[2:],
            )
            first_output = (40 * first) + second
        else:
            first_output = identifiers[0]
            rest = tuple()

        # Values above 127 need a special encoding. They get split up into
        # multiple positions.
        exploded_high_values = []
        for char in rest:
            if char > 127:
                exploded_high_values.extend(
                    ObjectIdentifier.encode_large_value(char)
                )
            else:
                exploded_high_values.append(char)

        collapsed_identifiers = [first_output]
        for subidentifier in rest:
            collapsed_identifiers.extend(
                ObjectIdentifier.encode_large_value(subidentifier)
            )
        return tuple(collapsed_identifiers)

    def encode_raw(self) -> bytes:
        """
        Overrides :py:meth:`.X690Type.encode_raw`
        """
        if isinstance(self.pyvalue, _SENTINEL_UNINITIALISED):
            return b""
        collapsed_identifiers = self.collapse_identifiers()
        if collapsed_identifiers == ():
            return b""
        try:
            output = bytes(collapsed_identifiers)
        except ValueError as exc:
            raise ValueError(
                "Unable to collapse %r. First two octets are too large!"
                % (self.nodes,)
            ) from exc
        return output

    def __int__(self) -> int:
        nodes = self.nodes
        if len(nodes) != 1:
            raise ValueError(
                "Only ObjectIdentifier with one node can be "
                "converted to int. %r is not convertable. It has %d nodes."
                % (self, len(self))
            )
        return nodes[0]

    def __str__(self) -> str:
        return self.value

    def __repr__(self) -> str:
        return "ObjectIdentifier(%r)" % (self.value,)

    def __eq__(self, other: object) -> bool:
        return isinstance(other, ObjectIdentifier) and self.value == other.value

    def __len__(self) -> int:
        return len(self.nodes)

    def __contains__(self, other: "ObjectIdentifier") -> bool:
        """
        Check if one OID is a child of another.

        TODO: This has been written in the middle of the night! It's messy...
        """
        # pylint: disable=invalid-name

        a, b = other.nodes, self.nodes

        # if both have the same amount of identifiers, check for equality
        if len(a) == len(b):
            return a == b

        # if "self" is longer than "other", self cannot be "in" other
        if len(b) > len(a):
            return False

        # For all other cases:
        #   1. zero-fill
        #   2. drop identical items from the front (leaving us with "tail")
        #   3. compare both tails
        zipped = zip_longest(a, b, fillvalue=None)
        tail: List[Tuple[int, int]] = []
        for tmp_a, tmp_b in zipped:
            if tmp_a == tmp_b and not tail:
                continue
            tail.append((tmp_a, tmp_b))

        # if we only have Nones in "b", we know that "a" was longer and that it
        # is a direct subtree of "b" (no diverging nodes). Otherwise we would
        # have te divergence in "b", and we can say that "b is contained in a"
        _, unzipped_b = zip(*tail)
        if all([x is None for x in unzipped_b]):
            return True

        # In all other cases we end up with an unmatching tail and know that "b
        # is not contained in a".
        return False

    def __lt__(self, other: "ObjectIdentifier") -> bool:
        return self.nodes < other.nodes

    def __hash__(self) -> int:
        return hash(self.value)

    def __add__(self, other: "ObjectIdentifier") -> "ObjectIdentifier":
        nodes = ".".join([self.value, other.value])
        return ObjectIdentifier(nodes)

    @overload
    def __getitem__(self, index: int) -> int:  # pragma: no cover
        ...

    @overload
    def __getitem__(
        self, index: slice
    ) -> "ObjectIdentifier":  # pragma: no cover
        ...

    def __getitem__(
        self, index: Union[int, slice]
    ) -> Union["ObjectIdentifier", int]:
        if isinstance(index, int):
            return self.nodes[index]
        output = self.nodes[index]
        return ObjectIdentifier(".".join([str(n) for n in output]))

    def parentof(self, other: "ObjectIdentifier") -> bool:
        """
        Convenience method to check whether this OID is a parent of another OID
        """
        return other in self

    def childof(self, other: "ObjectIdentifier") -> bool:
        """
        Convenience method to check whether this OID is a child of another OID
        """
        return self in other


class ObjectDescriptor(X690Type[str]):
    TAG = 0x07
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]


class External(X690Type[bytes]):
    TAG = 0x08


class Real(X690Type[float]):
    TAG = 0x09
    NATURE = [TypeNature.PRIMITIVE]


class Enumerated(X690Type[List[Any]]):
    TAG = 0x0A
    NATURE = [TypeNature.PRIMITIVE]


class EmbeddedPdv(X690Type[bytes]):
    TAG = 0x0B


class Utf8String(X690Type[str]):
    TAG = 0x0C
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]


class RelativeOid(X690Type[str]):
    TAG = 0x0D
    NATURE = [TypeNature.PRIMITIVE]


class Set(X690Type[bytes]):
    TAG = 0x11


class NumericString(X690Type[str]):
    TAG = 0x12
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]


class PrintableString(X690Type[str]):
    TAG = 0x13
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]


class T61String(X690Type[str]):
    TAG = 0x14
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]
    __INITIALISED = False

    def __init__(self, value: Union[str, bytes] = "") -> None:
        if isinstance(value, str):
            super().__init__(value or UNINITIALISED)
        else:
            super().__init__(T61String.decode_raw(value))

    def __eq__(self, other: object) -> bool:
        return isinstance(other, T61String) and self.value == other.value

    @staticmethod
    def decode_raw(data: bytes, slc: slice = slice(None, None)) -> str:
        """
        Converts the raw byte-value (without type & length header) into a
        pure Python type

        Overrides :py:meth:`~.X690Type.decode_raw`
        """
        data = data[slc]
        if not T61String.__INITIALISED:
            t61codec.register()
            T61String.__INITIALISED = True
        return data.decode("t61")

    def encode_raw(self) -> bytes:
        """
        Overrides :py:meth:`.X690Type.encode_raw`
        """
        if not T61String.__INITIALISED:  # pragma: no cover
            t61codec.register()
            T61String.__INITIALISED = True
        if isinstance(self.pyvalue, _SENTINEL_UNINITIALISED):
            return b""
        return self.pyvalue.encode("t61")


class VideotexString(X690Type[str]):
    TAG = 0x15
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]


class IA5String(X690Type[str]):
    TAG = 0x16
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]


class UtcTime(X690Type[datetime]):
    TAG = 0x17
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]


class GeneralizedTime(X690Type[datetime]):
    TAG = 0x18
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]


class GraphicString(X690Type[str]):
    # NOTE: As per x.690, this should inherit from OctetString. However, this
    #       library serves as an abstraction layer between X.690 and Python.
    #       For this reason, it defines this as a "str" type. To keep the
    #       correct behaviours, we can still "borrow" the implementation from
    #       OctetString if needed
    TAG = 0x19
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]

    @staticmethod
    def decode_raw(data: bytes, slc: slice = slice(None)) -> str:
        """
        Converts the raw byte-value (without type & length header) into a
        pure Python type

        Overrides :py:meth:`~.X690Type.decode_raw`
        """
        data = data[slc]
        return data.decode("ascii")


class VisibleString(X690Type[str]):
    TAG = 0x1A
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]


class GeneralString(X690Type[str]):
    TAG = 0x1B
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]


class UniversalString(X690Type[str]):
    TAG = 0x1C
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]


class CharacterString(X690Type[str]):
    TAG = 0x1D


class BmpString(X690Type[str]):
    TAG = 0x1E
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]


class EOC(X690Type[bytes]):
    TAG = 0x00
    NATURE = [TypeNature.PRIMITIVE]


class BitString(X690Type[str]):
    TAG = 0x03
    NATURE = [TypeNature.PRIMITIVE, TypeNature.CONSTRUCTED]
