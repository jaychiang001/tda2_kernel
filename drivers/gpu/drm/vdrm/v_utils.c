#include "vdrm.h"

static const uint32_t formats[] = {
	/* 16bpp [A]RGB: */
	DRM_FORMAT_RGB565,
	DRM_FORMAT_RGBX4444,
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_RGBA4444,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB1555,
	/* 24bpp RGB: */
	DRM_FORMAT_RGB888,
	/* 32bpp [A]RGB: */
	DRM_FORMAT_RGBX8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_RGBA8888,
	DRM_FORMAT_ARGB8888,
	/* YUV: */
	DRM_FORMAT_NV12,
	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
};

void vdrm_get_supported_formats(uint32_t *array, uint32_t *n)
{
	int i, max = *n;

	for(i = 0; i < ARRAY_SIZE(formats); i++) {
		if(i >= max)
			break;
		array[i] = formats[i];
	}
	*n = i;
}

bool vdrm_check_supported_formats(uint32_t format)
{
	int i;

	for(i = 0; i < ARRAY_SIZE(formats); i++) {
		if(formats[i] == format)
			return true;
	}
	return false;
}
