from itertools import zip_longest


def assert_bytes_equal(a, b):
    # type: (Union[bytes, bytearray], Union[bytes, bytearray]) -> None
    """
    Helper method to compare bytes with more helpful output.
    """
    __tracebackhide__ = True

    def is_bytes(x):
        # type: (Union[bytes, bytearray]) -> bool
        return isinstance(x, (bytes, bytearray))

    if not is_bytes(a) or not is_bytes(b):
        raise ValueError("assertBytesEqual requires two bytes objects!")

    if a != b:
        comparisons = []
        type_a = type(a)
        type_b = type(b)
        a = bytearray(a)
        b = bytearray(b)

        def char_repr(c):
            # type: (bytes) -> str
            if 0x1F < char_a < 0x80:
                # bytearray to prevent accidental pre-mature str conv
                # str to prevent b'' suffix in repr's output
                return repr(str(bytearray([char_a]).decode("ascii")))
            return "."

        for offset, (char_a, char_b) in enumerate(zip_longest(a, b)):
            comp, marker = ("==", "") if char_a == char_b else ("!=", ">>")

            # Using "zip_longest", overflows are marked as "None", which is
            # unambiguous in this case, but we need to handle these
            # separately from the main format string.
            if char_a is None:
                char_ab = char_ad = char_ah = char_ar = "?"
            else:
                char_ab = f"0b{char_a:08b}"
                char_ad = f"{char_a:3d}"
                char_ah = f"0x{char_a:02x}"
                char_ar = char_repr(char_a)
            if char_b is None:
                char_bb = char_bd = char_bh = char_br = "?"
            else:
                char_bb = f"0b{char_b:08b}"
                char_bd = f"{char_b:3d}"
                char_bh = f"0x{char_b:02x}"
                char_br = char_repr(char_b)
            comparisons.append(
                "{8:<3} Offset {0:4d}: "
                "{1:^10} {4} {5:^10} | "
                "{2:>3} {4} {6:>3} | "
                "{3:^4} {4} {7:^4} | {9:>3} {4} {10:>3}".format(
                    offset,
                    char_ab,
                    char_ad,
                    char_ah,
                    comp,
                    char_bb,
                    char_bd,
                    char_bh,
                    marker,
                    char_ar,
                    char_br,
                )
            )
        raise AssertionError(
            "Bytes differ!\n"
            + "type(a)=%s, type(b)=%s\n" % (type_a, type_b)
            + "\nIndividual bytes:\n"
            + "\n".join(comparisons)
        )
