"""
This module contains exceptions for the x.690 protocol
"""


class X690Error(Exception):
    """
    Top-Level exception for everything related to the X690 protocol
    """


class UnexpectedType(X690Error):
    """
    Raised when decoding resulted in an unexpected type.
    """


class IncompleteDecoding(X690Error):
    """
    Raised when decoding did not consume all bytes.

    The junk bytes are stored in the "remainder" attribute
    """

    def __init__(self, message: str, remainder: bytes) -> None:
        super().__init__(message)
        self.remainder = remainder
