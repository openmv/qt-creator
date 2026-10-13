"""
This is a Python script for translating .ts files from one language to another.
It makes use of the translators library to translate individual strings in the file,
updating the file with the new translations.
"""

import sys
import time
import xml.etree.ElementTree
import translators


def replace_first_lines(file_path):
    """
    Replaces the first two lines of a file with the XML declaration and DOCTYPE.

    :param file_path: The path to the file to modify.
    :type file_path: str
    """
    with open(file_path, 'r+', encoding='utf-8') as file:
        lines = file.readlines()
        lines[0] = '<?xml version="1.0" encoding="utf-8"?>\n'
        lines.insert(1, '<!DOCTYPE TS>\n')

        file.seek(0)
        file.writelines(lines)
        file.truncate()


def translate_string(source_string: str, source_language: str, target_language: str) -> str:
    """
    ...

    :param source_string: The string to translate.
    :type source_string: str
    :param source_language: The ISO 639-1 code of the language to translate from.
    :type source_language: str
    :param target_language: The ISO 639-1 code of the language to translate to.
    :type target_language: str
    :return: The translated string.
    :rtype: str
    """
    start_time = time.time()
    output = translators.google(source_string, source_language, target_language)
    print(
        f"translateString: {time.time() - start_time}s : {source_string} -> {output} "
        f"({source_language} -> {target_language})"
    )

    return output


def transform_ts_file(ts_file_path, _language, target_language):
    """
    Transforms a .ts file by translating all 'unfinished' messages.
    The translated messages replace the original messages in the .ts file.

    :param ts_file_path: The path to the .ts file to transform.
    :type ts_file_path: str
    :param _language: The ISO 639-1 code of the source language.
    :type _language: str
    :param target_language: The ISO 639-1 code of the target language.
    :type target_language: str
    """
    tree = xml.etree.ElementTree.parse(ts_file_path)
    root = tree.getroot()

    for message in root.iter('message'):
        numerus = message.attrib.get('numerus') == 'yes'
        if numerus:
            source_text = message.find('source').text
            unfinished_translation = message.find('translation')
            if unfinished_translation is not None and \
                    unfinished_translation.attrib.get('type') == 'unfinished':
                try:
                    translated_text = translate_string(source_text, _language, target_language)
                except TypeError as e:
                    print(e)
                    continue
                for num_form in unfinished_translation.findall('numerusform'):
                    unfinished_translation.remove(num_form)  # Remove existing empty numerusforms
                for _ in range(2):  # assuming two forms, singular and plural
                    new_numerusform = xml.etree.ElementTree.SubElement(unfinished_translation,
                                                                       'numerusform')
                    new_numerusform.text = translated_text
                del unfinished_translation.attrib['type']
        else:
            translation = message.find('translation')
            if translation is not None and translation.attrib.get('type') == 'unfinished':
                source_text = message.find('source').text
                try:
                    translated_text = translate_string(source_text, _language, target_language)
                except TypeError as e:
                    print(e)
                    continue
                translation.text = translated_text
                del translation.attrib['type']

    tree.write(ts_file_path, encoding='utf-8', xml_declaration=True)
    replace_first_lines(ts_file_path)  # Replace the first two lines of the output file
    with open(ts_file_path, 'a', encoding='utf-8') as file:  # Preserve the last empty line
        file.write('\n')

    print("TS file transformed successfully.")


def main():
    """
    The main entry point of the script. It checks if a file path was given as
    command line argument and if so, calls the function to transform the .ts file.
    """
    if len(sys.argv) < 4:
        print("Usage: python script.py <ts_file_path> sourceLanguage targetLanguage")
        return

    ts_file_path = sys.argv[1]
    start_time = time.time()
    transform_ts_file(ts_file_path, sys.argv[2], sys.argv[3])
    print(f"Whole execution took {time.time() - start_time}s.")


if __name__ == "__main__":
    main()

# test call:
# python auto_trans.py testing/helloworld.ts en cn
#
# Using Germany server backend.
# translateString: 1.3896245956420898s : Hello world! -> 你好世界！ (en -> cn)
# translateString: 1.9492523670196533s : My first dish. -> 我的第一道菜。 (en -> cn)
# translateString: 2.112003803253174s : white bread with butter -> 白面包和黄油 (en -> cn)
# TS file transformed successfully.
# Whole execution took 5.453961610794067s.
# (venv) [mpetrick@marcel-precision3551 AutoTrans]$

# manual tests:
# python auto_trans.py testing/numerus.ts de cn
# python auto_trans.py testing/helloworld.ts en cn
