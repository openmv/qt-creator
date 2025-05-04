"""
We should be able to decode values without creating excessive copies in memory
"""

import pytest

import x690.types as t
from x690 import decode


@pytest.mark.parametrize(
    "data, index, expected, expected_nxt",
    [
        (b"padding\x02\x01\x01padding", 7, t.Integer(1), 10),
        (b"padding\x04\x03foopadding", 7, t.OctetString(b"foo"), 12),
        (
            b"pa\x00dding\x04\x80foo\x00\x00padding",
            8,
            t.OctetString(b"foo"),
            15,
        ),
    ],
)
def test_offset_decoding(data, index, expected, expected_nxt):
    """
    To work with non-copied data, we need to be able to decode at any point
    in the stream
    """
    value, next_tlv = decode(data, index)
    assert value == expected
    assert next_tlv == expected_nxt
