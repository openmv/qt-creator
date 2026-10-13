# pylint: skip-file

import sys
from unittest import TestCase

import pytest

from x690.exc import IncompleteDecoding, UnexpectedType, X690Error
from x690.types import (
    Boolean,
    Integer,
    Null,
    ObjectIdentifier,
    OctetString,
    Sequence,
    T61String,
    UnknownType,
    X690Type,
    decode,
)
from x690.util import TypeInfo

from .conftest import assert_bytes_equal


class TestBoolean(TestCase):
    def test_encoding_false(self):
        value = Boolean(False)
        result = bytes(value)
        expected = b"\x01\x01\x00"
        assert_bytes_equal(result, expected)

    def test_encoding_true(self):
        value = Boolean(True)
        result = bytes(value)
        expected = b"\x01\x01\x01"
        assert_bytes_equal(result, expected)

    def test_decoding_false(self):
        result, _ = decode(b"\x01\x01\x00")
        expected = Boolean(False)
        self.assertEqual(result, expected)

    def test_decoding_true(self):
        result, _ = decode(b"\x01\x01\x01")
        expected = Boolean(True)
        self.assertEqual(result, expected)

        result, _ = decode(b"\x01\x01\x02")
        expected = Boolean(True)
        self.assertEqual(result, expected)

        result, _ = decode(b"\x01\x01\xff")
        expected = Boolean(True)
        self.assertEqual(result, expected)

    def test_pythonize(self):
        result = Boolean(True).pythonize()
        expected = True
        self.assertEqual(result, expected)

    def test_validate_too_long(self):
        """
        Validate what happens when there are too many bytes.
        """
        with self.assertRaisesRegex(ValueError, "Length"):
            Boolean.validate(b"\x01\x00\x00")


