"""
Type instances should have raw-bytes easily available without the "type/length" header.
"""
import pytest

import x690.types as t


@pytest.mark.parametrize("cls", t.X690Type.all())
def test_raw_bytes(cls):
    try:
        instance = cls.decode(b"")
    except NotImplementedError:
        raise pytest.skip("Not yet implemented")
    assert instance.raw_bytes == b""


@pytest.mark.parametrize("cls", t.X690Type.all())
def test_raw_bytes(cls):
    instance = cls()
    assert isinstance(instance.raw_bytes, bytes)
