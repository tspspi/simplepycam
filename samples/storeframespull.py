from PIL import Image
import simplepycam

nImagesToStore = 5

with simplepycam.Camera("/dev/video0") as cam:
	cam.streamOn()

	for i in range(nImagesToStore):
		frame = cam.nextFrame()
		print("Storing image {} of {}".format(i, nImagesToStore))

		newPilImage = []
		for row in range(len(frame)):
			for col in range(len(frame[row])):
				newPilImage.append(frame[row][col])
		im = Image.new(mode="RGB", size=(len(frame[0]), len(frame)))
		im.putdata(newPilImage)
		im.save("image{}.jpg".format(i))

	cam.streamOff()