class TestObjectIdentifier(TestCase):
    def setUp(self):
        super().setUp()

    def test_uncollapsible_head(self):
        """
        OIDs need to be "collapsed" when converting to bytes. This requires the
        first two positions to be "small-enough"
        """
        with pytest.raises(ValueError) as exc:
            instance = ObjectIdentifier("200.200")
            bytes(instance)
        exc.match("Unable to collapse.*too large")

    def test_to_int(self):
        """
        An object-identifier of one node should be convertible to int
        """
        result = int(ObjectIdentifier("42"))
        assert result == 42

    def test_to_int_multiple(self):
        """
        An object-identifier of multiple node should not be convertible to int
        """
        with pytest.raises(ValueError) as exc:
            int(ObjectIdentifier("1.2.3"))
        exc.match("one node.*3 nodes")

    def test_raw_encoding_empty(self):
        """
        An empty object-identifier should be encodable
        """
        result = ObjectIdentifier().encode_raw()
        assert result == b""
        result = ObjectIdentifier(b"").encode_raw()
        assert result == b""

    def test_simple_encoding(self):
        """
        A simple OID with no identifier above 127
        """
        oid = ObjectIdentifier("1.3.6.1.2.1")
        result = bytes(oid)
        expected = b"\x06\x05\x2b\x06\x01\x02\x01"
        assert_bytes_equal(result, expected)

    def test_simple_decoding(self):
        """
        A simple OID with no identifier above 127
        """
        expected = ObjectIdentifier("1.3.6.1.2.1")
        result, _ = decode(b"\x06\x05\x2b\x06\x01\x02\x01")
        self.assertEqual(result, expected)

    def test_decoding_zero(self):
        """
        A simple OID with the top-level ID '0'
        """
        expected = ObjectIdentifier()
        result, _ = decode(b"\x06\x00")
        self.assertEqual(result, expected)

    def test_encoding_zero(self):
        """
        A simple OID with the top-level ID '0'
        """
        oid = ObjectIdentifier()
        result = bytes(oid)
        expected = b"\x06\x00"
        self.assertEqual(result, expected)

    def test_multibyte_encoding(self):
        """
        If a sub-identifier has a value bigger than 127, the encoding becomes a
        bit weird. The sub-identifiers are split into multiple sub-identifiers.
        """
        oid = ObjectIdentifier("1.3.6.8072")
        result = bytes(oid)
        expected = b"\x06\x04\x2b\x06\xbf\x08"
        assert_bytes_equal(result, expected)

    def test_multibyte_decoding(self):
        """
        If a sub-identifier has a value bigger than 127, the decoding becomes a
        bit weird. The sub-identifiers are split into multiple sub-identifiers.
        """
        expected = ObjectIdentifier("1.3.6.8072")
        result, _ = decode(b"\x06\x04\x2b\x06\xbf\x08")
        self.assertEqual(result, expected)

    def test_encode_large_value(self):
        """
        OID sub-identifiers larger than 127 must be split up.

        See https://en.wikipedia.org/wiki/Variable-length_quantity
        """
        result = ObjectIdentifier.encode_large_value(106903)
        expected = [0b10000110, 0b11000011, 0b00010111]
        self.assertEqual(result, expected)

    def test_fromstring(self):
        result = ObjectIdentifier("1.2.3")
        expected = ObjectIdentifier("1.2.3")
        self.assertEqual(result, expected)

    def test_fromstring_leading_dot(self):
        """
        A leading dot represents the "root" node. This should be allowed as
        string input.
        """
        result = ObjectIdentifier(".1.2.3")
        expected = ObjectIdentifier("1.2.3")
        self.assertEqual(result, expected)

    def test_pythonize(self):
        result = ObjectIdentifier("1.2.3").pythonize()
        expected = "1.2.3"
        self.assertEqual(result, expected)

    def test_str(self):
        result = str(ObjectIdentifier("1.2.3"))
        expected = "1.2.3"
        self.assertEqual(result, expected)

    def test_encode_root(self):
        result = bytes(ObjectIdentifier("1"))
        expected = b"\x06\x01\x01"
        assert_bytes_equal(result, expected)

    def test_containment_a(self):
        a = ObjectIdentifier("1.2.3.4")
        b = ObjectIdentifier("1.2.3")
        self.assertTrue(a in b)

    def test_containment_b(self):
        a = ObjectIdentifier("1.2.3.4")
        b = ObjectIdentifier("1.2.3.4")
        self.assertTrue(a in b)

    def test_containment_c(self):
        a = ObjectIdentifier("1.3.6.1.2.1.1.1.0")
        b = ObjectIdentifier("1.3.6.1.2.1")
        self.assertTrue(a in b)

    def test_non_containment_a(self):
        a = ObjectIdentifier("1.2.3")
        b = ObjectIdentifier("1.2.3.4")
        self.assertFalse(a in b)

    def test_non_containment_b(self):
        a = ObjectIdentifier("1.2.3.5")
        b = ObjectIdentifier("1.2.3.4")
        self.assertFalse(a in b)

    def test_non_containment_c(self):
        a = ObjectIdentifier("1.2.3.4")
        b = ObjectIdentifier("1.2.3.5")
        self.assertFalse(a in b)

    def test_non_containment_d(self):
        a = ObjectIdentifier("1.3.6.1.2.1.25.1.1.0")
        b = ObjectIdentifier("1.3.6.1.2.1.1.9")
        self.assertFalse(a in b)

    def test_non_containment_e(self):
        a = ObjectIdentifier("1.3.6.1.2.13")
        b = ObjectIdentifier("1.3.6.1.2.1")
        self.assertFalse(a in b)

    def test_create_by_iterable(self):
        result = ObjectIdentifier("1.2.3")
        expected = ObjectIdentifier("1.2.3")
        self.assertEqual(result, expected)

    def test_repr(self):
        result = repr(ObjectIdentifier("1.2.3"))
        expected = "ObjectIdentifier('1.2.3')"
        self.assertEqual(result, expected)

    def test_hash(self):
        """
        Test hash function and that it makes sense.
        """
        result = hash(ObjectIdentifier("1.2.3"))
        expected = hash(ObjectIdentifier("1.2.3"))
        self.assertEqual(result, expected)

    def test_non_containment_f(self):
        """
        This case showed up during development of bulk operations. Throwing it
        into the unit tests to ensure proper containment checks.
        """
        a = ObjectIdentifier("1.3.6.1.2.1.2.2.1.22")
        b = ObjectIdentifier("1.3.6.1.2.1.2.2.1.10.38")
        self.assertNotIn(a, b, "%s should not be in %s" % (a, b))
        self.assertNotIn(b, a, "%s should not be in %s" % (b, a))

    def test_length_1(self):
        """
        OIDs with one node should have a length of 1
        """
        obj = ObjectIdentifier("1")
        self.assertEqual(len(obj), 1)

    def test_length_ge1(self):
        """
        OIDs with more than one node should have a length equal to the number
        of nodes.
        """
        obj = ObjectIdentifier("1.2.3")
        self.assertEqual(len(obj), 3)

    def test_inequalitites(self):
        a = ObjectIdentifier("1.2.3")
        b = ObjectIdentifier("1.2.4")
        self.assertTrue(a < b)
        self.assertFalse(b < a)
        self.assertFalse(a < a)
        self.assertFalse(a > b)
        self.assertTrue(b > a)
        self.assertFalse(b > b)

    def test_concatenation(self):
        a = ObjectIdentifier("1.2.3")
        b = ObjectIdentifier("4.5.6")
        expected = ObjectIdentifier("1.2.3.4.5.6")
        result = a + b
        self.assertEqual(result, expected)

    def test_item_access(self):
        a = ObjectIdentifier("1.2.3")
        expected = 2
        result = a[1]
        self.assertEqual(result, expected)

    def test_slicing(self):
        a = ObjectIdentifier("1.2.3.4.5")
        expected = ObjectIdentifier("1.2.3.4")
        result = a[:-1]
        self.assertEqual(result, expected)

    def test_large_first_values(self):
        """
        When working with sub-trees the first two values can be larger than is
        accepted for *absolute* OIDs
        """
        str_value = "56.1342177480.31"
        oid = ObjectIdentifier(str_value)
        assert str_value == str(oid)


