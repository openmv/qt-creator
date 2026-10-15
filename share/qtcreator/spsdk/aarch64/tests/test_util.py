# pylint: skip-file

from unittest import TestCase

import pytest

from x690.util import (
    Length,
    TypeClass,
    TypeInfo,
    TypeNature,
    decode_length,
    encode_length,
    get_value_slice,
    visible_octets,
    wrap,
)

from .conftest import assert_bytes_equal


class TestTypeInfoDecoding(TestCase):
    """
    Tests the various possible combinations for decoding type-hint octets into
    Python objects.
    """

    def test_from_bytes_raw_value(self):
        result = TypeInfo.from_bytes(0b00011110)._raw_value
        expected = 0b00011110
        self.assertEqual(result, expected)

    def test_from_bytes_a(self):
        result = TypeInfo.from_bytes(0b00011110)
        expected = TypeInfo(TypeClass.UNIVERSAL, TypeNature.PRIMITIVE, 0b11110)
        self.assertEqual(result, expected)

    def test_from_bytes_b(self):
        result = TypeInfo.from_bytes(0b00111110)
        expected = TypeInfo(
            TypeClass.UNIVERSAL, TypeNature.CONSTRUCTED, 0b11110
        )
        self.assertEqual(result, expected)

    def test_from_bytes_c(self):
        result = TypeInfo.from_bytes(0b01011110)
        expected = TypeInfo(
            TypeClass.APPLICATION, TypeNature.PRIMITIVE, 0b11110
        )
        self.assertEqual(result, expected)

    def test_from_bytes_d(self):
        result = TypeInfo.from_bytes(0b01111110)
        expected = TypeInfo(
            TypeClass.APPLICATION, TypeNature.CONSTRUCTED, 0b11110
        )
        self.assertEqual(result, expected)

    def test_from_bytes_e(self):
        result = TypeInfo.from_bytes(0b10011110)
        expected = TypeInfo(TypeClass.CONTEXT, TypeNature.PRIMITIVE, 0b11110)
        self.assertEqual(result, expected)

    def test_from_bytes_f(self):
        result = TypeInfo.from_bytes(0b10111110)
        expected = TypeInfo(TypeClass.CONTEXT, TypeNature.CONSTRUCTED, 0b11110)
        self.assertEqual(result, expected)

    def test_from_bytes_g(self):
        result = TypeInfo.from_bytes(0b11011110)
        expected = TypeInfo(TypeClass.PRIVATE, TypeNature.PRIMITIVE, 0b11110)
        self.assertEqual(result, expected)

    def test_from_bytes_h(self):
        result = TypeInfo.from_bytes(0b11111110)
        expected = TypeInfo(TypeClass.PRIVATE, TypeNature.CONSTRUCTED, 0b11110)
        self.assertEqual(result, expected)


class TestTypeInfoEncoding(TestCase):
    """
    Tests the various possible combinations for encoding type-hint instances
    into bytes.
    """

    def test_to_bytes_a(self):
        obj = TypeInfo(TypeClass.UNIVERSAL, TypeNature.PRIMITIVE, 0b11110)
        result = bytes(obj)
        expected = bytes([0b00011110])
        self.assertEqual(result, expected)

    def test_to_bytes_b(self):
        obj = TypeInfo(TypeClass.UNIVERSAL, TypeNature.CONSTRUCTED, 0b11110)
        result = bytes(obj)
        expected = bytes([0b00111110])
        self.assertEqual(result, expected)

    def test_to_bytes_c(self):
        obj = TypeInfo(TypeClass.APPLICATION, TypeNature.PRIMITIVE, 0b11110)
        result = bytes(obj)
        expected = bytes([0b01011110])
        self.assertEqual(result, expected)

    def test_to_bytes_d(self):
        obj = TypeInfo(TypeClass.APPLICATION, TypeNature.CONSTRUCTED, 0b11110)
        result = bytes(obj)
        expected = bytes([0b01111110])
        self.assertEqual(result, expected)

    def test_to_bytes_e(self):
        obj = TypeInfo(TypeClass.CONTEXT, TypeNature.PRIMITIVE, 0b11110)
        result = bytes(obj)
        expected = bytes([0b10011110])
        self.assertEqual(result, expected)

    def test_to_bytes_f(self):
        obj = TypeInfo(TypeClass.CONTEXT, TypeNature.CONSTRUCTED, 0b11110)
        result = bytes(obj)
        expected = bytes([0b10111110])
        self.assertEqual(result, expected)

    def test_to_bytes_g(self):
        obj = TypeInfo(TypeClass.PRIVATE, TypeNature.PRIMITIVE, 0b11110)
        result = bytes(obj)
        expected = bytes([0b11011110])
        self.assertEqual(result, expected)

    def test_to_bytes_h(self):
        obj = TypeInfo(TypeClass.PRIVATE, TypeNature.CONSTRUCTED, 0b11110)
        result = bytes(obj)
        expected = bytes([0b11111110])
        self.assertEqual(result, expected)


