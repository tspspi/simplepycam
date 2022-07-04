from distutils.core import setup, Extension

from pathlib import Path
this_directory = Path(__file__).parent
long_description = (this_directory / "README.md").read_text()

setup(
    name="simplepycam",
    version="0.1",
	author='Thomas Spielauer',
	description='Simple Python native access library for Video4Linux camera devices',
	long_description=long_description,
	long_description_content_type='text/markdown',
	license_files='../LICENSE.md',
	url='https://github.com/tspspi/simplepycam',

	python_requires='>=3.6',
	license='BSD',

	classifiers=[
		'Programming Language :: Python :: 3',

		'License :: OSI Approved :: BSD License',

		'Operating System :: POSIX',
		'Operating System :: POSIX :: BSD :: FreeBSD',

		'Programming Language :: C',

		'Topic :: Multimedia :: Video :: Capture',

		'Development Status :: 3 - Alpha'
	],

    ext_modules=[
        Extension("simplepycam", ["simplepycam.c"], include_dirs=["/usr/local/include"])
    ]
)
