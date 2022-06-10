from distutils.core import setup, Extension

setup(
    name="simplepycam",
    version="0.1",
    ext_modules=[
        Extension("simplepycam", ["simplepycam.c"], include_dirs=["/usr/local/include"])
    ]
)