class TestInteger(TestCase):
    def test_encode_empty(self):
        result = Integer().encode_raw()
        assert result == b""

    def test_encoding(self):
        value = Integer(100)
        result = bytes(value)
        expected = b"\x02\x01\x64"
        assert_bytes_equal(result, expected)

    def test_decoding(self):
        result, _ = decode(b"\x02\x01\x0a")
        expected = Integer(10)
        self.assertEqual(result, expected)

    def test_encoding_large_value(self):
        value = Integer(1913359423)
        result = bytes(value)
        expected = b"\x02\x04\x72\x0b\x8c\x3f"
        assert_bytes_equal(result, expected)

    def test_decoding_large_value(self):
        result, _ = decode(b"\x02\x04\x72\x0b\x8c\x3f")
        expected = Integer(1913359423)
        self.assertEqual(result, expected)

    def test_encoding_zero(self):
        value = Integer(0)
        result = bytes(value)
        expected = b"\x02\x01\x00"
        assert_bytes_equal(result, expected)

    def test_decoding_zero(self):
        result, _ = decode(b"\x02\x01\x00")
        expected = Integer(0)
        self.assertEqual(result, expected)

    def test_decoding_minus_one(self):
        result, _ = decode(b"\x02\x01\xff")
        expected = Integer(-1)
        self.assertEqual(result, expected)

    def test_decoding_minus_large_value(self):
        result, _ = decode(b"\x02\x04\x8d\xf4\x73\xc1")
        expected = Integer(-1913359423)
        self.assertEqual(result, expected)

    def test_pythonize(self):
        result = Integer(1).pythonize()
        expected = 1
        self.assertEqual(result, expected)


