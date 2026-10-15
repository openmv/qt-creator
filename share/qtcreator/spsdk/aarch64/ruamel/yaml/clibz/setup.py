
from setuptools import Extension
from setuptools import setup

base = 'https://sourceforge.net/p/ruamel-yaml-clibz/'

setup(
    name='ruamel.yaml.clibz',
    version='0.3.1',
    python_requires='>=3.9',
    build_zig=True,
    author='Anthon van der Neut',
    author_email='a.van.der.neut@ruamel.eu',
    description='C version of reader, parser and emitter for ruamel.yaml, compiled with Zig,'
                ' derived from libyaml',
    long_description=open("README.md").read(),
    long_description_content_type="text/markdown",
    project_urls=dict(
        Home=base,
        Source=f'{base}code/ci/default/tree/',
        Tracker=f'{base}tickets/',
        Documentation='https://yaml.dev/doc/ruamel.yaml.clibz',
    ),
    license='MIT',
    ext_modules=[Extension(
        name='_ruamel_yaml_clibz',
        sources=[
            '_ruamel_yaml_clibz.c',
            'api.c',
            'writer.c',
            'dumper.c',
            'loader.c',
            'reader.c',
            'scanner.c',
            'parser.c',
            'emitter.c',
        ],
        extra_compile_args=[
            # '-O', 'Debug',
        ],
    )],
    setup_requires=['setuptools-zig>=0.4.2', 'ziglang<0.16'],
)