class TestTypeInfoClass(TestCase):
    """
    Tests various "implied" functionality of TypeInfo objects.
    """

    def test_equality(self):
        a = TypeInfo(TypeClass.UNIVERSAL, TypeNature.CONSTRUCTED, 0b11110)
        b = TypeInfo(TypeClass.UNIVERSAL, TypeNature.CONSTRUCTED, 0b11110)
        self.assertEqual(a, b)

    def test_inequality(self):
        a = TypeInfo(TypeClass.UNIVERSAL, TypeNature.CONSTRUCTED, 0b11110)
        b = TypeInfo(TypeClass.UNIVERSAL, TypeNature.CONSTRUCTED, 0b10110)
        self.assertNotEqual(a, b)

    def test_encoding_symmetry_a(self):
        """
        Encoding an object to bytes, and then decoding the resulting bytes
        should yield the same instance.
        """
        expected = TypeInfo(
            TypeClass.UNIVERSAL, TypeNature.CONSTRUCTED, 0b11110
        )
        result = TypeInfo.from_bytes(bytes(expected))
        self.assertEqual(result, expected)

    def test_dencoding_symmetry_b(self):
        """
        Decoding an object from bytes, and then encoding the resulting instance
        should yield the same bytes.
        """
        expected = bytes([0b11111110])
        result = bytes(TypeInfo.from_bytes(expected))
        self.assertEqual(result, expected)

    def test_impossible_class(self):
        instance = TypeInfo(10, 100, 1000)
        with self.assertRaisesRegex(ValueError, "class"):
            bytes(instance)

    def test_impossible_pc(self):
        instance = TypeInfo(TypeClass.APPLICATION, 100, 1000)
        with self.assertRaisesRegex(ValueError, "primitive/constructed"):
            bytes(instance)


class TestLengthOctets(TestCase):
    def test_encode_length_short(self):
        expected = bytes([0b00100110])
        result = encode_length(38)
        self.assertEqual(result, expected)

    def test_encode_length_long(self):
        expected = bytes([0b10000001, 0b11001001])
        result = encode_length(201)
        assert_bytes_equal(result, expected)

    def test_encode_length_longer(self):
        expected = bytes([0b10000010, 0b00000001, 0b00101110])
        result = encode_length(302)
        assert_bytes_equal(result, expected)

    def test_encode_length_longer_2(self):
        expected = bytes([0x81, 0xA4])
        result = encode_length(164)
        assert_bytes_equal(result, expected)

    def test_encode_length_indefinite(self):
        expected = bytes([0b10000000])
        result = encode_length(Length.INDEFINITE)
        assert_bytes_equal(result, expected)

    def test_identifier_long(self):
        with self.assertRaises(NotImplementedError):
            TypeInfo.from_bytes(0b11111111)
        self.skipTest("Not yet implemented")  # TODO implement

    def test_decode_length_at_index(self):
        data = b"foobar\x05"
        expected = 5
        result, offset = decode_length(data, index=6)
        self.assertEqual(result, expected)
        self.assertEqual(offset, 1)

    def test_decode_length_short(self):
        data = b"\x05"
        expected = 5
        result, offset = decode_length(data)
        self.assertEqual(result, expected)
        self.assertEqual(offset, 1)

    def test_decode_length_long(self):
        data = bytes([0b10000010, 0b00000001, 0b10110011])
        expected = 435
        result, offset = decode_length(data)
        self.assertEqual(result, expected)
        self.assertEqual(offset, 3)

    def test_decode_length_longer(self):
        data = bytes([0x81, 0xA4])
        expected = 164
        result, offset = decode_length(data)
        self.assertEqual(result, expected)
        self.assertEqual(offset, 2)

    def test_decode_length_indefinite(self):
        data = bytes([0x80])
        expected = -1
        result, offset = decode_length(data)
        self.assertEqual(result, expected)
        self.assertEqual(offset, -1)

    def test_decode_length_reserved(self):
        with self.assertRaises(NotImplementedError):
            decode_length(bytes([0b11111111]))