class TestIntegerValues(TestCase):
    def test_32768(self):
        """
        Issue identified in github issue #27

        See https://github.com/exhuma/puresnmp/issues/27
        """
        value = Integer(32768)
        result = bytes(value)
        expected = b"\x02\x03\x00\x80\x00"
        assert_bytes_equal(result, expected)

    def test_minus_one(self):
        value = Integer(-1)
        result = bytes(value)
        expected = b"\x02\x01\xff"
        assert_bytes_equal(result, expected)

    def test_minus_two(self):
        value = Integer(-2)
        result = bytes(value)
        expected = b"\x02\x01\xfe"
        assert_bytes_equal(result, expected)

    def test_zero(self):
        value = Integer(0)
        result = bytes(value)
        expected = b"\x02\x01\x00"
        assert_bytes_equal(result, expected)

    def test_minus_16bit(self):
        value = Integer(-0b1111111111111111)
        result = bytes(value)
        expected = b"\x02\x03\xff\x00\x01"
        assert_bytes_equal(result, expected)

    def test_minus_16bit_plus_one(self):
        value = Integer(-0b1111111111111111 + 1)
        result = bytes(value)
        expected = b"\x02\x03\xff\x00\x02"
        assert_bytes_equal(result, expected)

    def test_minus_16bit_minus_one(self):
        value = Integer(-0b1111111111111111 - 1)
        result = bytes(value)
        expected = b"\x02\x03\xff\x00\x00"
        assert_bytes_equal(result, expected)

    def test_minus_16bit_minus_two(self):
        value = Integer(-0b1111111111111111 - 2)
        result = bytes(value)
        expected = b"\x02\x03\xfe\xff\xff"
        assert_bytes_equal(result, expected)

    def test_16bit(self):
        value = Integer(0b1111111111111111)
        result = bytes(value)
        expected = b"\x02\x03\x00\xff\xff"
        assert_bytes_equal(result, expected)

    def test_16bitplusone(self):
        value = Integer(0b1111111111111111 + 1)
        result = bytes(value)
        expected = b"\x02\x03\x01\x00\x00"
        assert_bytes_equal(result, expected)

    def test_16bitminusone(self):
        value = Integer(0b1111111111111111 - 1)
        result = bytes(value)
        expected = b"\x02\x03\x00\xff\xfe"
        assert_bytes_equal(result, expected)

    def test_32bit(self):
        value = Integer(0b11111111111111111111111111111111)
        result = bytes(value)
        expected = b"\x02\x05\x00\xff\xff\xff\xff"
        assert_bytes_equal(result, expected)


class TestOctetString(TestCase):
    def test_encoding(self):
        value = OctetString("hello")
        result = bytes(value)
        expected = b"\x04\x05hello"
        assert_bytes_equal(result, expected)

    def test_decoding(self):
        result, _ = decode(b"\x04\x05hello")
        expected = OctetString("hello")
        self.assertEqual(result, expected)

    def test_pythonize(self):
        result = OctetString("hello").pythonize()
        expected = b"hello"
        self.assertEqual(result, expected)

    def test_decoding_indef_length(self):
        data_def = b"\x04\x0bhello-world"
        data_indef = b"\x04\x80hello-world\x00\x00"
        result_def, _ = decode(data_def)
        result_indef, _ = decode(data_indef)
        assert result_def == result_indef


class TestT61String(TestCase):
    def test_encode_empty(self):
        result = T61String().encode_raw()
        assert result == b""

    def test_encoding(self):
        value = T61String("hello Ω")
        result = bytes(value)
        expected = b"\x14\x07hello \xe0"
        assert_bytes_equal(result, expected)

    def test_decoding(self):
        result, _ = decode(b"\x14\x07hello \xe0")
        expected = T61String("hello Ω")
        self.assertEqual(result, expected)

    def test_pythonize_from_string(self):
        obj = T61String("hello Ω")
        result = obj.pythonize()
        expected = "hello Ω"
        self.assertEqual(result, expected)

    def test_pythonize_from_bytes(self):
        obj = T61String(b"hello \xe0")
        result = obj.pythonize()
        expected = "hello Ω"
        self.assertEqual(result, expected)


