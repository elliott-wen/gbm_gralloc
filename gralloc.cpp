/*
 * Copyright (C) 2010-2011 Chia-I Wu <olvaffe@gmail.com>
 * Copyright (C) 2010-2011 LunarG Inc.
 * Copyright (C) 2016 Linaro, Ltd., Rob Herring <robh@kernel.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define LOG_TAG "GRALLOC-GBM"

#include <cutils/log.h>
#include <cutils/sockets.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include <hardware/gralloc.h>
#include <system/graphics.h>

#include <gbm.h>

#include "gralloc_drm.h"
#include "gralloc_gbm_priv.h"
#include "gralloc_drm_handle.h"


struct gbm_module_t {
	gralloc_module_t base;

	pthread_mutex_t mutex;
	struct gbm_device *gbm;
};

static inline int gralloc_gbm_get_bpp(int format)
{
	int bpp;

	switch (format) {
	case HAL_PIXEL_FORMAT_RGBA_8888:
	case HAL_PIXEL_FORMAT_RGBX_8888:
	case HAL_PIXEL_FORMAT_BGRA_8888:
		bpp = 4;
		break;
	case HAL_PIXEL_FORMAT_RGB_888:
		bpp = 3;
		break;
	case HAL_PIXEL_FORMAT_RGB_565:
	case HAL_PIXEL_FORMAT_YCbCr_422_I:
		bpp = 2;
		break;
	/* planar; only Y is considered */
	case HAL_PIXEL_FORMAT_YV12:
	case HAL_PIXEL_FORMAT_YCbCr_422_SP:
	case HAL_PIXEL_FORMAT_YCrCb_420_SP:
		bpp = 1;
		break;
	default:
		bpp = 0;
		break;
	}

	return bpp;
}

/*
 * Initialize the DRM device object
 */
static int gbm_init(struct gbm_module_t *dmod)
{
	ALOGW("gbm_init");
	int err = 0;

	pthread_mutex_lock(&dmod->mutex);
	if (!dmod->gbm) {
		dmod->gbm = gbm_dev_create();
		if (!dmod->gbm)
			err = -EINVAL;
	}
	pthread_mutex_unlock(&dmod->mutex);

	return err;
}

static int gbm_mod_perform(const struct gralloc_module_t *mod, int op, ...)
{
	ALOGW("gbm_mod_perform");
	struct gbm_module_t *dmod = (struct gbm_module_t *) mod;
	va_list args;
	int err;
	uint32_t uop = static_cast<uint32_t>(op);

	err = gbm_init(dmod);
	if (err)
		return err;

	va_start(args, op);
	switch (uop) {
	case GRALLOC_MODULE_PERFORM_GET_DRM_FD:
		{
			int *fd = va_arg(args, int *);
			*fd = gbm_device_get_fd(dmod->gbm);
			err = 0;
		}
		break;
	/* TODO: This is a stub and should be implemented fully */
	case GRALLOC_MODULE_PERFORM_GET_USAGE:
		{
			err = 0;
		}
		break;
	default:
		err = -EINVAL;
		break;
	}
	va_end(args);

	return err;
}

static int gbm_mod_register_buffer(const gralloc_module_t *mod,
		buffer_handle_t handle)
{
	ALOGW("gbm_mod_register_buffer");
	struct gbm_module_t *dmod = (struct gbm_module_t *) mod;
	int err;

	err = gbm_init(dmod);
	if (err)
		return err;

	pthread_mutex_lock(&dmod->mutex);
	err = gralloc_gbm_handle_register(handle, dmod->gbm);
	pthread_mutex_unlock(&dmod->mutex);

	return err;
}

static int gbm_mod_unregister_buffer(const gralloc_module_t *mod,
		buffer_handle_t handle)
{
	ALOGW("gbm_mod_unregister_buffer");
	struct gbm_module_t *dmod = (struct gbm_module_t *) mod;
	int err;

	pthread_mutex_lock(&dmod->mutex);
	err = gralloc_gbm_handle_unregister(handle);
	pthread_mutex_unlock(&dmod->mutex);

	return err;
}

