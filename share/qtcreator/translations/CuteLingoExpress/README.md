# CuteLingoExpress
CuteLingoExpress is a powerful tool designed to facilitate the translation of Qt ts-files for internationalization. It automates the translation process by allowing users to specify the source and target language, providing a preview of how the translated layouts will appear. This tool is particularly useful for ensuring that the app's interface is well-suited for different languages before engaging native speakers for final translations, saving time in the development cycle.

## Motivation
Internationalization plays a crucial role in developing successful applications, as not all customers are comfortable with English. Qt provides a comprehensive ecosystem for handling internationalization, including language support in C++/Qt and various tools like lupdate and Linguist. However, one missing aspect has been the ability to automatically generate translations and preview them in the context of the app's layouts. CuteLingoExpress fills this gap by automating the translation process and providing developers with a quick and convenient way to assess layout compatibility.

# Usage

## Setup
To use CuteLingoExpress, start by installing the required packages specified in 
`pip install requirements.txt`  
Please note that the tool currently supports the translators package version 5.3.1, as the newest version may not be compatible.

## Invocation
CuteLingoExpress is invoked by providing the path to the ts-file that needs translation and the ISO 639-1 country codes for the source and target languages. For more information about ISO 639-1 country codes, refer to the documentation available at https://pypi.org/project/translators/. The following examples demonstrate the usage:   
```shell
python auto_trans.py testing/numerus.ts de cn
python auto_trans.py testing/helloworld.ts en cn
```
Upon execution, the tool will perform the translations and update the ts-file in-place. An example of the output could be as follows:    
```shell
$ python auto_trans.py testing/helloworld.ts en cn
Using Germany server backend.
translateString: 0.5832037925720215s : Hello world! -> 你好世界！ (en -> cn)
translateString: 1.0015525817871094s : My first dish. -> 我的第一道菜。 (en -> cn)
translateString: 1.534256935119629s : white bread with butter -> 白面包和黄油 (en -> cn)
TS file transformed successfully.
Whole execution took 3.1223082542419434s.
```

## Handling errors
* Sometimes, the chosen backend for translation, Google, may fail to start in approximately 20% of the runs. If this occurs, you can press Ctrl+C to stop the execution and retry the translation.
* Yandex and DeepL were also quite powerful, but I ran quickly into rate-limitations (watch the output).

## Checking results
* To assess the translated content, it is recommended to use the diff command from your preferred version-control system. This allows you to compare the changes made in the ts-file and verify the accuracy of the translations.
![](comparison_cn.png)  

### Additional Features
* CuteLingoExpress also handles the numerus form, providing support for translation involving plurals and singulars.
* During development, a key goal was to preserve the original file structure to minimize the differences when comparing versions. This approach ensures that the changes made during translation are easily identifiable.

## Software quality
* Please execute the tests in `test_auto_trans.py` to check for regressions.  
* `pylint` gives it a rating of 10/10.

# Naming?
* The name "CuteLingoExpress" combines elements from different aspects of the tool to convey its purpose and characteristics. It blends "cute" from Qt, "lingo" representing the language translation aspect, and "express" to emphasize the tool's speed and efficiency in translating Qt content. This name reflects the tool's goal of delivering delightful and rapid translations while capturing the essence of the Qt framework.
* The development of CuteLingoExpress involved applying design-thinking methodologies, leveraging GPT to enhance the overall user experience and refine the translation workflow.