class TestHelpers(TestCase):
    def test_visible_octets_minimal(self):
        result = visible_octets(bytes([0b00000000, 0b01010101]))
        expected = "00 55                                              .U"
        self.assertEqual(result, expected)

    def test_visible_octets_double_space(self):
        """
        Test that we have a double space after 8 octets for better readability
        """
        result = visible_octets(
            bytes(
                [
                    0b00000000,
                    0b01010101,
                    0b00000000,
                    0b01010101,
                    0b00000000,
                    0b01010101,
                    0b00000000,
                    0b01010101,
                    0b01010101,
                ]
            )
        )
        expected = (
            "00 55 00 55 00 55 00 55  55                        " ".U.U.U.UU"
        )
        self.assertEqual(result, expected)

    def test_visible_octets_multiline(self):
        """
        If we have more than 16 octets, we need to go to a new line.
        """
        result = visible_octets(bytes([0b00000000, 0b01010101] * 9))
        expected = (
            "00 55 00 55 00 55 00 55  00 55 00 55 00 55 00 55   "
            ".U.U.U.U.U.U.U.U\n"
            "00 55                                              "
            ".U"
        )
        self.assertEqual(result, expected)


class TestGithubIssue23(TestCase):
    """
    In issue #23 a problem was raised that the byte-order was incorrect when
    encoding large values for "length" information.

    This test-case takes the value (435) from the X.690 Wikipedia page as
    reference and has indeed highlighted the error.
    """

    def test_encode(self):
        expected = bytes([0b10000010, 0b00000001, 0b10110011])
        result = encode_length(435)
        assert_bytes_equal(result, expected)

    def test_decode(self):
        data = bytes([0b10000010, 0b00000001, 0b10110011])
        expected = 435
        result, offset = decode_length(data)
        self.assertEqual(result, expected)
        self.assertEqual(offset, 3)

    def test_symmetry(self):
        result, _ = decode_length(encode_length(435))
        self.assertEqual(result, 435)


def test_wrap():
    """
    Ensures that wrapping debug text works as expected
    """
    result = wrap("Hello\nThis is a long line", "Title", 3)
    expected = (
        "      ┌─────────────────────┐\n"
        "      │ Title               │\n"
        "      ├─────────────────────┤\n"
        "      │ Hello               │\n"
        "      │ This is a long line │\n"
        "      └─────────────────────┘"
    )
    assert result == expected


@pytest.mark.parametrize(
    "data, slc, next_index",
    [
        (b"\x02\01\01", slice(2, 3), 3),
        (b"\x04\03xxx", slice(2, 5), 5),
        (b"\x04\x80xxx\x00\x00", slice(2, 5), 7),
    ],
)
def test_value_slice(data, slc, next_index):
    res_slc, res_next_index = get_value_slice(data)
    assert res_slc == slc
    assert res_next_index == next_index


@pytest.mark.parametrize(
    "data, slc, next_index",
    [
        (b"padding\x02\x01\x01end-padding", slice(9, 10), 10),
        (b"padding\x04\x80the-value\x00\x00end-padding", slice(9, 18), 20),
    ],
)
def test_value_slice_indexed(data, slc, next_index):
    """
    We should be able to fetch a value slice starting at a given index
    """
    res_slc, res_next_index = get_value_slice(data, 7)
    assert res_slc == slc
    assert data[next_index:] == b"end-padding"