static int gbm_mod_lock(const gralloc_module_t *mod, buffer_handle_t handle,
		int usage, int x, int y, int w, int h, void **ptr)
{
	ALOGW("gbm_mod_lock");
	struct gbm_module_t *dmod = (struct gbm_module_t *) mod;
	int err;

	pthread_mutex_lock(&dmod->mutex);

	err = gralloc_gbm_bo_lock(handle, usage, x, y, w, h, ptr);
	ALOGV("buffer %p lock usage = %08x", handle, usage);

	pthread_mutex_unlock(&dmod->mutex);
	return err;
}

static int gbm_mod_unlock(const gralloc_module_t *mod, buffer_handle_t handle)
{
	ALOGW("gbm_mod_unlock");
	struct gbm_module_t *dmod = (struct gbm_module_t *) mod;
	int err;

	pthread_mutex_lock(&dmod->mutex);
	err = gralloc_gbm_bo_unlock(handle);
	pthread_mutex_unlock(&dmod->mutex);

	return err;
}

static int gbm_mod_close_gpu0(struct hw_device_t *dev)
{
	ALOGW("gbm_mod_close_gpu0");
	struct gbm_module_t *dmod = (struct gbm_module_t *)dev->module;
	struct alloc_device_t *alloc = (struct alloc_device_t *) dev;

	gbm_dev_destroy(dmod->gbm);
	free(alloc);

	return 0;
}

static int gbm_mod_free_gpu0(alloc_device_t *dev, buffer_handle_t handle)
{
	ALOGW("gbm_mod_free_gpu0");
	struct gbm_module_t *dmod = (struct gbm_module_t *) dev->common.module;

	pthread_mutex_lock(&dmod->mutex);

	gbm_free(handle);
	native_handle_close(handle); //native_handle_close does not do anything, but invoke an unnessary system call close 
	//delete handle;
	native_handle_delete((native_handle*)handle);

	pthread_mutex_unlock(&dmod->mutex);
	return 0;
}

static int gbm_mod_alloc_gpu0(alloc_device_t *dev,
		int w, int h, int format, int usage,
		buffer_handle_t *handle, int *stride)
{
	ALOGW("gbm_mod_alloc_gpu0");
	struct gbm_module_t *dmod = (struct gbm_module_t *) dev->common.module;
	struct gralloc_gbm_handle_t *gbm_handle;
	int err = 0;

	pthread_mutex_lock(&dmod->mutex);

	gbm_handle = gralloc_gbm_bo_create(dmod->gbm, w, h, format, usage);
	if (!gbm_handle) {
		err = -errno;
		goto unlock;
	}

	*handle = &gbm_handle->base;
	/* in pixels */
	*stride = gbm_handle->stride / gralloc_gbm_get_bpp(format);

	ALOGE("buffer %p usage = %08x", *handle, usage);
unlock:
	pthread_mutex_unlock(&dmod->mutex);
	return err;
}

static int gbm_mod_open_gpu0(struct gbm_module_t *dmod, hw_device_t **dev)
{
	ALOGW("gbm_mod_open_gpu0");
	struct alloc_device_t *alloc;
	int err;

	err = gbm_init(dmod);
	if (err)
		return err;

	alloc = (alloc_device_t *)malloc(sizeof(alloc_device_t));
	if (!alloc)
		return -EINVAL;

	alloc->common.tag = HARDWARE_DEVICE_TAG;
	alloc->common.version = 0;
	alloc->common.module = &dmod->base.common;
	alloc->common.close = gbm_mod_close_gpu0;

	alloc->alloc = gbm_mod_alloc_gpu0;
	alloc->free = gbm_mod_free_gpu0;

	*dev = &alloc->common;

	return 0;
}

static int gbm_fb_set_interval(struct framebuffer_device_t* dev,
            int interval)
{
	ALOGW("gbm_fb_set_interval");
	(void)dev;
	(void)interval;
	return 0;
}

static int gbm_mod_close_fb0(hw_device_t *dev)
{
	ALOGW("gbm_mod_close_fb0");
	struct framebuffer_device_t *fb = (struct framebuffer_device_t *) dev;

	free(fb);
	return 0;
}

static int _send_fd(int s, int fd)
{
  	char buf[1];
	struct iovec iov;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	unsigned int n;
	char cms[CMSG_SPACE(sizeof(int))];
	
	buf[0] = 0;
	iov.iov_base = buf;
	iov.iov_len = 1;

	memset(&msg, 0, sizeof msg);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = (caddr_t)cms;
	msg.msg_controllen = CMSG_LEN(sizeof(int));

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	memmove(CMSG_DATA(cmsg), &fd, sizeof(int));

	if((n=sendmsg(s, &msg, 0)) != iov.iov_len)
		return -1;
	return 0;
}


