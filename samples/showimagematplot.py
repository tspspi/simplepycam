import matplotlib.pyplot as plt
import simplepycam

with simplepycam.Camera("/dev/video0") as cam:
	cam.streamOn()
	frame = cam.nextFrame()
	cam.streamOff()
	plt.imshow(frame)
	plt.show()
