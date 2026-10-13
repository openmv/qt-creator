# pylint: skip-file
import pytest

from x690 import types as t
from x690.util import TypeClass, TypeInfo, TypeNature

UNIVERSAL = TypeClass.UNIVERSAL
APPLICATION = TypeClass.APPLICATION
PRIVATE = TypeClass.PRIVATE
CONTEXT = TypeClass.CONTEXT
PRIMITIVE = TypeNature.PRIMITIVE
CONSTRUCTED = TypeNature.CONSTRUCTED


@pytest.mark.skip("TODO")
class TestBasics:
    def test_identifier_long(self):
        self.skipTest(
            "This is not yet implemented. I have not understood the "
            "spec to confidently write a test"
        )  # TODO


@pytest.mark.parametrize(
    "octet, expected_class, expected_pc, expected_value",
    [
        (0b00000010, UNIVERSAL, PRIMITIVE, 0b00010),
        (0b00100010, UNIVERSAL, CONSTRUCTED, 0b00010),
        (0b01000010, APPLICATION, PRIMITIVE, 0b00010),
        (0b01100010, APPLICATION, CONSTRUCTED, 0b00010),
        (0b10000010, CONTEXT, PRIMITIVE, 0b00010),
        (0b10100010, CONTEXT, CONSTRUCTED, 0b00010),
        (0b11000010, PRIVATE, PRIMITIVE, 0b00010),
        (0b11100010, PRIVATE, CONSTRUCTED, 0b00010),
    ],
)
def test_identifiers(octet, expected_class, expected_pc, expected_value):
    result = TypeInfo.from_bytes(octet)
    expected = TypeInfo(expected_class, expected_pc, expected_value)
    assert result == expected


@pytest.mark.parametrize(
    "typeclass, tag, pc, expected_class",
    [
        (UNIVERSAL, 0x00, PRIMITIVE, t.EOC),
        (UNIVERSAL, 0x01, PRIMITIVE, t.Boolean),
        (UNIVERSAL, 0x02, PRIMITIVE, t.Integer),
        (UNIVERSAL, 0x03, PRIMITIVE, t.BitString),
        (UNIVERSAL, 0x04, PRIMITIVE, t.OctetString),
        (UNIVERSAL, 0x05, PRIMITIVE, t.Null),
        (UNIVERSAL, 0x06, PRIMITIVE, t.ObjectIdentifier),
        (UNIVERSAL, 0x07, PRIMITIVE, t.ObjectDescriptor),
        (UNIVERSAL, 0x09, PRIMITIVE, t.Real),
        (UNIVERSAL, 0x0A, PRIMITIVE, t.Enumerated),
        (UNIVERSAL, 0x0C, PRIMITIVE, t.Utf8String),
        (UNIVERSAL, 0x0D, PRIMITIVE, t.RelativeOid),
        (UNIVERSAL, 0x12, PRIMITIVE, t.NumericString),
        (UNIVERSAL, 0x13, PRIMITIVE, t.PrintableString),
        (UNIVERSAL, 0x14, PRIMITIVE, t.T61String),
        (UNIVERSAL, 0x15, PRIMITIVE, t.VideotexString),
        (UNIVERSAL, 0x16, PRIMITIVE, t.IA5String),
        (UNIVERSAL, 0x17, PRIMITIVE, t.UtcTime),
        (UNIVERSAL, 0x18, PRIMITIVE, t.GeneralizedTime),
        (UNIVERSAL, 0x19, PRIMITIVE, t.GraphicString),
        (UNIVERSAL, 0x1A, PRIMITIVE, t.VisibleString),
        (UNIVERSAL, 0x1B, PRIMITIVE, t.GeneralString),
        (UNIVERSAL, 0x1C, PRIMITIVE, t.UniversalString),
        (UNIVERSAL, 0x1E, PRIMITIVE, t.BmpString),
        (UNIVERSAL, 0x08, CONSTRUCTED, t.External),
        (UNIVERSAL, 0x0B, CONSTRUCTED, t.EmbeddedPdv),
        (UNIVERSAL, 0x10, CONSTRUCTED, t.Sequence),
        (UNIVERSAL, 0x11, CONSTRUCTED, t.Set),
        (UNIVERSAL, 0x1D, CONSTRUCTED, t.CharacterString),
    ],
)
def test_class_detection(typeclass, tag, pc, expected_class):
    result = t.X690Type.get(typeclass, tag, pc)
    assert result == expected_class