static int gbm_deliver_frame_to_x11(int postfd, char *result)
{
	
	char reply = 0;
	int unixfd = socket_local_client(REMOTE_X11_ENDPOINT, ANDROID_SOCKET_NAMESPACE_FILESYSTEM, SOCK_STREAM);
	if(unixfd <= 0)
	{
		ALOGE("failed to connect unix socket");
		return -1;
	}

	struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(unixfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    if(_send_fd(unixfd, postfd) == -1)
    {
    	ALOGE("failed to send unix socket");
        close(unixfd);
        return -1;
    }

    if(recv(unixfd, &reply, sizeof(reply), 0) != sizeof(reply))
    {
        ALOGE("failed to recv unix socket");
        close(unixfd);
        return -1;
    }
    close(unixfd);
    if(result != NULL)
    {
    	*result = reply;
    }
    return 0;
}


static int gbm_fb_post(struct framebuffer_device_t* dev, buffer_handle_t buffer)
{
	struct gbm_module_t *dmod = (struct gbm_module_t *) dev->common.module;
	//int err = 0;

	pthread_mutex_lock(&dmod->mutex);
	int framefd = gralloc_drm_get_prime_fd(buffer);
	gbm_deliver_frame_to_x11(framefd, NULL);
	pthread_mutex_unlock(&dmod->mutex);
	return 0;
}

static int gbm_mod_open_fb0(struct gbm_module_t *dmod, hw_device_t **dev)
{
	ALOGW("gbm_mod_open_fb0");
	struct framebuffer_device_t *fb;
	int err;

	fb = (framebuffer_device_t*)malloc(sizeof(framebuffer_device_t));
	if (!fb)
		return -EINVAL;

	fb->common.tag = HARDWARE_DEVICE_TAG;
	fb->common.version = 0;
	fb->common.module = &dmod->base.common;
	fb->common.close = gbm_mod_close_fb0;

	fb->setSwapInterval = gbm_fb_set_interval;
    fb->post            = gbm_fb_post;
    fb->compositionComplete = 0;
    fb->setUpdateRect = 0;
    const_cast<uint32_t&>(fb->flags) = 0;
    const_cast<uint32_t&>(fb->width) = 720;
    const_cast<uint32_t&>(fb->height) = 1280;
    const_cast<int&>(fb->stride) = 768;
    const_cast<int&>(fb->format) = HAL_PIXEL_FORMAT_RGBA_8888;
    const_cast<float&>(fb->xdpi) = 240;
    const_cast<float&>(fb->ydpi) = 240;
    const_cast<float&>(fb->fps) = 30; 
    const_cast<int&>(fb->minSwapInterval) = 1;
    const_cast<int&>(fb->maxSwapInterval) = 1;
	*dev = &fb->common;

	return 0;
}

static int gbm_mod_open(const struct hw_module_t *mod,
		const char *name, struct hw_device_t **dev)
{
	ALOGW("GBM_MOD_OPEN");
	struct gbm_module_t *dmod = (struct gbm_module_t *) mod;
	int err = -EINVAL;

	if (strcmp(name, GRALLOC_HARDWARE_GPU0) == 0)
		err = gbm_mod_open_gpu0(dmod, dev);
	else if (strcmp(name, GRALLOC_HARDWARE_FB0) == 0)
		err = gbm_mod_open_fb0(dmod, dev);

	return err;
}

static struct hw_module_methods_t gbm_mod_methods = {
	.open = gbm_mod_open
};

struct gbm_module_t HAL_MODULE_INFO_SYM = {
	.base = {
		.common = {
			.tag = HARDWARE_MODULE_TAG,
			.version_major = 1,
			.version_minor = 0,
			.id = GRALLOC_HARDWARE_MODULE_ID,
			.name = "GBM Memory Allocator",
			.author = "Rob Herring - Linaro",
			.methods = &gbm_mod_methods
		},
		.registerBuffer = gbm_mod_register_buffer,
		.unregisterBuffer = gbm_mod_unregister_buffer,
		.lock = gbm_mod_lock,
		.unlock = gbm_mod_unlock,
		.perform = gbm_mod_perform
	},

	.mutex = PTHREAD_MUTEX_INITIALIZER,
	.gbm = NULL,
};
