from distutils.core import setup, Extension

from pathlib import Path
this_directory = Path(__file__).parent
long_description = (this_directory / "README.rst").read_text()

setup(
    name="simplepycam-tspspi",
    version="0.1.5",
	author='Thomas Spielauer',
    author_email="pypipackages01@tspi.at",
	description='Simple Python native access library for Video4Linux camera devices',
	long_description=long_description,
#	long_description_content_type='text/markdown',
	license_files='LICENSE.md',
	url='https://github.com/tspspi/simplepycam',
    package_data={
        'simplepycam-tspspi' : [ 'simplepycam.h' ]
    },
    include_package_data=True,

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

    setup_requires=[
        "setuptools-git-versioning<2"
    ],

    setuptools_git_versioning={
        "enabled" : True
    },

    ext_modules=[
        Extension("simplepycam", ["simplepycam.c"], include_dirs=["/usr/local/include", "."])
    ]
)
