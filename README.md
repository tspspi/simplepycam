# Simple Python native access library for Video4Linux camera devices

_Warning_: Work (slowly) in progress. Currently hardwired to YUV format. This will be
fixed soon

This is a simple library that allows one to access camera devices accessible
via Video4Linux from Python without using libraries with huge dependency chains
that allows easy and fast testing of ideas and algorithms for image processing
before implementing them in a proper way in some programming language such as C.
It's built in the most simple way on purpose.

Tested on:

* Python 3.8:
	* FreeBSD 12 (amd64, aarch64)
	* FreeBSD 13 (amd64, aarch64)

# Building

Use ```setup.py``` in ```cext```:

```
$ python setup.py build
```

# Example usage

More sophisticated examples can be found in the ```samples``` directory.

## Using the stream callback interface, the default format and with

```
import simplepycam

def processFrame(camera, frame):
	if shouldStopProcessing:
		return False
	else:
		return True

with simplepycam.Camera("/dev/video0") as cam:
	cam.frameCallback = [ processFrame ]
	cam.stream()
```

## Using the stream callback interface, the default format and open/close

```
import simplepycam

def processFrame(camera, frame):
	if shouldStopProcessing:
		return False
	else:
		return True

cam = simplepycam.Camera("/dev/video0")
cam.open()
cam.frameCallback = [ processFrame ]
cam.stream()
cam.close()
```

## Using the polling API, the default format and with

```
import simplepycam

with simplepycam.Camera("/dev/video0") as cam:
	cam.streamOn()
	for i in range(100):
		frame = cam.nextFrame()
	cam.streamOff()
```

## Using the polling API, the default format and open/close

```
import simplepycam

cam = simplepycam.Camera("/dev/video0")
cam.open()
cam.streamOn()
for i in range(100):
	frame = cam.nextFrame()
cam.streamOff()
cam.close()
```
