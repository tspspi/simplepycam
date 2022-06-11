#ifndef SIMPLEPYCAM_BUFFERED_FRAMES__DEFAULT
	#define SIMPLEPYCAM_BUFFERED_FRAMES__DEFAULT 1
#endif

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/mman.h>

#include <linux/videodev2.h>

#include "Python.h"
#include "./simplepycam.h"


static int xioctl(int fh, int request, void *arg) {
	int r;
	do {
		r = ioctl(fh, request, arg);
	} while ((r == -1) && (errno == EINTR));
	return r;
}

static PyModuleDef simplepycamModuleDefinition = {
    PyModuleDef_HEAD_INIT,

    .m_name = "simplepycam",
    .m_doc = "Simple V4L camera access from Python",
    .m_size = -1
};

/*
    Camera object
*/

static PyObject* pyCamera_New(
    PyTypeObject* type,
    PyObject* args,
    PyObject* kwds
) {
    struct pyCamera_Instance* lpNewInstance;
    lpNewInstance = (struct pyCamera_Instance*)type->tp_alloc(type, 0);
    if(lpNewInstance == NULL) {
        return NULL;
    }

    lpNewInstance->lpDeviceName     						= NULL;
    lpNewInstance->hHandle          						= -1;
		lpNewInstance->stateFlags										= 0;

		lpNewInstance->lpCaps_Driver								= NULL;
		lpNewInstance->lpCaps_Card									= NULL;
		lpNewInstance->lpCaps_BusInfo								= NULL;
		lpNewInstance->dwCaps_Version								= 0;
		lpNewInstance->dwCaps_CapabilityFlags				= 0;
		lpNewInstance->dwCaps_DeviceCapabilityFlags	= 0;

		lpNewInstance->dwBufferedFrames							= SIMPLEPYCAM_BUFFERED_FRAMES__DEFAULT;
		lpNewInstance->cbFrameCallback							= NULL;

    return (PyObject*)lpNewInstance;
}

static char* pyCamera_Init__KWList[] = { "dev", NULL };
static char* pyCamera_Init__DefaultCameraFile = "/dev/video0";

static int pyCamera_Init(
    struct pyCamera_Instance* lpSelf,
    PyObject* args,
    PyObject* kwds
) {
		struct stat fileStat;
    char* lpArg_Dev = NULL;

		if((lpSelf->stateFlags & PYCAMERA_STATEFLAG__STREAMING) != 0) {
			PyErr_SetString(PyExc_ValueError, "Streaming is running, cannot re-initialize the camera without stopping");
			return -1;
		}

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "|s", pyCamera_Init__KWList, &lpArg_Dev)) {
        return -1;
    }

		/* ToDo: Check if handle is opened. If close it (after optionally also stopping streaming) */
    if(lpSelf->lpDeviceName != NULL) 			{ free(lpSelf->lpDeviceName); lpSelf->lpDeviceName = NULL; }
		if(lpSelf->lpCaps_Driver != NULL) 		{ free(lpSelf->lpCaps_Driver); lpSelf->lpCaps_Driver = NULL; }
		if(lpSelf->lpCaps_Card != NULL) 			{ free(lpSelf->lpCaps_Card); lpSelf->lpCaps_Card = NULL; }
		if(lpSelf->lpCaps_BusInfo != NULL) 		{ free(lpSelf->lpCaps_BusInfo); lpSelf->lpCaps_BusInfo = NULL; }
		lpSelf->dwCaps_Version 								= 0;
		lpSelf->dwCaps_CapabilityFlags 				= 0;
		lpSelf->dwCaps_DeviceCapabilityFlags 	= 0;
		lpSelf->stateFlags 										= 0;

		lpSelf->dwBufferedFrames							= SIMPLEPYCAM_BUFFERED_FRAMES__DEFAULT;

		if(lpSelf->cbFrameCallback != NULL) {
			Py_DECREF(lpSelf->cbFrameCallback);
			lpSelf->cbFrameCallback = NULL;
		}

    if(lpArg_Dev == NULL) {
				if(stat(pyCamera_Init__DefaultCameraFile, &fileStat) != 0) {
					PyErr_SetString(PyExc_FileNotFoundError, pyCamera_Init__DefaultCameraFile);
					return -1;
				}
				if(!S_ISCHR(fileStat.st_mode)) {
					PyErr_SetString(PyExc_FileNotFoundError, pyCamera_Init__DefaultCameraFile);
					return -1;
				}

        lpSelf->lpDeviceName = (char*)malloc(sizeof(char) * strlen(pyCamera_Init__DefaultCameraFile) + 1);
        if(lpSelf->lpDeviceName == NULL) {
						PyErr_SetNone(PyExc_MemoryError);
						return -1;
        }
        strcpy(lpSelf->lpDeviceName, pyCamera_Init__DefaultCameraFile);
    } else {
				if(stat(lpArg_Dev, &fileStat) != 0) {
					PyErr_SetString(PyExc_FileNotFoundError, lpArg_Dev);
					return -1;
				}
				if(!S_ISCHR(fileStat.st_mode)) {
					PyErr_SetString(PyExc_FileNotFoundError, lpArg_Dev);
					return -1;
				}

        lpSelf->lpDeviceName = (char*)malloc(sizeof(char) * strlen(lpArg_Dev) + 1);
        if(lpSelf->lpDeviceName == NULL) {
						PyErr_SetNone(PyExc_MemoryError);
						return -1;
        }
        strcpy(lpSelf->lpDeviceName, lpArg_Dev);
    }

    return 0;
}

static void pyCamera_Dealloc(
    struct pyCamera_Instance* lpSelf
) {
    if(lpSelf->lpDeviceName != NULL) { free(lpSelf->lpDeviceName); lpSelf->lpDeviceName = NULL; }
		/* ToDo: If streaming is running stop and release buffers before releasing the handle ... */
		if(lpSelf->hHandle != -1) { close(lpSelf->hHandle); lpSelf->hHandle = -1; }

		if(lpSelf->lpCaps_Driver != NULL) { free(lpSelf->lpCaps_Driver); lpSelf->lpCaps_Driver = NULL; }
		if(lpSelf->lpCaps_Card != NULL) { free(lpSelf->lpCaps_Card); lpSelf->lpCaps_Card = NULL; }
		if(lpSelf->lpCaps_BusInfo != NULL) { free(lpSelf->lpCaps_BusInfo); lpSelf->lpCaps_BusInfo = NULL; }
		lpSelf->dwCaps_Version = 0;
		lpSelf->dwCaps_CapabilityFlags = 0;
		lpSelf->dwCaps_DeviceCapabilityFlags = 0;

		if(lpSelf->cbFrameCallback) { Py_DECREF(lpSelf->cbFrameCallback); lpSelf->cbFrameCallback = NULL; }

    PyTypeObject* lpType = Py_TYPE(lpSelf);
    Py_TYPE(lpSelf)->tp_free((PyObject*)lpSelf);

    Py_DECREF(lpType);
}

static char* errString_AlreadyOpenedCamera = "Camera already opened";
static char* errString_CameraNotOpened = "Camera not opened";

