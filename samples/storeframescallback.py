from PIL import Image
import sys

sys.path.append("./build/lib.freebsd-12.3-RELEASE-p5-amd64-3.8")

import simplepycam

nStoredImages = 0
nImagesToStore = 100

def cbStoreImage(camera, frame):
	global nStoredImages
	print("Storing image {} of {}".format(nStoredImages, nImagesToStore))

	# Transform frame into flatter array so PIL is able to process it
	newPilImage = []
	for row in range(len(frame)):
		for col in range(len(frame[row])):
			newPilImage.append(frame[row][col])

	im = Image.new(mode="RGB", size=(len(frame[0]), len(frame)))
	im.putdata(newList)
	im.save("image{}.jpg".format(nStoredImages))

	nStoredImages = nStoredImages + 1
	if(nStoredImages >= nImagesToStore):
		return False

	return True

with simplepycam.Camera("/dev/video0") as cam:
	cam.frameCallback = [ cbStoreImage ]
	cam.stream()
