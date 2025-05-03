# -*- coding: utf-8 -*-
"""
Python Character Mapping Codec for T61

See https://en.wikipedia.org/wiki/ITU_T.61
"""

# pylint: disable=invalid-name, no-member, redefined-builtin

import codecs
from typing import Tuple


__version__ = '1.0.1'


class Codec(codecs.Codec):
    """
    Main implementation for the T.61 codec, based on
    :py:func:`codecs.charmap_encode` and :py:func:`codecs.charmap_decode`
    """

    def encode(self, input, errors='strict'):
        # type: (str, str) -> Tuple[bytes, int]
        return codecs.charmap_encode(  # type: ignore
            input, errors, encoding_table)

    def decode(self, input, errors='strict'):
        # type: (bytes, str) -> Tuple[str, int]
        return codecs.charmap_decode(  # type: ignore
            input, errors, decoding_table)


class IncrementalEncoder(codecs.IncrementalEncoder):
    """
    See :py:class:`codecs.IncrementalEncoder`
    """
    def encode(self, input, final=False):
        # type: (str, bool) -> bytes
        return codecs.charmap_encode(  # type: ignore
            input, self.errors, encoding_table)[0]


class IncrementalDecoder(codecs.IncrementalDecoder):
    """
    See :py:class:`codecs.IncrementalDecoder`
    """
    def decode(self, input, final=False):
        # type: (bytes, bool) -> str
        return codecs.charmap_decode(  # type: ignore
            input, self.errors, decoding_table)[0]


class StreamWriter(Codec, codecs.StreamWriter):
    """
    See :py:class:`codecs.StreamWriter`
    """


class StreamReader(Codec, codecs.StreamReader):
    """
    See :py:class:`codecs.StreamReader`
    """


def getregentry():
    # type: () -> codecs.CodecInfo
    """
    Creates a :py:class:`codecs.CodecInfo` instance for use in the registry
    """
    return codecs.CodecInfo(
        name='t.61',
        encode=Codec().encode,
        decode=Codec().decode,
        incrementalencoder=IncrementalEncoder,
        incrementaldecoder=IncrementalDecoder,
        streamreader=StreamReader,
        streamwriter=StreamWriter,
    )