static PyObject* pyCamera_Open(
	PyObject* lpSelf,
	PyObject *args
) {
	struct v4l2_capability cap;

	struct pyCamera_Instance* lpThis = (struct pyCamera_Instance*)lpSelf;

	/* In case we already have an opened handle fail - this method is not idempotent */
	if(lpThis->hHandle != -1) {
		PyErr_SetString(PyExc_ValueError, errString_AlreadyOpenedCamera);
		return NULL;
	}

	/* Try to open the device file ... */
	lpThis->hHandle = open(lpThis->lpDeviceName, O_RDWR | O_NONBLOCK, 0);
	if(lpThis->hHandle < 0) {
		switch(errno) {
			case EACCES:
			case EPERM:
				PyErr_SetString(PyExc_PermissionError, lpThis->lpDeviceName);
				return NULL;
			default:
				PyErr_SetString(PyExc_BaseException, lpThis->lpDeviceName);
				return NULL;
		}
	}

	/* Query camera capabilities */
	if(xioctl(lpThis->hHandle, VIDIOC_QUERYCAP, &cap) == -1) {
		close(lpThis->hHandle); lpThis->hHandle = -1;
		PyErr_SetString(PyExc_ValueError, "Failed to query camera capabilities");
		return NULL;
	}

	/* Verify this device supports capture */
	if((cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) == 0) {
		close(lpThis->hHandle); lpThis->hHandle = -1;
		PyErr_SetString(PyExc_ValueError, "Device does not support video capture");
		return NULL;
	}

	if(((cap.capabilities & V4L2_CAP_READWRITE) == 0) && ((cap.capabilities & V4L2_CAP_STREAMING) == 0)) {
		close(lpThis->hHandle); lpThis->hHandle = -1;
		PyErr_SetString(PyExc_ValueError, "Device does not support read/write nor streaming interface");
		return NULL;
	}

	/* Copy capabilities into our own cache ... */
	lpThis->dwCaps_DeviceCapabilityFlags = cap.device_caps;
	lpThis->dwCaps_CapabilityFlags = cap.capabilities;
	lpThis->dwCaps_Version = cap.version;

	{
		unsigned long int lenDriver = strlen((const char*)cap.driver);
		if(lenDriver > sizeof(cap.driver)) { lenDriver = sizeof(cap.driver); }
		unsigned long int lenCard = strlen((const char*)cap.card);
		if(lenCard > sizeof(cap.card)) { lenCard = sizeof(cap.card); }
		unsigned long int lenBusInfo = strlen((const char*)cap.bus_info);
		if(lenBusInfo > sizeof(cap.bus_info)) { lenBusInfo = sizeof(cap.bus_info); }

		if(lenDriver > 0) {
			lpThis->lpCaps_Driver = (char*)malloc((lenDriver + 1) * sizeof(char));
			if(lpThis->lpCaps_Driver == NULL) {
				PyErr_SetNone(PyExc_MemoryError);
				goto errorCleanup;
			}
		}
		if(lenCard > 0) {
			lpThis->lpCaps_Card = (char*)malloc((lenCard + 1) * sizeof(char));
			if(lpThis->lpCaps_Card == NULL) {
				PyErr_SetNone(PyExc_MemoryError);
				goto errorCleanup;
			}
		}
		if(lenBusInfo > 0) {
			lpThis->lpCaps_BusInfo = (char*)malloc((lenBusInfo + 1) * sizeof(char));
			if(lpThis->lpCaps_BusInfo == NULL) {
				PyErr_SetNone(PyExc_MemoryError);
				goto errorCleanup;
			}
		}

		if(lenCard > 0) {
			strncpy(lpThis->lpCaps_Card, (const char*)cap.card, lenCard);
			lpThis->lpCaps_Card[lenCard] = 0;
		}
		if(lenDriver > 0) {
			strncpy(lpThis->lpCaps_Driver, (const char*)cap.driver, lenDriver);
			lpThis->lpCaps_Driver[lenDriver] = 0;
		}
		if(lenBusInfo > 0) {
			strncpy(lpThis->lpCaps_BusInfo, (const char*)cap.bus_info, lenBusInfo);
			lpThis->lpCaps_BusInfo[lenBusInfo] = 0;
		}
	}

	/*
		Query cropping capabilities and set default cropping region
	*/
	{
		memset(&(lpThis->cropCap), 0, sizeof(lpThis->cropCap));
		lpThis->cropCap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if(xioctl(lpThis->hHandle, VIDIOC_CROPCAP, &(lpThis->cropCap)) != -1) {
			/* Cropping supported */
			lpThis->stateFlags = lpThis->stateFlags | PYCAMERA_STATEFLAG__CROPSUPPORTED;
			/* Set cropping region to default ... */
			lpThis->cropCurrent.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			lpThis->cropCurrent.c = lpThis->cropCap.defrect;
			if(xioctl(lpThis->hHandle, VIDIOC_S_CROP, &(lpThis->cropCurrent)) != -1) {
				/* Clear state flag and ignore ... */
				lpThis->stateFlags = lpThis->stateFlags | PYCAMERA_STATEFLAG__CROPSETSUPPORTED;
			}
		}
	}

	/* We return nothing ... */
	Py_INCREF(Py_None);
	return Py_None;

	/*
		Cleanup routines in case of errors - release everything
		and bring into uninitialized state
	*/
errorCleanup:
	if(lpThis->lpCaps_Driver != NULL) { free(lpThis->lpCaps_Driver); lpThis->lpCaps_Driver = NULL; }
	if(lpThis->lpCaps_Card != NULL) { free(lpThis->lpCaps_Card); lpThis->lpCaps_Card = NULL; }
	if(lpThis->lpCaps_BusInfo != NULL) { free(lpThis->lpCaps_BusInfo); lpThis->lpCaps_BusInfo = NULL; }
	if(lpThis->hHandle != -1) { close(lpThis->hHandle); lpThis->hHandle = -1; }
	lpThis->dwCaps_DeviceCapabilityFlags = 0;
	lpThis->dwCaps_CapabilityFlags = 0;
	lpThis->dwCaps_Version = 0;
	lpThis->stateFlags = 0;
	return NULL;
}

