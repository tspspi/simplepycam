#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/stat.h>

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

    lpNewInstance->lpDeviceName     = NULL;
    lpNewInstance->hHandle          = -1;

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

    if(!PyArg_ParseTupleAndKeywords(args, kwds, "|s", pyCamera_Init__KWList, &lpArg_Dev)) {
        return -1;
    }

    if(lpSelf->lpDeviceName != NULL) {
        free(lpSelf->lpDeviceName);
        lpSelf->lpDeviceName = NULL;
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
		close(lpThis->hHandle);
		lpThis->hHandle = -1;
		PyErr_SetString(PyExc_ValueError, "Failed to query camera capabilities");
		return NULL;
	}

	/* We return nothing ... */
	Py_INCREF(Py_None);
	return Py_None;
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

	/* Close handle ... */
	close(lpThis->hHandle);
	lpThis->hHandle = -1;

	/* We return nothing ... */
	Py_INCREF(Py_None);
	return Py_None;
}





static PyMethodDef pyCamera_Methods[] = {
	{ "open", pyCamera_Open, METH_VARARGS, "Open the camera" },
	{ "close", pyCamera_Close, METH_VARARGS, "Close the camera" },
	{ NULL, NULL, 0, NULL }
};

static PyTypeObject simplepycamType_Camera = {
    PyVarObject_HEAD_INIT(NULL, 0)

    .tp_name = "simplepycam.Camera",
    .tp_doc = PyDoc_STR("Video4Linux Camera"),
    .tp_basicsize = sizeof(struct pyCamera_Instance),
    .tp_itemsize = 0,

    .tp_methods = pyCamera_Methods,
/*    .tp_members = pyCamera_Members, */

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