class TestSequence(TestCase):
    def test_encode_raw(self):
        data = b"\x04\x05hello\x06\x02+\x06\x02\x01d"
        result = Sequence.decode_raw(data)
        expected = [
            OctetString(b"hello"),
            ObjectIdentifier("1.3.6"),
            Integer(100),
        ]
        assert result == expected

    def test_encode_raw_empty(self):
        result = Sequence().encode_raw()
        assert result == b""

    def test_eq_instancecheck(self):
        assert Sequence() != object()

    def test_encoding(self):
        value = Sequence(
            [OctetString("hello"), ObjectIdentifier("1.3.6"), Integer(100)]
        )
        result = bytes(value)
        expected = (
            bytes(
                [
                    0x30,
                    14,  # Expected length (note that an OID drops one byte)
                ]
            )
            + bytes(OctetString("hello"))
            + bytes(ObjectIdentifier("1.3.6"))
            + bytes(Integer(100))
        )
        assert_bytes_equal(result, expected)

    def test_raw_bytes_init(self):
        """
        After a normal init, the raw_bytes should be set
        """
        obj = Sequence(
            [
                Integer(1),
                Integer(2),
                OctetString("foo"),
            ]
        )
        self.assertEqual(obj.raw_bytes, b"\x02\x01\x01\x02\x01\x02\x04\x03foo")

    def test_decoding_simple(self):
        result, _ = decode(b"\x30\x0b\x02\x01\x01\x02\x01\x02\x04\x03foo")
        expected = Sequence(
            [
                Integer(1),
                Integer(2),
                OctetString("foo"),
            ]
        )
        self.assertEqual(result, expected)

    def test_decoding_recursive(self):
        result, _ = decode(
            b"\x30\x13"
            b"\x02\x01\x01"
            b"\x02\x01\x02"
            b"\x04\x03foo"
            b"\x30\x06"
            b"\x02\x01\x01"
            b"\x02\x01\x02"
        )
        expected = Sequence(
            [
                Integer(1),
                Integer(2),
                OctetString("foo"),
                Sequence(
                    [
                        Integer(1),
                        Integer(2),
                    ]
                ),
            ]
        )
        self.assertEqual(result, expected)

    def test_pythonize(self):
        result = Sequence(
            [Integer(1), Sequence([OctetString("123")])]
        ).pythonize()
        expected = [1, [b"123"]]
        self.assertEqual(result, expected)

    def test_iteration(self):
        data = Sequence(
            [Integer(1), Sequence([OctetString("123")]), OctetString(b"foo")]
        )
        result = [item for item in data]
        expected = [
            Integer(1),
            Sequence([OctetString("123")]),
            OctetString(b"foo"),
        ]
        self.assertEqual(result, expected)

    def test_indexing(self):
        data = Sequence([Integer(1), OctetString(b"foo")])
        result = data[1]
        expected = OctetString(b"foo")
        self.assertEqual(result, expected)

    def test_repr(self):
        result = repr(Sequence([Integer(10)]))
        expected = "Sequence([Integer(10)])"
        self.assertEqual(result, expected)

    def test_length_empty(self):
        result = len(Sequence())
        expected = 0
        self.assertEqual(result, expected)

    def test_length_nonempty(self):
        result = len(Sequence([Integer(1), Integer(2)]))
        expected = 2
        self.assertEqual(result, expected)


class TestNull(TestCase):
    def test_null_is_false(self):
        """
        The Null type should be considered as falsy.
        """
        self.assertFalse(Null())

    def test_validate_true(self):
        Null.validate(b"\x05\x00")

    def test_validate_false(self):
        with self.assertRaises(ValueError):
            Null.validate(b"\x05\x01")

    def test_encoding(self):
        result = bytes(Null())
        expected = b"\x05\x00"
        self.assertEqual(result, expected)

    def test_decode_null(self):
        expected = Null()
        result = Null.decode(b"\x05\x00\x00")
        self.assertEqual(result, expected)

    def test_repr(self):
        expected = "Null()"
        result = repr(Null())
        self.assertEqual(result, expected)


