"""
Unit tests for auto_trans.py.
These tests cover the functions replace_first_lines, translate_string and transform_ts_file.
"""

import unittest
from unittest.mock import patch, MagicMock
from xml.etree import ElementTree

import auto_trans


class AutoTransTest(unittest.TestCase):
    """
    Test class for the auto_trans module.
    """

    def setUp(self):
        """
        Set up anything that is necessary for the test environment.
        """

    @patch("builtins.open", new_callable=MagicMock)
    def test_replace_first_lines(self, mock_open):
        """
        Test that replace_first_lines replaces the first two lines of a file correctly.
        """
        auto_trans.replace_first_lines("fakepath")
        mock_open.assert_called_with("fakepath", 'r+', encoding='utf-8')

    @patch("translators.google", return_value="你好世界")
    def test_translate_string(self, mock_google):
        """
        Test that translate_string calls the appropriate translation function
        and returns the correct result.
        """
        result = auto_trans.translate_string("Hello world", "en", "cn")
        self.assertEqual(result, "你好世界")
        mock_google.assert_called_once_with("Hello world", "en", "cn")

    @patch("xml.etree.ElementTree.parse")
    @patch("auto_trans.translate_string", return_value="你好世界")
    @patch("auto_trans.replace_first_lines")
    def test_transform_ts_file(self, mock_replace_first_lines, mock_translate_string, mock_parse):
        """
        Test that transform_ts_file updates a .ts file correctly.
        """
        fake_tree = ElementTree.ElementTree(ElementTree.Element("TS"))
        fake_msg = ElementTree.Element("message")
        fake_source = ElementTree.Element("source")
        fake_source.text = "Hello world"
        fake_translation = ElementTree.Element("translation", attrib={"type": "unfinished"})
        fake_msg.extend([fake_source, fake_translation])
        fake_tree.getroot().append(fake_msg)
        mock_parse.return_value = fake_tree

        auto_trans.transform_ts_file("fakepath", "en", "cn")

        mock_translate_string.assert_called_once_with("Hello world", "en", "cn")
        mock_replace_first_lines.assert_called_once_with("fakepath")
        self.assertIsNone(fake_translation.attrib.get('type'))
        self.assertEqual(fake_translation.text, "你好世界")


if __name__ == "__main__":
    unittest.main()