static PyObject* pyCamera_Close(
	PyObject* lpSelf,
	PyObject* args
) {
	struct pyCamera_Instance* lpThis = (struct pyCamera_Instance*)lpSelf;

	/* In case we already have an opened handle fail - this method is not idempotent */
	if(lpThis->hHandle == -1) {
		PyErr_SetString(PyExc_ValueError, errString_CameraNotOpened);
		return NULL;
	}

	if((lpThis->stateFlags & PYCAMERA_STATEFLAG__STREAMING) != 0) {
		PyErr_SetString(PyExc_ValueError, "Streaming is running, cannot close the camera without stopping");
		return NULL;
	}

	/* Close handle ... */
	close(lpThis->hHandle);
	lpThis->hHandle = -1;

	/* Release associated capability information */
	if(lpThis->lpCaps_Driver != NULL) { free(lpThis->lpCaps_Driver); lpThis->lpCaps_Driver = NULL; }
	if(lpThis->lpCaps_Card != NULL) { free(lpThis->lpCaps_Card); lpThis->lpCaps_Card = NULL; }
	if(lpThis->lpCaps_BusInfo != NULL) { free(lpThis->lpCaps_BusInfo); lpThis->lpCaps_BusInfo = NULL; }
	lpThis->dwCaps_Version = 0;
	lpThis->dwCaps_CapabilityFlags = 0;
	lpThis->dwCaps_DeviceCapabilityFlags = 0;
	lpThis->stateFlags = 0;

	/* We return nothing ... */
	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* pyCamera_Enter(
	PyObject* lpSelf,
	PyObject* args
) {
	pyCamera_Open(lpSelf, args);

	return lpSelf;
}

static PyObject* pyCamera_Exit(
	PyObject* lpSelf,
	PyObject* args
) {
	pyCamera_Close(lpSelf, args);

	Py_INCREF(Py_None);
	return Py_None;
}

static PyObject* pyCamera__StreamOrNextFrame__ProcessFrame(
	struct pyCamera_Instance* lpThis,
	struct pyCamera_ImageBuffer* imageBuffers,
	struct v4l2_buffer* buf
) {
	unsigned long int row, col;

	PyObject* resultImage = PyList_New(lpThis->cropCurrent.c.height);
	if(resultImage == NULL) {
		PyErr_SetNone(PyExc_MemoryError);
		return NULL;
	}

	for(row = 0; row < lpThis->cropCurrent.c.height; row = row + 1) {
		PyObject* newRow = PyList_New(lpThis->cropCurrent.c.width);
		if(newRow == NULL) {
			Py_DECREF(resultImage);
			PyErr_SetNone(PyExc_MemoryError);
			return NULL;
		}
		PyList_SetItem(resultImage, row, newRow);
	}

	/* Currently for testing: Assume we have a YUV frame and decode to RGB ... */
	for(row = 0; row < lpThis->cropCurrent.c.height; row = row + 1) {
		PyObject* lpCurRow = PyList_GetItem(resultImage, row);
		for(col = 0; col < lpThis->cropCurrent.c.width; col = col + 1) {
			unsigned char y0, y1, y;
			unsigned char u0, v0;

			signed int c,d,e;
			unsigned char r,g,b;
			signed int rtmp,gtmp, btmp;

			y0 = ((unsigned char*)(imageBuffers[buf->index].lpBase))[((col + row * lpThis->cropCurrent.c.width) >> 1)*4 + 0];
			u0 = ((unsigned char*)(imageBuffers[buf->index].lpBase))[((col + row * lpThis->cropCurrent.c.width) >> 1)*4 + 1];
			y1 = ((unsigned char*)(imageBuffers[buf->index].lpBase))[((col + row * lpThis->cropCurrent.c.width) >> 1)*4 + 2];
			v0 = ((unsigned char*)(imageBuffers[buf->index].lpBase))[((col + row * lpThis->cropCurrent.c.width) >> 1)*4 + 3];

			if((col + row * lpThis->cropCurrent.c.width) % 2 == 0) {
				y = y0;
			} else {
				y = y1;
			}

			c = ((signed int)y) - 16;
			d = ((signed int)u0) - 128;
			e = ((signed int)v0) - 128;

			rtmp = ((298 * c + 409 * e + 128) >> 8);
			gtmp = ((298 * c - 100 * d - 208 * e + 128) >> 8);
			btmp = ((298 * c + 516 * d + 128) >> 8);

			if(rtmp < 0) { r = 0; }
			else if(rtmp > 255) { r = 255; }
			else { r = (unsigned char)rtmp; }

			if(gtmp < 0) { g = 0; }
			else if(gtmp > 255) { g = 255; }
			else { g = (unsigned char)gtmp; }

			if(btmp < 0) { b = 0; }
			else if(btmp > 255) { b = 255; }
			else { b = (unsigned char)btmp; }

			PyObject* lpNewRGBImage = PyTuple_New(3);
			if(lpNewRGBImage == NULL) {
				Py_DECREF(resultImage);
				PyErr_SetNone(PyExc_MemoryError);
				return NULL;
			}
			PyTuple_SetItem(lpNewRGBImage, 0, PyLong_FromUnsignedLong(r));
			PyTuple_SetItem(lpNewRGBImage, 1, PyLong_FromUnsignedLong(g));
			PyTuple_SetItem(lpNewRGBImage, 2, PyLong_FromUnsignedLong(b));
			PyList_SetItem(lpCurRow, col, lpNewRGBImage);
		}
	}

	return resultImage;
}

static PyObject* pyCamera_Stream(
	PyObject* lpSelf,
	PyObject *args
) {
	unsigned long int i;
	struct pyCamera_Instance* lpThis = (struct pyCamera_Instance*)lpSelf;
	struct v4l2_requestbuffers rqBuffers;
	struct pyCamera_ImageBuffer* imageBuffers = NULL;
	PyObject* lpCallback_Args = NULL;
	PyObject* lpCallback_KWargs = NULL;


	/*
		Start streaming:
			* Allocate buffers (might trigger out of memory)
			* Enqueue all buffers
			* STREAMON
			* Run event in endless event loop
			* On every frame call registered callback (note this function can only be invoked with at least one callback being present)
				* In case at least one callback signals we should stop (returnung "false") we stop the event loop
	*/

	if((lpThis->stateFlags & PYCAMERA_STATEFLAG__STREAMING) != 0) {
		PyErr_SetString(PyExc_ValueError, "Streaming is already enabled, cannot start streaming a second time");
		return NULL;
	}

	if(lpThis->cbFrameCallback == NULL) {
		PyErr_SetString(PyExc_ValueError, "At least one callback has to be registered to run streaming in callback mode");
		return NULL;
	}

	if(PyList_Check(lpThis->cbFrameCallback)) {
		if(PyList_Size(lpThis->cbFrameCallback) == 0) {
			PyErr_SetString(PyExc_ValueError, "At least one callback has to be registered to run streaming in callback mode");
			return NULL;
		}

		/* Fast pre-check ... */
		for(i = 0; i < (unsigned long int)PyList_Size(lpThis->cbFrameCallback); i=i+1) {
			if(!PyCallable_Check(PyList_GetItem(lpThis->cbFrameCallback, i))) {
				PyErr_SetString(PyExc_ValueError, "At least on callback is not callable");
				return NULL;
			}
		}
	}

	if((lpThis->dwCaps_CapabilityFlags & V4L2_CAP_STREAMING) != 0) {
		/* Allocate buffers ... */
		rqBuffers.count = lpThis->dwBufferedFrames;
		rqBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		rqBuffers.memory = V4L2_MEMORY_MMAP;

		if(xioctl(lpThis->hHandle, VIDIOC_REQBUFS, &rqBuffers) == -1) {
			PyErr_SetNone(PyExc_MemoryError);
			return NULL;
		}

		lpThis->stateFlags = lpThis->stateFlags | PYCAMERA_STATEFLAG__STREAMING;

		/* Update real buffer count ... */
		lpThis->dwBufferedFrames = rqBuffers.count;

		/* Host memory allocation ... */
		imageBuffers = calloc(lpThis->dwBufferedFrames, sizeof(struct pyCamera_ImageBuffer));
		if(imageBuffers == NULL) {
			PyErr_SetNone(PyExc_MemoryError);
			goto cleanupMmapAfterError;
		}

		for(i = 0; i < lpThis->dwBufferedFrames; i=i+1) {
			imageBuffers[i].lpBase = NULL;
			imageBuffers[i].len = 0;
		}

		/* Fetch the references to the memory mapped buffers and map into userspace */
		for(i = 0; i < rqBuffers.count; i=i+1) {
			struct v4l2_buffer vBuffer;
			memset(&vBuffer, 0, sizeof(struct v4l2_buffer));
			vBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			vBuffer.memory = V4L2_MEMORY_MMAP;
			vBuffer.index = i;

			if(xioctl(lpThis->hHandle, VIDIOC_QUERYBUF, &vBuffer) == -1) {
				PyErr_SetNone(PyExc_MemoryError);
				goto cleanupMmapAfterError;
			}

			imageBuffers[i].lpBase = mmap(NULL, vBuffer.length, PROT_READ|PROT_WRITE, MAP_SHARED, lpThis->hHandle, vBuffer.m.offset);
			if(imageBuffers[i].lpBase == MAP_FAILED) {
				imageBuffers[i].lpBase = NULL;
				PyErr_SetNone(PyExc_MemoryError);
				goto cleanupMmapAfterError;
			}
			imageBuffers[i].len = vBuffer.length;
		}

		/* Enqueue buffers ... */
		for(i = 0; i < rqBuffers.count; i=i+1) {
			struct v4l2_buffer buf;
			memset(&buf, 0, sizeof(struct v4l2_buffer));
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index = i;
			if(xioctl(lpThis->hHandle, VIDIOC_QBUF, &buf) == -1) {
				PyErr_SetNone(PyExc_MemoryError);
				goto cleanupMmapAfterError;
			}
		}

		/* Run streaming loop ... */
		{
			enum v4l2_buf_type type;
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if(xioctl(lpThis->hHandle, VIDIOC_STREAMON, &type) == -1) {
				PyErr_SetString(PyExc_BaseException, "Failed to start streaming");
				goto cleanupMmapAfterError;
			}
		}

		{
			/*
				Note that here I use select over kqueue to allow the code to be portable
				to other platforms than FreeBSD. If it would be limited to FreeBSD I'd use
				kqueue for sure ...
			*/
			struct fd_set selectSetMaster;
			struct fd_set selectSetWork;
			int breakRequested = 0;

			FD_ZERO(&selectSetMaster);
			FD_ZERO(&selectSetWork);

			FD_SET(lpThis->hHandle, &selectSetMaster);

			/* Prepare parameters for callback ... */
			PyObject* lpCallback_Args = PyTuple_New(2);
			PyObject* lpCallback_KWargs = PyDict_New();
			if((lpCallback_Args == NULL) || (lpCallback_KWargs == NULL)) { PyErr_SetNone(PyExc_MemoryError); goto cleanupMmapAfterError; }

			PyTuple_SetItem(lpCallback_Args, 0, lpSelf);

			/* STREAMING LOOP */
			for(;;) {
				memcpy(&selectSetWork, &selectSetMaster, sizeof(selectSetMaster));

				int r = select(lpThis->hHandle + 1, &selectSetWork, NULL, NULL, NULL);
				if(r < 0) {
					/* Some error occured ... cleanup and exit ... */
					PyErr_SetNone(PyExc_BaseException);
					goto cleanupMmapAfterError;
				} else if(r > 0) {
					if(FD_ISSET(lpThis->hHandle, &selectSetWork)) {
						struct v4l2_buffer buf;

						/* We have captured another frame (or EOF) ... */
						memset(&buf, 0, sizeof(struct v4l2_buffer));
						buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
						buf.memory = V4L2_MEMORY_MMAP;

						if(xioctl(lpThis->hHandle, VIDIOC_DQBUF, &buf) == -1) {
							PyErr_SetNone(PyExc_IOError);
							goto cleanupMmapAfterError;
						}

						/*
							Dequeued an image buffer ... optionally do some processing (like
							colorspace conversion) if configured and call all registered callbacks
						*/
						{
							PyObject* resultImage = pyCamera__StreamOrNextFrame__ProcessFrame(lpThis, imageBuffers, &buf);
							if(resultImage == NULL) { goto cleanupMmapAfterError; }

							PyTuple_SetItem(lpCallback_Args, 1, resultImage);

							/* Run callbacks ... */
							if(PyList_Check(lpThis->cbFrameCallback)) {
								for(i = 0; i < (unsigned long int)PyList_Size(lpThis->cbFrameCallback); i=i+1) {
									PyObject* lpFunc = PyList_GetItem(lpThis->cbFrameCallback, i);
									if(!PyCallable_Check(lpFunc)) {
										Py_DECREF(resultImage);
										PyErr_SetString(PyExc_ValueError, "At least one callback has to be registered to run streaming in callback mode");
										goto cleanupMmapAfterError;
									}

									PyObject* lpR = PyObject_Call(lpFunc, lpCallback_Args, lpCallback_KWargs);

									if(lpR == NULL) {
										Py_DECREF(resultImage);
										goto cleanupMmapAfterError;
									}
									if(lpR == Py_False) { breakRequested = 1; }
									Py_DECREF(lpR);
								}
							} else if(PyCallable_Check(lpThis->cbFrameCallback)){

								PyObject* lpR = PyObject_Call(lpThis->cbFrameCallback, lpCallback_Args, lpCallback_KWargs);
								if(lpR == NULL) {
									Py_DECREF(resultImage);
									goto cleanupMmapAfterError;
								}
								if(lpR == Py_False) { breakRequested = 1; }

								Py_DECREF(lpR);
							}

							// Py_DECREF(resultImage); resultImage = NULL;
						}

						/*
							Re-enqueue buffer
						*/
						if(xioctl(lpThis->hHandle, VIDIOC_QBUF, &buf) == -1) {
							PyErr_SetNone(PyExc_IOError);
							goto cleanupMmapAfterError;
						}
					}
				} else {
					/* Timeout (should never happen) ... */
				}

				/* Has any callback requested a break ...? */
				if(breakRequested != 0) {
					break;
				}
			} /* END OF STREAMING LOOP */
		}

		/* Stop streaming */
		{
			enum v4l2_buf_type type;
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if(xioctl(lpThis->hHandle, VIDIOC_STREAMOFF, &type) == -1) {
				PyErr_SetString(PyExc_BaseException, "Failed to stop streaming");
				goto cleanupMmapAfterError;
			}
		}

		/* Release buffers */
		for(i = 0; i < lpThis->dwBufferedFrames; i=i+1) {
			if(imageBuffers[i].lpBase != NULL) { munmap(imageBuffers[i].lpBase, imageBuffers[i].len); imageBuffers[i].lpBase = NULL; imageBuffers[i].len = 0; }
		}
		free(imageBuffers); imageBuffers = NULL;

		rqBuffers.count = 0;
		rqBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		rqBuffers.memory = V4L2_MEMORY_MMAP;
		if(xioctl(lpThis->hHandle, VIDIOC_REQBUFS, &rqBuffers) == -1) {
			PyErr_SetString(PyExc_BaseException, "Failed to release DMA buffers");
			goto cleanupMmapAfterError;
		}
	} /* END OF MMAP BASED STREAMING CASE */

	/* Unmap streaming flags */
	lpThis->stateFlags = lpThis->stateFlags & (~PYCAMERA_STATEFLAG__STREAMING);

	/* Done ... */
	Py_INCREF(Py_None);
	return Py_None;

cleanupMmapAfterError:
	if(lpCallback_Args != NULL) { Py_DECREF(lpCallback_Args); lpCallback_Args = NULL; }
	if(lpCallback_KWargs != NULL) { Py_DECREF(lpCallback_KWargs); lpCallback_KWargs = NULL; }

	if(imageBuffers != NULL) {
		for(i = 0; i < lpThis->dwBufferedFrames; i=i+1) {
			if(imageBuffers[i].lpBase != NULL) { munmap(imageBuffers[i].lpBase, imageBuffers[i].len); imageBuffers[i].lpBase = NULL; imageBuffers[i].len = 0; }
		}
		free(imageBuffers); imageBuffers = NULL;
	}

	rqBuffers.count = 0;
	rqBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	rqBuffers.memory = V4L2_MEMORY_MMAP;
	xioctl(lpThis->hHandle, VIDIOC_REQBUFS, &rqBuffers);

	lpThis->stateFlags = lpThis->stateFlags & (~PYCAMERA_STATEFLAG__STREAMING);

	return NULL;
}

static PyObject* pyCamera_StreamOn(
	PyObject* lpSelf,
	PyObject *args
) {
	unsigned long int i;
	struct pyCamera_Instance* lpThis = (struct pyCamera_Instance*)lpSelf;

	if((lpThis->stateFlags & PYCAMERA_STATEFLAG__STREAMING) != 0) {
		PyErr_SetString(PyExc_ValueError, "Streaming is already enabled, cannot start streaming a second time");
		return NULL;
	}

	if((lpThis->dwCaps_CapabilityFlags & V4L2_CAP_STREAMING) != 0) {
		/* Allocate buffers ... */
		lpThis->rqBuffers.count = lpThis->dwBufferedFrames;
		lpThis->rqBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		lpThis->rqBuffers.memory = V4L2_MEMORY_MMAP;

		if(xioctl(lpThis->hHandle, VIDIOC_REQBUFS, &(lpThis->rqBuffers)) == -1) {
			PyErr_SetNone(PyExc_MemoryError);
			return NULL;
		}

		lpThis->stateFlags = lpThis->stateFlags | PYCAMERA_STATEFLAG__STREAMING;

		/* Update real buffer count ... */
		lpThis->dwBufferedFrames = lpThis->rqBuffers.count;

		/* Host memory allocation ... */
		lpThis->imageBuffers = calloc(lpThis->dwBufferedFrames, sizeof(struct pyCamera_ImageBuffer));
		if(lpThis->imageBuffers == NULL) {
			PyErr_SetNone(PyExc_MemoryError);
			goto cleanupMmapAfterError;
		}

		for(i = 0; i < lpThis->dwBufferedFrames; i=i+1) {
			lpThis->imageBuffers[i].lpBase = NULL;
			lpThis->imageBuffers[i].len = 0;
		}

		/* Fetch the references to the memory mapped buffers and map into userspace */
		for(i = 0; i < lpThis->rqBuffers.count; i=i+1) {
			struct v4l2_buffer vBuffer;
			memset(&vBuffer, 0, sizeof(struct v4l2_buffer));
			vBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			vBuffer.memory = V4L2_MEMORY_MMAP;
			vBuffer.index = i;

			if(xioctl(lpThis->hHandle, VIDIOC_QUERYBUF, &vBuffer) == -1) {
				PyErr_SetNone(PyExc_MemoryError);
				goto cleanupMmapAfterError;
			}

			lpThis->imageBuffers[i].lpBase = mmap(NULL, vBuffer.length, PROT_READ|PROT_WRITE, MAP_SHARED, lpThis->hHandle, vBuffer.m.offset);
			if(lpThis->imageBuffers[i].lpBase == MAP_FAILED) {
				lpThis->imageBuffers[i].lpBase = NULL;
				PyErr_SetNone(PyExc_MemoryError);
				goto cleanupMmapAfterError;
			}
			lpThis->imageBuffers[i].len = vBuffer.length;
		}

		/* Enqueue buffers ... */
		for(i = 0; i < lpThis->rqBuffers.count; i=i+1) {
			struct v4l2_buffer buf;
			memset(&buf, 0, sizeof(struct v4l2_buffer));
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;
			buf.index = i;
			if(xioctl(lpThis->hHandle, VIDIOC_QBUF, &buf) == -1) {
				PyErr_SetNone(PyExc_MemoryError);
				goto cleanupMmapAfterError;
			}
		}

		{
			enum v4l2_buf_type type;
			type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			if(xioctl(lpThis->hHandle, VIDIOC_STREAMON, &type) == -1) {
				PyErr_SetString(PyExc_BaseException, "Failed to start streaming");
				goto cleanupMmapAfterError;
			}
		}
	}

	/* Success */
	Py_INCREF(Py_True);
	return Py_True;

cleanupMmapAfterError:
	if(lpThis->imageBuffers != NULL) {
	for(i = 0; i < lpThis->dwBufferedFrames; i=i+1) {
		if(lpThis->imageBuffers[i].lpBase != NULL) { munmap(lpThis->imageBuffers[i].lpBase, lpThis->imageBuffers[i].len); lpThis->imageBuffers[i].lpBase = NULL; lpThis->imageBuffers[i].len = 0; }
		}
		free(lpThis->imageBuffers); lpThis->imageBuffers = NULL;
	}

	lpThis->rqBuffers.count = 0;
	lpThis->rqBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	lpThis->rqBuffers.memory = V4L2_MEMORY_MMAP;
	xioctl(lpThis->hHandle, VIDIOC_REQBUFS, &(lpThis->rqBuffers));

	lpThis->stateFlags = lpThis->stateFlags & (~PYCAMERA_STATEFLAG__STREAMING);

	return NULL;
}

static PyObject* pyCamera_StreamOff(
	PyObject* lpSelf,
	PyObject *args
) {
	unsigned long int i;
	struct pyCamera_Instance* lpThis = (struct pyCamera_Instance*)lpSelf;

	int err = 0;

	if((lpThis->stateFlags & PYCAMERA_STATEFLAG__STREAMING) == 0) {
		PyErr_SetString(PyExc_ValueError, "Streaming is not enabled, cannot stop streaming");
		return NULL;
	}

	/* Stop streaming */
	{
		enum v4l2_buf_type type;
		type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		if(xioctl(lpThis->hHandle, VIDIOC_STREAMOFF, &type) == -1) {
			PyErr_SetString(PyExc_BaseException, "Failed to stop streaming");
			err = 1;
		}
	}

	if((lpThis->dwCaps_CapabilityFlags & V4L2_CAP_STREAMING) != 0) {
		/* Release buffers */
		for(i = 0; i < lpThis->dwBufferedFrames; i=i+1) {
			if(lpThis->imageBuffers[i].lpBase != NULL) { munmap(lpThis->imageBuffers[i].lpBase, lpThis->imageBuffers[i].len); lpThis->imageBuffers[i].lpBase = NULL; lpThis->imageBuffers[i].len = 0; }
		}
		free(lpThis->imageBuffers); lpThis->imageBuffers = NULL;

		lpThis->rqBuffers.count = 0;
		lpThis->rqBuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		lpThis->rqBuffers.memory = V4L2_MEMORY_MMAP;
		if(xioctl(lpThis->hHandle, VIDIOC_REQBUFS, &(lpThis->rqBuffers)) == -1) {
			if(err == 0) {
				PyErr_SetString(PyExc_BaseException, "Failed to release DMA buffers");
				err = 1;
			}
		}
	}

	/* Unmap streaming flags */
	lpThis->stateFlags = lpThis->stateFlags & (~PYCAMERA_STATEFLAG__STREAMING);

	/* Done ... */
	if(err == 0) {
		Py_INCREF(Py_None);
		return Py_None;
	} else {
		return NULL;
	}
}

static PyObject* pyCamera_NextFrame(
	PyObject* lpSelf,
	PyObject *args
) {
	unsigned long int i;
	struct pyCamera_Instance* lpThis = (struct pyCamera_Instance*)lpSelf;

	if((lpThis->stateFlags & PYCAMERA_STATEFLAG__STREAMING) == 0) {
		PyErr_SetString(PyExc_ValueError, "Streaming is not enabled, cannot query next frame");
		return NULL;
	}

	/*
		Note that here I use select over kqueue to allow the code to be portable
		to other platforms than FreeBSD. If it would be limited to FreeBSD I'd use
		kqueue for sure ...

		This is a short version of the "streaming loop" truncated to a single frame
		and without callbacks ...
	*/
	struct fd_set selectSetMaster;
	struct fd_set selectSetWork;
	PyObject* resultImage = NULL;

	FD_ZERO(&selectSetMaster);
	FD_ZERO(&selectSetWork);

	FD_SET(lpThis->hHandle, &selectSetMaster);

	memcpy(&selectSetWork, &selectSetMaster, sizeof(selectSetMaster));

	int r = select(lpThis->hHandle + 1, &selectSetWork, NULL, NULL, NULL);
	if(r < 0) {
		/* Some error occured ... cleanup and exit ... */
		PyErr_SetNone(PyExc_BaseException);
		return NULL;
	} else if(r > 0) {
		if(FD_ISSET(lpThis->hHandle, &selectSetWork)) {
			struct v4l2_buffer buf;

			/* We have captured another frame (or EOF) ... */
			memset(&buf, 0, sizeof(struct v4l2_buffer));
			buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
			buf.memory = V4L2_MEMORY_MMAP;

			if(xioctl(lpThis->hHandle, VIDIOC_DQBUF, &buf) == -1) {
				PyErr_SetNone(PyExc_IOError);
				return NULL;
			}

			/*
				Dequeued an image buffer ... optionally do some processing (like
				colorspace conversion) if configured and call all registered callbacks
			*/
			{
				/* Decode frame ... */
				resultImage = pyCamera__StreamOrNextFrame__ProcessFrame(lpThis, (lpThis->imageBuffers), &buf);
				if(resultImage == NULL) {
					return NULL;
				}

				/*
					Re-enqueue buffer
				*/
				if(xioctl(lpThis->hHandle, VIDIOC_QBUF, &buf) == -1) {
					PyErr_SetNone(PyExc_IOError);
					/* ToDo: Release captured frame ... */
					return NULL;
				}
			}
		} else {
			/* Timeout (should never happen) ... */
		}
	}

	return resultImage;
}

/*
	Custom property getters and setters
*/

static int pyCamera_Property_Immutable_Set(
	struct pyCamera_Instance* lpSelf,
	PyObject* lpValue,
	void* lpClosure
) {
	PyErr_SetString(PyExc_ValueError, "Property is immutable");
	return -1;
}


static int pyCamera_Property_Device_Set(
	struct pyCamera_Instance* lpSelf,
	PyObject* lpValue,
	void* lpClosure
) {
	if(lpSelf->hHandle != -1) {
		PyErr_SetString(PyExc_ValueError, "Device is opened, cannot set device file name");
		return -1;
	}

	if(lpValue == NULL) {
		PyErr_SetString(PyExc_TypeError, "Device file name has to be a string");
		return -1;
	}
	if(!PyUnicode_Check(lpValue)) {
		PyErr_SetString(PyExc_TypeError, "Device file name has to be a string");
		return -1;
	}
	if(PyUnicode_KIND(lpValue) != PyUnicode_1BYTE_KIND) {
		/* ToDo: Convert if possible ... */
		PyErr_SetString(PyExc_TypeError, "Device file name has to be a UTF-8 string");
		return -1;
	}

	unsigned long int dwNameLen = PyUnicode_GetLength(lpValue);
	char* lpNewName = (char*)malloc(sizeof(char) * (PyUnicode_GetLength(lpValue) + 1));
	if(lpNewName == NULL) { PyErr_SetNone(PyExc_MemoryError); return -1; }

	if(lpSelf->lpDeviceName != NULL) { free(lpSelf->lpDeviceName); lpSelf->lpDeviceName = NULL; }

	strncpy(lpNewName, PyUnicode_DATA(lpValue), PyUnicode_GetLength(lpValue));
	lpNewName[PyUnicode_GetLength(lpValue)] = 0;
	lpSelf->lpDeviceName = lpNewName;

	/* Return success */
	return 0;
}

static PyObject* pyCamera_Property_Device_Get(
	struct pyCamera_Instance* lpSelf,
	void* lpClosure
) {
	PyObject* lpDevString = PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, lpSelf->lpDeviceName, strlen(lpSelf->lpDeviceName));
	if(lpDevString == NULL) { PyErr_SetNone(PyExc_MemoryError); return NULL; }

	return lpDevString;
}

