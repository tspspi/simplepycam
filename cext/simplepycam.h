#define PYCAMERA_STATEFLAG__CROPSUPPORTED			0x00000001
#define PYCAMERA_STATEFLAG__CROPSETSUPPORTED	0x00000002
#define PYCAMERA_STATEFLAG__STREAMING					0x80000000

struct pyCamera_Instance {
    PyObject_HEAD

    char*       									lpDeviceName;
    int         									hHandle;

		uint32_t											stateFlags;
		unsigned long int							dwBufferedFrames;

		PyObject*											cbFrameCallback;

		/*
			Capabilities queried from v4l API

			These are used to fill the capabilities structure
		*/
		char*													lpCaps_Driver;
		char*													lpCaps_Card;
		char*													lpCaps_BusInfo;
		uint32_t											dwCaps_Version;
		uint32_t											dwCaps_CapabilityFlags;
		uint32_t											dwCaps_DeviceCapabilityFlags;

		struct v4l2_cropcap						cropCap;
		struct v4l2_crop							cropCurrent;

		/*
			Streaming status
		*/
		struct v4l2_requestbuffers		rqBuffers;
		struct pyCamera_ImageBuffer*	imageBuffers;
};

struct pyCamera_ImageBuffer {
	void* lpBase;
	ssize_t len;
};
