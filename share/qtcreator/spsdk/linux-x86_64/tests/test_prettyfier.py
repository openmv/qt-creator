"""
This module covers prettyfication testing
"""
# pylint: disable=line-too-long
import sys

import pytest

from x690.types import Integer, OctetString, UnknownType, X690Type


@pytest.mark.parametrize("cls", X690Type.all())
def test_pretty(cls):
    """
    Calling "pretty" on classes should always return a string
    """
    result = cls().pretty()
    assert isinstance(result, str)


def test_pretty_octetstrings():
    """
    OctetStrings should display any wrapped/embedded value
    """
    embedded = Integer(10)
    data = OctetString(bytes(embedded))
    result = data.pretty()
    if sys.version_info < (3, 7):
        expected = (
            "┌────────────────────────────────────┐\n"
            "│ Embedded in x690.types.OctetString │\n"
            "├────────────────────────────────────┤\n"
            "│ Integer(10)                        │\n"
            "└────────────────────────────────────┘"
        )
    else:
        expected = (
            "┌──────────────────────────────────────────────┐\n"
            "│ Embedded in <class 'x690.types.OctetString'> │\n"
            "├──────────────────────────────────────────────┤\n"
            "│ Integer(10)                                  │\n"
            "└──────────────────────────────────────────────┘"
        )

    assert result == expected


def test_pretty_octetstrings_raw():
    """
    OctetStrings should display a "hexdump" for values
    """
    data = OctetString(b"hello-world")
    result = data.pretty()
    if sys.version_info < (3, 7):
        expected = (
            "┌────────────────────────────────────────────────────────────────┐\n"
            "│ x690.types.OctetString                                         │\n"
            "├────────────────────────────────────────────────────────────────┤\n"
            "│ 68 65 6c 6c 6f 2d 77 6f  72 6c 64                  hello-world │\n"
            "└────────────────────────────────────────────────────────────────┘"
        )
    else:
        expected = (
            "┌────────────────────────────────────────────────────────────────┐\n"
            "│ <class 'x690.types.OctetString'>                               │\n"
            "├────────────────────────────────────────────────────────────────┤\n"
            "│ 68 65 6c 6c 6f 2d 77 6f  72 6c 64                  hello-world │\n"
            "└────────────────────────────────────────────────────────────────┘"
        )
    assert result == expected


def test_long_bytes_snip():
    """
    If a value has large amounts of data, we want to cut if to avoid drowning
    out the console output
    """
    value = UnknownType(1000 * b"x")
    result = value.pretty()
    assert len(result.splitlines()) < 50
    assert "more lines" in result