class TestUnknownType(TestCase):
    def test_decoding(self):
        result, _ = decode(b"\x99\x01\x0a")
        expected = UnknownType(b"\x0a", 0x99)
        self.assertEqual(result, expected)

    def test_encoding(self):
        result = bytes(UnknownType(b"\x0a", 0x99))
        expected = b"\xb9\x01\x0a"
        self.assertEqual(result, expected)

    def test_repr(self):
        result = repr(UnknownType(b"abc", 99))
        typeinfo = TypeInfo("application", "constructed", 3)
        expected = "<UnknownType 99 b'abc' application/constructed/3>"
        self.assertEqual(result, expected)

    def test_decoding_indef_length(self):
        data = bytearray(bytes(UnknownType(b"some-data", 0x99)))
        data_def = bytes(data)
        data[1] = 0x80
        data.extend([0x00, 0x00])
        data_indef = bytes(data)
        result_def, _ = decode(data_def)
        result_indef, _ = decode(data_indef)
        assert result_def == result_indef


class TestAllTypes(TestCase):
    """
    Tests which are valid for all types
    """

    def test_tlv_empty(self):
        with pytest.raises(IndexError):
            decode(b"")

    def test_tlv_simple(self):
        result = decode(bytes([2, 1, 0]))
        expected = (Integer(0), 3)
        self.assertEqual(result, expected)

    def test_tlv_unknown_type(self):
        result, next_index = decode(bytes([254, 1, 0]))
        expected = UnknownType(b"\x00", 254)
        self.assertEqual(next_index, 3)
        self.assertEqual(result, expected)
        self.assertEqual(result.tag, 254)
        self.assertEqual(result.length, 1)
        self.assertEqual(result.value, b"\x00")

    def test_validation_wrong_typeclass(self):
        with self.assertRaises(ValueError):
            Integer.validate(bytes([0b00111110]))

    def test_childof(self):
        a = ObjectIdentifier("1.2.3")
        b = ObjectIdentifier("1.2.3.1")
        c = ObjectIdentifier("1.2.4")
        d = ObjectIdentifier("1")
        self.assertTrue(b.childof(a))
        self.assertFalse(a.childof(b))
        self.assertTrue(a.childof(a))
        self.assertFalse(c.childof(a))
        self.assertFalse(a.childof(c))
        self.assertFalse(d.childof(c))
        self.assertTrue(c.childof(d))

    def test_parentdf(self):
        a = ObjectIdentifier("1.2.3")
        b = ObjectIdentifier("1.2.3.1")
        c = ObjectIdentifier("1.2.4")
        d = ObjectIdentifier("1")
        self.assertFalse(b.parentof(a))
        self.assertTrue(a.parentof(b))
        self.assertTrue(a.parentof(a))
        self.assertFalse(c.parentof(a))
        self.assertFalse(a.parentof(c))
        self.assertTrue(d.parentof(c))
        self.assertFalse(c.parentof(d))


@pytest.mark.parametrize("cls", X690Type.all())
def test_noarg_constructor(cls):
    """
    We want to allow a no-args constructor on everything. It should create
    an instance of the requested class
    """
    instance = cls()
    assert isinstance(instance, cls)


def test_enforcing_types():
    data = bytes(OctetString(b"foo"))
    with pytest.raises(UnexpectedType):
        decode(data, enforce_type=Integer)


def test_incomplete_decoding():
    """
    When calling decode in strict mode we want to raise an exception on
    unconsumed bytes.
    """
    data = bytes(OctetString(b"Hello")) + b"junk-bytes"
    with pytest.raises(IncompleteDecoding) as exc:
        decode(data, strict=True)
    assert exc.value.remainder == b"junk-bytes"


@pytest.mark.parametrize("cls", X690Type.all())
def test_repr(cls):
    obj = cls()
    result = repr(obj)
    assert cls.__name__ in result


def test_helpful_noarg_error():
    """
    Custom types with no "no-arg" constructor lead to cryptic errors if not
    dealt with. Ensure the error-message is somewhat helpful.
    """

    class Foo(X690Type[str]):
        def __init__(self, value: str) -> None:
            """
            This enforces *value* which breaks lazy-decoding
            """
            super().__init__(value)

    with pytest.raises(X690Error) as exc:
        Foo.from_bytes(b"hello")
    exc.match("no-arg.*x690.types.UNINITIALISED")