decoding_table = (
    u'\x00'  # 0x00 -> NULL
    u'\x01'  # 0x01 -> START OF HEADING
    u'\x02'  # 0x02 -> START OF TEXT
    u'\x03'  # 0x03 -> END OF TEXT
    u'\x04'  # 0x04 -> END OF TRANSMISSION
    u'\x05'  # 0x05 -> ENQUIRY
    u'\x06'  # 0x06 -> ACKNOWLEDGE
    u'\x07'  # 0x07 -> BELL
    u'\x08'  # 0x08 -> BACKSPACE
    u'\t'  # 0x09 -> HORIZONTAL TABULATION
    u'\n'  # 0x0A -> LINE FEED
    u'\x0b'  # 0x0B -> VERTICAL TABULATION
    u'\x0c'  # 0x0C -> FORM FEED
    u'\r'  # 0x0D -> CARRIAGE RETURN
    u'\x0e'  # 0x0E -> SHIFT OUT
    u'\x0f'  # 0x0F -> SHIFT IN
    u'\x10'  # 0x10 -> DATA LINK ESCAPE
    u'\x11'  # 0x11 -> DEVICE CONTROL ONE
    u'\x12'  # 0x12 -> DEVICE CONTROL TWO
    u'\x13'  # 0x13 -> DEVICE CONTROL THREE
    u'\x14'  # 0x14 -> DEVICE CONTROL FOUR
    u'\x15'  # 0x15 -> NEGATIVE ACKNOWLEDGE
    u'\x16'  # 0x16 -> SYNCHRONOUS IDLE
    u'\x17'  # 0x17 -> END OF TRANSMISSION BLOCK
    u'\x18'  # 0x18 -> CANCEL
    u'\x19'  # 0x19 -> END OF MEDIUM
    u'\x1a'  # 0x1A -> SUBSTITUTE
    u'\x1b'  # 0x1B -> ESCAPE
    u'\x1c'  # 0x1C -> FILE SEPARATOR
    u'\x1d'  # 0x1D -> GROUP SEPARATOR
    u'\x1e'  # 0x1E -> RECORD SEPARATOR
    u'\x1f'  # 0x1F -> UNIT SEPARATOR
    u' '  # 0x20 -> SPACE
    u'!'  # 0x21 -> EXCLAMATION MARK
    u'"'  # 0x22 -> QUOTATION MARK
    u'\ufffe'  # 0x23 -> *unmapped*
    u'\ufffe'  # 0x24 -> *unmapped*
    u'%'  # 0x25 -> PERCENT SIGN
    u'&'  # 0x26 -> AMPERSAND
    u"'"  # 0x27 -> APOSTROPHE
    u'('  # 0x28 -> LEFT PARENTHESIS
    u')'  # 0x29 -> RIGHT PARENTHESIS
    u'*'  # 0x2A -> ASTERISK
    u'+'  # 0x2B -> PLUS SIGN
    u','  # 0x2C -> COMMA
    u'-'  # 0x2D -> HYPHEN-MINUS
    u'.'  # 0x2E -> FULL STOP
    u'/'  # 0x2F -> SOLIDUS
    u'0'  # 0x30 -> DIGIT ZERO
    u'1'  # 0x31 -> DIGIT ONE
    u'2'  # 0x32 -> DIGIT TWO
    u'3'  # 0x33 -> DIGIT THREE
    u'4'  # 0x34 -> DIGIT FOUR
    u'5'  # 0x35 -> DIGIT FIVE
    u'6'  # 0x36 -> DIGIT SIX
    u'7'  # 0x37 -> DIGIT SEVEN
    u'8'  # 0x38 -> DIGIT EIGHT
    u'9'  # 0x39 -> DIGIT NINE
    u':'  # 0x3A -> COLON
    u';'  # 0x3B -> SEMICOLON
    u'<'  # 0x3C -> LESS-THAN SIGN
    u'='  # 0x3D -> EQUALS SIGN
    u'>'  # 0x3E -> GREATER-THAN SIGN
    u'?'  # 0x3F -> QUESTION MARK
    u'@'  # 0x40 -> COMMERCIAL AT
    u'A'  # 0x41 -> LATIN CAPITAL LETTER A
    u'B'  # 0x42 -> LATIN CAPITAL LETTER B
    u'C'  # 0x43 -> LATIN CAPITAL LETTER C
    u'D'  # 0x44 -> LATIN CAPITAL LETTER D
    u'E'  # 0x45 -> LATIN CAPITAL LETTER E
    u'F'  # 0x46 -> LATIN CAPITAL LETTER F
    u'G'  # 0x47 -> LATIN CAPITAL LETTER G
    u'H'  # 0x48 -> LATIN CAPITAL LETTER H
    u'I'  # 0x49 -> LATIN CAPITAL LETTER I
    u'J'  # 0x4A -> LATIN CAPITAL LETTER J
    u'K'  # 0x4B -> LATIN CAPITAL LETTER K
    u'L'  # 0x4C -> LATIN CAPITAL LETTER L
    u'M'  # 0x4D -> LATIN CAPITAL LETTER M
    u'N'  # 0x4E -> LATIN CAPITAL LETTER N
    u'O'  # 0x4F -> LATIN CAPITAL LETTER O
    u'P'  # 0x50 -> LATIN CAPITAL LETTER P
    u'Q'  # 0x51 -> LATIN CAPITAL LETTER Q
    u'R'  # 0x52 -> LATIN CAPITAL LETTER R
    u'S'  # 0x53 -> LATIN CAPITAL LETTER S
    u'T'  # 0x54 -> LATIN CAPITAL LETTER T
    u'U'  # 0x55 -> LATIN CAPITAL LETTER U
    u'V'  # 0x56 -> LATIN CAPITAL LETTER V
    u'W'  # 0x57 -> LATIN CAPITAL LETTER W
    u'X'  # 0x58 -> LATIN CAPITAL LETTER X
    u'Y'  # 0x59 -> LATIN CAPITAL LETTER Y
    u'Z'  # 0x5A -> LATIN CAPITAL LETTER Z
    u'['  # 0x5B -> LEFT SQUARE BRACKET
    u'\ufffe'  # 0x5C -> *unmapped*
    u']'  # 0x5D -> RIGHT SQUARE BRACKET
    u'\ufffe'  # 0x5E -> *unmapped*
    u'_'  # 0x5F -> LOW LINE
    u'\ufffe'  # 0x60 -> *unmapped*
    u'a'  # 0x61 -> LATIN SMALL LETTER A
    u'b'  # 0x62 -> LATIN SMALL LETTER B
    u'c'  # 0x63 -> LATIN SMALL LETTER C
    u'd'  # 0x64 -> LATIN SMALL LETTER D
    u'e'  # 0x65 -> LATIN SMALL LETTER E
    u'f'  # 0x66 -> LATIN SMALL LETTER F
    u'g'  # 0x67 -> LATIN SMALL LETTER G
    u'h'  # 0x68 -> LATIN SMALL LETTER H
    u'i'  # 0x69 -> LATIN SMALL LETTER I
    u'j'  # 0x6A -> LATIN SMALL LETTER J
    u'k'  # 0x6B -> LATIN SMALL LETTER K
    u'l'  # 0x6C -> LATIN SMALL LETTER L
    u'm'  # 0x6D -> LATIN SMALL LETTER M
    u'n'  # 0x6E -> LATIN SMALL LETTER N
    u'o'  # 0x6F -> LATIN SMALL LETTER O
    u'p'  # 0x70 -> LATIN SMALL LETTER P
    u'q'  # 0x71 -> LATIN SMALL LETTER Q
    u'r'  # 0x72 -> LATIN SMALL LETTER R
    u's'  # 0x73 -> LATIN SMALL LETTER S
    u't'  # 0x74 -> LATIN SMALL LETTER T
    u'u'  # 0x75 -> LATIN SMALL LETTER U
    u'v'  # 0x76 -> LATIN SMALL LETTER V
    u'w'  # 0x77 -> LATIN SMALL LETTER W
    u'x'  # 0x78 -> LATIN SMALL LETTER X
    u'y'  # 0x79 -> LATIN SMALL LETTER Y
    u'z'  # 0x7A -> LATIN SMALL LETTER Z
    u'\ufffe'  # 0x7B -> *unmapped*
    u'|'  # 0x7C -> VERTICAL LINE
    u'\ufffe'  # 0x7D -> *unmapped*
    u'\ufffe'  # 0x7E -> *unmapped*
    u'\x7f'  # 0x7F -> DELETE
    u'\x80'  # 0x80 -> PADDING CHARACTER
    u'\x81'  # 0x81 -> HIGH OCTET PRESET
    u'\x82'  # 0x82 -> BREAK PERMITTED HERE (BPH)
    u'\x83'  # 0x83 -> NO BREAK HERE (NBH)
    u'\x84'  # 0x84 -> INDEX (IND)
    u'\x85'  # 0x85 -> NEXT LINE (NEL)
    u'\x86'  # 0x86 -> START OF SELECTED AREA (SSA)
    u'\x87'  # 0x87 -> END OF SELECTED AREA (ESA)
    u'\x88'  # 0x88 -> CHARACTER TABULATION SET (HTS)
    u'\x89'  # 0x89 -> CHARACTER TABULATION WITH JUSTIFICATION (HTJ)
    u'\x8a'  # 0x8a -> LINE TABULATION SET (VTS)
    u'\x8b'  # 0x8b -> PARTIAL LINE FORWARD (PLD)
    u'\x8c'  # 0x8c -> PARTIAL LINE BACKWARD (PLU)
    u'\x8d'  # 0x8d -> REVERSE LINE FEED (RI)
    u'\x8e'  # 0x8e -> SINGLE-SHIFT TWO (SS2)
    u'\x8f'  # 0x8f -> SINGLE-SHIFT THREE (SS3)
    u'\x90'  # 0x90 -> DEVICE CONTROL STRING (DCS)
    u'\x91'  # 0x91 -> PRIVATE USE ONE (PU1)
    u'\x92'  # 0x92 -> PRIVATE USE TWO (PU2)
    u'\x93'  # 0x93 -> SET TRANSMIT STATE (STS)
    u'\x94'  # 0x94 -> CANCEL CHARACTER (CCH)
    u'\x95'  # 0x95 -> MESSAGE WAITING (MW)
    u'\x96'  # 0x96 -> START OF GUARDED AREA (SPA)
    u'\x97'  # 0x97 -> END OF GUARDED AREA (EPA)
    u'\x98'  # 0x98 -> START OF STRING (SOS)
    u'\x99'  # 0x99 -> SINGLE GRAPHIC CHARACTER INTRODUCER (SGCI)
    u'\x9a'  # 0x9a -> SINGLE CHARACTER INTRODUCER (SCI)
    u'\x9b'  # 0x9b -> CONTROL SEQUENCE INTRODUCER (CSI)
    u'\x9c'  # 0x9c -> STRING TERMINATOR (ST)
    u'\x9d'  # 0x9d -> OPERATING SYSTEM COMMAND (OSC)
    u'\x9e'  # 0x9e -> PRIVACY MESSAGE (PM)
    u'\x9f'  # 0x9f -> APPLICATION PROGRAM COMMAND (APC)
    u'\xa0'  # 0xA0 -> NO-BREAK SPACE
    u'\xa1'  # 0xA1 -> INVERTED EXCLAMATION MARK
    u'\xa2'  # 0xA2 -> CENT SIGN
    u'\xa3'  # 0xA3 -> POUND SIGN
    u'$'  # 0xA4 -> DOLLAR SIGN
    u'\xa5'  # 0xA5 -> YEN SIGN
    u'#'  # 0xA6 -> NUMBER SIGN
    u'\xa7'  # 0xA7 -> SECTION SIGN
    u'\xa4'  # 0xA8 -> CURRENCY SIGN
    u'\ufffe'  # 0xA9 -> *unmapped*
    u'\ufffe'  # 0xAA -> *unmapped*
    u'\xab'  # 0xAB -> LEFT-POINTING DOUBLE ANGLE QUOTATION MARK
    u'\ufffe'  # 0xAC -> *unmapped*
    u'\ufffe'  # 0xAD -> *unmapped*
    u'\ufffe'  # 0xAE -> *unmapped*
    u'\ufffe'  # 0xAF -> *unmapped*
    u'\xb0'  # 0xB0 -> DEGREE SIGN
    u'\xb1'  # 0xB1 -> PLUS-MINUS SIGN
    u'\xb2'  # 0xB2 -> SUPERSCRIPT TWO
    u'\xb3'  # 0xB3 -> SUPERSCRIPT THREE
    u'\xd7'  # 0xD7 -> MULTIPLICATION SIGN
    u'\xb5'  # 0xB5 -> MICRO SIGN
    u'\xb6'  # 0xB6 -> PILCROW SIGN
    u'\xb7'  # 0xB7 -> MIDDLE DOT
    u'\xf7'  # 0xF7 -> DIVISION SIGN
    u'\ufffe'  # 0xF8 -> *unmapped*
    u'\ufffe'  # 0xF9 -> *unmapped*
    u'\xbb'  # 0xBB -> RIGHT-POINTING DOUBLE ANGLE QUOTATION MARK
    u'\xbc'  # 0xBC -> VULGAR FRACTION ONE QUARTER
    u'\xbd'  # 0xBD -> VULGAR FRACTION ONE HALF
    u'\xbe'  # 0xBE -> VULGAR FRACTION THREE QUARTERS
    u'\xbf'  # 0xBF -> INVERTED QUESTION MARK
    u'\ufffe'  # 0xC0 -> *unmapped*
    u'\u0300'  # 0xC1 -> COMBINING GRAVE ACCENT
    u'\u0301'  # 0xC2 -> COMBINING ACUTE ACCENT
    u'\u0302'  # 0xC3 -> COMBINING CIRCUMFLEX ACCENT
    u'\u0303'  # 0xC4 -> COMBINING TILDE
    u'\u0304'  # 0xC5 -> COMBINING MACRON
    u'\u0306'  # 0xC6 -> COMBINING BREVE
    u'\u0307'  # 0xC7 -> COMBINING DOT ABOVE
    u'\u0308'  # 0xC8 -> COMBINING DIAERESIS
    u'\ufffe'  # 0xC9 -> *unmapped*
    u'\u030a'  # 0xCA -> COMBINING RING ABOVE
    u'\u0327'  # 0xCB -> COMBINING CEDILLA
    u'\u0332'  # 0xCC -> COMBINING LOW LINE
    u'\u030b'  # 0xCD -> COMBINING DOUBLE ACUTE ACCENT
    u'\u032b'  # 0xCE -> COMBINING INVERTED DOUBLE ARCH BELOW
    u'\u030c'  # 0xCF -> COMBINING CARON
    u'\ufffe'  # 0xD0 -> *unmapped*
    u'\ufffe'  # 0xD1 -> *unmapped*
    u'\ufffe'  # 0xD2 -> *unmapped*
    u'\ufffe'  # 0xD3 -> *unmapped*
    u'\ufffe'  # 0xD4 -> *unmapped*
    u'\ufffe'  # 0xD5 -> *unmapped*
    u'\ufffe'  # 0xD6 -> *unmapped*
    u'\ufffe'  # 0xD7 -> *unmapped*
    u'\ufffe'  # 0xD8 -> *unmapped*
    u'\ufffe'  # 0xD9 -> *unmapped*
    u'\ufffe'  # 0xDA -> *unmapped*
    u'\ufffe'  # 0xDB -> *unmapped*
    u'\ufffe'  # 0xDC -> *unmapped*
    u'\ufffe'  # 0xDD -> *unmapped*
    u'\ufffe'  # 0xDE -> *unmapped*
    u'\ufffe'  # 0xDF -> *unmapped*
    u'\u2126'  # 0xE0 -> OHM SIGN
    u'\u00c6'  # 0xE1 -> LATIN CAPITAL LETTER AE
    u'\u00d0'  # 0xE2 -> LATIN CAPITAL LETTER ETH
    u'\u00aa'  # 0xE3 -> FEMININE ORDINAL INDICATOR
    u'\u0126'  # 0xE4 -> LATIN CAPITAL LETTER H WITH STROKE
    u'\ufffe'  # 0xE5 -> *unmapped*
    u'\u0132'  # 0xE6 -> LATIN CAPITAL LIGATURE IJ
    u'\u013f'  # 0xE7 -> LATIN CAPITAL LETTER L WITH MIDDLE DOT
    u'\u0141'  # 0xE8 -> LATIN CAPITAL LETTER L WITH STROKE
    u'\u00d8'  # 0xE9 -> LATIN CAPITAL LETTER O WITH STROKE
    u'\u0152'  # 0xEA -> LATIN CAPITAL LIGATURE OE
    u'\u00ba'  # 0xEB -> MASCULINE ORDINAL INDICATOR
    u'\u00de'  # 0xEC -> LATIN CAPITAL LETTER THORN
    u'\u0166'  # 0xED -> LATIN CAPITAL LETTER T WITH STROKE
    u'\u014a'  # 0xEE -> LATIN CAPITAL LETTER ENG
    u'\u0149'  # 0xEF -> LATIN SMALL LETTER N PRECEDED BY APOSTROPHE
    u'\u0138'  # 0xF0 -> LATIN SMALL LETTER KRA
    u'\u00e6'  # 0xF1 -> LATIN SMALL LETTER AE
    u'\u0111'  # 0xF2 -> LATIN SMALL LETTER D WITH STROKE
    u'\u00f0'  # 0xF3 -> LATIN SMALL LETTER ETH
    u'\u0127'  # 0xF4 -> LATIN SMALL LETTER H WITH STROKE
    u'\u0131'  # 0xF5 -> LATIN SMALL LETTER DOTLESS I
    u'\u0133'  # 0xF6 -> LATIN SMALL LIGATURE IJ
    u'\u0140'  # 0xF7 -> LATIN SMALL LETTER L WITH MIDDLE DOT
    u'\u0142'  # 0xF8 -> LATIN SMALL LETTER L WITH STROKE
    u'\u00f8'  # 0xF9 -> LATIN SMALL LETTER O WITH STROKE
    u'\u0153'  # 0xFA -> LATIN SMALL LIGATURE OE
    u'\u00df'  # 0xFB -> LATIN SMALL LETTER SHARP S
    u'\u00fe'  # 0xFC -> LATIN SMALL LETTER THORN
    u'\u0167'  # 0xFD -> LATIN SMALL LETTER T WITH STROKE
    u'\u014b'  # 0xFE -> LATIN SMALL LETTER ENG
    u'\ufffe'  # 0xFF -> *unmapped*
)

# Encoding table
encoding_table = codecs.charmap_build(decoding_table)  # type: ignore


def search_function(encoding):
    # type: (str) -> codecs.CodecInfo
    """
    A search function which can be used with :py:func:`codecs.register`.

    As a convenience, there is also :func:`~.register` in this module.
    """
    if encoding.lower() in ('t61', 't.61'):
        return getregentry()
    return codecs.search_function(encoding)  # type: ignore


def register():
    # type: () -> None
    """
    Convenience function which registers a new default Python search function

    Example:

    >>> import t61codec
    >>> t61codec.register
    >>> b'Hello T.61: \xe0'.decode('t.61')
    'Hello T.61: Ω'
    """
    codecs.register(search_function)
