from distutils.core import setup, Extension

setup(
    name="simplepycam",
    version="0.1",
	author='Thomas Spielauer',
	description='Simple Python native access library for Video4Linux camera devices',
	long_description='file:README.md',
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