static PyObject* pyCamera_Property_Driver_Get(
	struct pyCamera_Instance* lpSelf,
	void* lpClosure
) {
	if(lpSelf->hHandle == -1) {
		PyErr_SetString(PyExc_ValueError, "Device not opened, cannot get driver name");
		return NULL;
	}

	if(lpSelf->lpCaps_Driver == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	PyObject* lpPyStr = PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, lpSelf->lpCaps_Driver, strlen(lpSelf->lpCaps_Driver));
	if(lpPyStr == NULL) { PyErr_SetNone(PyExc_MemoryError); return NULL; }
	return lpPyStr;
}

static PyObject* pyCamera_Property_Card_Get(
	struct pyCamera_Instance* lpSelf,
	void* lpClosure
) {
	if(lpSelf->hHandle == -1) {
		PyErr_SetString(PyExc_ValueError, "Device not opened, cannot get card name");
		return NULL;
	}

	if(lpSelf->lpCaps_Card == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	PyObject* lpPyStr = PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, lpSelf->lpCaps_Card, strlen(lpSelf->lpCaps_Card));
	if(lpPyStr == NULL) { PyErr_SetNone(PyExc_MemoryError); return NULL; }
	return lpPyStr;
}

static PyObject* pyCamera_Property_BusInfo_Get(
	struct pyCamera_Instance* lpSelf,
	void* lpClosure
) {
	if(lpSelf->hHandle == -1) {
		PyErr_SetString(PyExc_ValueError, "Device not opened, cannot get bus information");
		return NULL;
	}

	if(lpSelf->lpCaps_BusInfo == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	PyObject* lpPyStr = PyUnicode_FromKindAndData(PyUnicode_1BYTE_KIND, lpSelf->lpCaps_BusInfo, strlen(lpSelf->lpCaps_BusInfo));
	if(lpPyStr == NULL) { PyErr_SetNone(PyExc_MemoryError); return NULL; }
	return lpPyStr;
}

static PyObject* pyCamera_Property_CropBounds_Get(
	struct pyCamera_Instance* lpSelf,
	void* lpClosure
) {
	unsigned long int i;

	if(lpSelf->hHandle == -1) {
		PyErr_SetString(PyExc_ValueError, "Device not opened, cannot get cropping information");
		return NULL;
	}
	if((lpSelf->stateFlags & PYCAMERA_STATEFLAG__CROPSUPPORTED) == 0) {
		PyErr_SetString(PyExc_ValueError, "Cropping is not supported on this device, cannot get cropping information");
		return NULL;
	}

	/* Return a tuple ... */
	PyObject* lpRes = PyTuple_New(4);
	PyObject* lpValues[4] = { NULL, NULL, NULL, NULL };

	if(lpRes == NULL) { PyErr_SetNone(PyExc_MemoryError); goto cleanup; }


	lpValues[0] = PyLong_FromLong(lpSelf->cropCap.bounds.left);
	lpValues[1] = PyLong_FromLong(lpSelf->cropCap.bounds.top);
	lpValues[2] = PyLong_FromUnsignedLong(lpSelf->cropCap.bounds.width);
	lpValues[3] = PyLong_FromUnsignedLong(lpSelf->cropCap.bounds.height);

	for(i = 0; i < sizeof(lpValues)/sizeof(PyObject*); i=i+1) {
		if(lpValues[i] == 0) { PyErr_SetNone(PyExc_MemoryError); goto cleanup; }
		PyTuple_SetItem(lpRes, i, lpValues[i]);
	}

	return lpRes;

cleanup:
	if(lpRes != NULL) { Py_DECREF(lpRes); lpRes = NULL; }
	for(i = 0; i < sizeof(lpValues)/sizeof(PyObject*); i++) {
		if(lpValues[i] != NULL) { Py_DECREF(lpValues[i]); lpValues[i] = NULL; }
	}
	return NULL;
}

static PyObject* pyCamera_Property_CropDefault_Get(
	struct pyCamera_Instance* lpSelf,
	void* lpClosure
) {
	unsigned long int i;

	if(lpSelf->hHandle == -1) {
		PyErr_SetString(PyExc_ValueError, "Device not opened, cannot get cropping information");
		return NULL;
	}
	if((lpSelf->stateFlags & PYCAMERA_STATEFLAG__CROPSUPPORTED) == 0) {
		PyErr_SetString(PyExc_ValueError, "Cropping is not supported on this device, cannot get cropping information");
		return NULL;
	}

	/* Return a tuple ... */
	PyObject* lpRes = PyTuple_New(4);
	PyObject* lpValues[4] = { NULL, NULL, NULL, NULL };

	if(lpRes == NULL) { PyErr_SetNone(PyExc_MemoryError); goto cleanup; }


	lpValues[0] = PyLong_FromLong(lpSelf->cropCap.defrect.left);
	lpValues[1] = PyLong_FromLong(lpSelf->cropCap.defrect.top);
	lpValues[2] = PyLong_FromUnsignedLong(lpSelf->cropCap.defrect.width);
	lpValues[3] = PyLong_FromUnsignedLong(lpSelf->cropCap.defrect.height);

	for(i = 0; i < sizeof(lpValues)/sizeof(PyObject*); i=i+1) {
		if(lpValues[i] == 0) { PyErr_SetNone(PyExc_MemoryError); goto cleanup; }
		PyTuple_SetItem(lpRes, i, lpValues[i]);
	}

	return lpRes;

cleanup:
	if(lpRes != NULL) { Py_DECREF(lpRes); lpRes = NULL; }
	for(i = 0; i < sizeof(lpValues)/sizeof(PyObject*); i++) {
		if(lpValues[i] != NULL) { Py_DECREF(lpValues[i]); lpValues[i] = NULL; }
	}
	return NULL;
}

static PyObject* pyCamera_Property_CropRegion_Get(
	struct pyCamera_Instance* lpSelf,
	void* lpClosure
) {
	unsigned long int i;

	if(lpSelf->hHandle == -1) {
		PyErr_SetString(PyExc_ValueError, "Device not opened, cannot get cropping information");
		return NULL;
	}
	if((lpSelf->stateFlags & PYCAMERA_STATEFLAG__CROPSUPPORTED) == 0) {
		PyErr_SetString(PyExc_ValueError, "Cropping is not supported on this device, cannot get cropping information");
		return NULL;
	}

	/* Return a tuple ... */
	PyObject* lpRes = PyTuple_New(4);
	PyObject* lpValues[4] = { NULL, NULL, NULL, NULL };

	if(lpRes == NULL) { PyErr_SetNone(PyExc_MemoryError); goto cleanup; }


	lpValues[0] = PyLong_FromLong(lpSelf->cropCurrent.c.left);
	lpValues[1] = PyLong_FromLong(lpSelf->cropCurrent.c.top);
	lpValues[2] = PyLong_FromUnsignedLong(lpSelf->cropCurrent.c.width);
	lpValues[3] = PyLong_FromUnsignedLong(lpSelf->cropCurrent.c.height);

	for(i = 0; i < sizeof(lpValues)/sizeof(PyObject*); i=i+1) {
		if(lpValues[i] == 0) { PyErr_SetNone(PyExc_MemoryError); goto cleanup; }
		PyTuple_SetItem(lpRes, i, lpValues[i]);
	}

	return lpRes;

cleanup:
	if(lpRes != NULL) { Py_DECREF(lpRes); lpRes = NULL; }
	for(i = 0; i < sizeof(lpValues)/sizeof(PyObject*); i++) {
		if(lpValues[i] != NULL) { Py_DECREF(lpValues[i]); lpValues[i] = NULL; }
	}
	return NULL;
}

static int pyCamera_Property_CropRegion_Set(
	struct pyCamera_Instance* lpSelf,
	PyObject* lpValue,
	void* lpClosure
) {
	unsigned long int i;

	if(lpSelf->hHandle == -1) {
		PyErr_SetString(PyExc_ValueError, "Device is not opened, cannot set cropping region");
		return -1;
	}
	if((lpSelf->stateFlags & PYCAMERA_STATEFLAG__STREAMING) != 0) {
		PyErr_SetString(PyExc_ValueError, "Streaming is running, cannot set cropping region without stopping");
		return -1;
	}
	if((lpSelf->stateFlags & PYCAMERA_STATEFLAG__CROPSETSUPPORTED) == 0) {
		PyErr_SetString(PyExc_ValueError, "Device does not support setting of cropping region");
		return -1;
	}
	if(lpValue == NULL) {
		PyErr_SetString(PyExc_TypeError, "No cropping region supplied");
		return -1;
	}

	if(!PyTuple_Check(lpValue)) {
		PyErr_SetString(PyExc_TypeError, "Cropping region has to be a 4-tuple (left, top, widht, height)");
		return -1;
	}
	if(PyTuple_Size(lpValue) != 4) {
		PyErr_SetString(PyExc_TypeError, "Cropping region has to be a 4-tuple (left, top, widht, height)");
		return -1;
	}

	for(i = 0; i < 4; i=i+1) {
		if(!PyLong_Check(PyTuple_GetItem(lpValue, i))) {
			PyErr_SetString(PyExc_TypeError, "Cropping region has to be a 4 tuple of integers (left, top, width, height)");
			return -1;
		}
	}

	/* Do boundary checking ... */
	long newLeft;
	long newTop;
	long newWidth;
	long newHeight;

	newLeft		= PyLong_AsLong(PyTuple_GetItem(lpValue, 0));
	newTop		= PyLong_AsLong(PyTuple_GetItem(lpValue, 1));
	newWidth	= PyLong_AsLong(PyTuple_GetItem(lpValue, 2));
	newHeight	= PyLong_AsLong(PyTuple_GetItem(lpValue, 3));

	/* Check if rect is in boundary ... start with top and left */
	if(
		(newLeft < lpSelf->cropCap.bounds.left) ||
		(newTop < lpSelf->cropCap.bounds.top) ||
		(newLeft > (lpSelf->cropCap.bounds.left + lpSelf->cropCap.bounds.width)) ||
		(newTop > (lpSelf->cropCap.bounds.top + lpSelf->cropCap.bounds.height)) ||
		(newWidth <= 0) ||
		(newHeight <= 0) ||
		((newLeft + newWidth) > (lpSelf->cropCap.bounds.left + lpSelf->cropCap.bounds.width)) ||
		((newTop + newHeight) > (lpSelf->cropCap.bounds.top + lpSelf->cropCap.bounds.height))
	) {
		PyErr_SetString(PyExc_ValueError, "Cropping region out of range");
		return -1;
	}

	struct v4l2_crop cNew;
	cNew.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	cNew.c.left = newLeft;
	cNew.c.top = newTop;
	cNew.c.width = (unsigned long)newWidth;
	cNew.c.height = (unsigned long)newHeight;

	if(xioctl(lpSelf->hHandle, VIDIOC_S_CROP, &cNew) == -1) {
		PyErr_SetString(PyExc_BaseException, "Failed to set cropping region");
		return -1;
	}

	lpSelf->cropCurrent.c = cNew.c;

	/* Return success */
	return 0;
}

static PyObject* pyCamera_Property_HasCrop(
	struct pyCamera_Instance* lpSelf,
	void* lpClosure
) {
	unsigned long int i;

	if(lpSelf->hHandle == -1) {
		PyErr_SetString(PyExc_ValueError, "Device not opened, cannot get cropping information");
		return NULL;
	}

	if((lpSelf->stateFlags & PYCAMERA_STATEFLAG__CROPSUPPORTED) == 0) {
		Py_INCREF(Py_False);
		return Py_False;
	} else {
		Py_INCREF(Py_True);
		return Py_True;
	}
}

static PyObject* pyCamera_Property_HasCropSetting(
	struct pyCamera_Instance* lpSelf,
	void* lpClosure
) {
	unsigned long int i;

	if(lpSelf->hHandle == -1) {
		PyErr_SetString(PyExc_ValueError, "Device not opened, cannot get cropping information");
		return NULL;
	}

	if((lpSelf->stateFlags & PYCAMERA_STATEFLAG__CROPSETSUPPORTED) == 0) {
		Py_INCREF(Py_False);
		return Py_False;
	} else {
		Py_INCREF(Py_True);
		return Py_True;
	}
}

static int pyCamera_Property_BufferCount_Set(
	struct pyCamera_Instance* lpSelf,
	PyObject* lpValue,
	void* lpClosure
) {
	if(lpValue == NULL) {
		PyErr_SetString(PyExc_TypeError, "Buffer count has to be a integer number");
		return -1;
	}
	if(!PyLong_Check(lpValue)) {
		PyErr_SetString(PyExc_TypeError, "Buffer count has to be a integer value");
		return -1;
	}
	if((lpSelf->stateFlags & PYCAMERA_STATEFLAG__STREAMING) != 0) {
		PyErr_SetString(PyExc_ValueError, "Cannot modify buffer count while streaming");
		return -1;
	}

	signed long int newBufferCount = PyLong_AsLong(lpValue);
	if(newBufferCount <= 0) {
		PyErr_SetString(PyExc_ValueError, "Buffer count has to be a positive integer");
		return -1;
	}

	lpSelf->dwBufferedFrames = (unsigned long int)newBufferCount;

	/* Return success */
	return 0;
}

static PyObject* pyCamera_Property_BufferCount_Get(
	struct pyCamera_Instance* lpSelf,
	void* lpClosure
) {
	PyObject* lpIntCount = PyLong_FromUnsignedLong(lpSelf->dwBufferedFrames);
	if(lpIntCount == NULL) { PyErr_SetNone(PyExc_MemoryError); return NULL; }
	return lpIntCount;
}

static PyObject* pyCamera_Property_FrameCallback_Get(
	struct pyCamera_Instance* lpSelf,
	void* lpClosure
) {
	if((lpSelf->stateFlags & PYCAMERA_STATEFLAG__STREAMING) != 0) {
		PyErr_SetString(PyExc_ValueError, "Cannot access callbacks while streaming");
		return NULL;
	}

	if(lpSelf->cbFrameCallback == NULL) {
		Py_INCREF(Py_None);
		return Py_None;
	}

	Py_INCREF(lpSelf->cbFrameCallback);
	return lpSelf->cbFrameCallback;
}

static int pyCamera_Property_FrameCallback_Set(
	struct pyCamera_Instance* lpSelf,
	PyObject* lpValue,
	void* lpClosure
) {
	unsigned long int i;

	if((lpSelf->stateFlags & PYCAMERA_STATEFLAG__STREAMING) != 0) {
		PyErr_SetString(PyExc_ValueError, "Cannot access callbacks while streaming");
		return -1;
	}

	if(!(PyCallable_Check(lpValue) || PyList_Check(lpValue))) {
		PyErr_SetString(PyExc_ValueError, "Callback has to be a list of callbacks or a callable");
		return -1;
	}

	if(PyList_Check(lpValue)) {
		/* We will validate members every time but we can do a quick check now ... */
		for(i = 0; i < (unsigned long int)PyList_Size(lpValue); i=i+1) {
			if(!PyCallable_Check(PyList_GetItem(lpValue, i))) {
				PyErr_SetString(PyExc_ValueError, "At least one list member is not callable");
				return -1;
			}
		}
	}

	Py_INCREF(lpValue);
	lpSelf->cbFrameCallback = lpValue;

	return 0;
}


static PyGetSetDef pyCamera_Properties[] = {
	/* Querying and settingdevice file information */
	{ "device", (getter)pyCamera_Property_Device_Get, (setter)pyCamera_Property_Device_Set, "Name of the device file (only mutable when closed)", NULL },

	/* Querying driver and device information */
	{ "driver", (getter)pyCamera_Property_Driver_Get, (setter)pyCamera_Property_Immutable_Set, "Immutable name of driver attached to the device", NULL},
	{ "card", (getter)pyCamera_Property_Card_Get, (setter)pyCamera_Property_Immutable_Set, "Immutable name of card", NULL},
	{ "busInfo", (getter)pyCamera_Property_BusInfo_Get, (setter)pyCamera_Property_Immutable_Set, "Immutable bus information from V4L device", NULL},

	/* Streaming configuration */
	{ "bufferCount", (getter)pyCamera_Property_BufferCount_Get, (setter)pyCamera_Property_BufferCount_Set, "Number of buffers allocated to the device in ringbuffer", NULL },

	/* Croppign capabilities */
	{ "hasCrop", (getter)pyCamera_Property_HasCrop, (setter)pyCamera_Property_Immutable_Set, "Immutable maximum cropping area size", NULL},
	{ "hasCropSetting", (getter)pyCamera_Property_HasCropSetting, (setter)pyCamera_Property_Immutable_Set, "Immutable maximum cropping area size", NULL},
	{ "cropBounds", (getter)pyCamera_Property_CropBounds_Get, (setter)pyCamera_Property_Immutable_Set, "Immutable maximum cropping area size", NULL},
	{ "cropDefault", (getter)pyCamera_Property_CropDefault_Get, (setter)pyCamera_Property_Immutable_Set, "Immutable default cropping area size", NULL},
	{ "cropRegion", (getter)pyCamera_Property_CropRegion_Get, (setter)pyCamera_Property_CropRegion_Set, "Setting and getting cropping area size", NULL},

	/* Callbacks when running in callback mode ... */
	{ "frameCallback", (getter)pyCamera_Property_FrameCallback_Get, (setter)pyCamera_Property_FrameCallback_Set, "Callback(s) that will be called on every captured frame (if present running in blocking mode)" },

	{ NULL }
};

static PyMethodDef pyCamera_Methods[] = {
	/* Connection handling */
	{ "open", pyCamera_Open, METH_VARARGS, "Open the camera" },
	{ "close", pyCamera_Close, METH_VARARGS, "Close the camera" },

	/* Polling interface */
	{ "streamOn", pyCamera_StreamOn, METH_VARARGS, "Start streaming in configured format with configured crop settings and configured buffer count" },
	{ "streamOff", pyCamera_StreamOff, METH_VARARGS, "Stop streaming" },
	{ "nextFrame", pyCamera_NextFrame, METH_VARARGS, "Grab next frame" },

	/* Callback interface */
	{ "stream", pyCamera_Stream, METH_VARARGS, "Stream using callback function" },

	/* Context handler */
	{ "__enter__", pyCamera_Enter, METH_VARARGS, "Enter for the context method" },
	{ "__exit__", pyCamera_Exit, METH_VARARGS, "Exit for the context method" },

	{ NULL, NULL, 0, NULL }
};

static PyTypeObject simplepycamType_Camera = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "simplepycam.Camera",
    .tp_doc = PyDoc_STR("Video4Linux Camera"),
    .tp_basicsize = sizeof(struct pyCamera_Instance),
    .tp_itemsize = 0,

    .tp_methods = pyCamera_Methods,
		.tp_getset = pyCamera_Properties,

    .tp_new = pyCamera_New,
    .tp_init = (initproc)pyCamera_Init,
    .tp_dealloc = (destructor)pyCamera_Dealloc
};

PyMODINIT_FUNC PyInit_simplepycam(void) {
    PyObject* lpPyModule = NULL;

    /* Finalize all type definitions */
    if(PyType_Ready(&simplepycamType_Camera) < 0) {
        return NULL;
    }

    /* Create an instance of our module definition */
    lpPyModule = PyModule_Create(&simplepycamModuleDefinition);
    if(lpPyModule == NULL) {
        return NULL;
    }

    /* Increment refcounts on all type definitions */
    Py_INCREF(&simplepycamType_Camera);

    /* Add types to module ... */
    if((PyModule_AddObject(lpPyModule, "Camera", (PyObject*)(&simplepycamType_Camera))) < 0) {
        Py_DECREF(&simplepycamType_Camera);
        Py_DECREF(lpPyModule);
        return NULL;
    }

    return lpPyModule;
}
