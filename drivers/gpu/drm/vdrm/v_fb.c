#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

#include "vdrm.h"

/* per-plane info for the fb: */
struct v_fb_plane {
	struct drm_gem_object *bo;
	uint32_t pitch;
	uint32_t offset;
};

struct v_framebuffer {
	struct drm_framebuffer base;
	struct v_fb_plane planes[4];
};

#define to_v_framebuffer(x) container_of(x, struct v_framebuffer, base)

void v_framebuffer_populate_buffer_info(struct drm_framebuffer *fb,
		uint32_t *handles, uint32_t *pitches, uint32_t *offsets,
		uint32_t array_sz)
{
	struct v_framebuffer *v_fb = to_v_framebuffer(fb);
	int i, n = drm_format_num_planes(fb->pixel_format);

	for (i = 0; i < n; i++) {
		struct v_fb_plane *plane = &v_fb->planes[i];

		if(i >= array_sz)
			break;
		handles[i] = v_gem_get_v_ctrl_gem_handle(plane->bo);
		offsets[i] = plane->offset;
		pitches[i] = plane->pitch;
	}
	for(; i < array_sz; i++) {
		handles[i] = 0;
		offsets[i] = 0;
		pitches[i] = 0;
	}
}

static int v_framebuffer_create_handle(struct drm_framebuffer *fb,
		struct drm_file *file_priv,
		unsigned int *handle)
{
	struct v_framebuffer *v_fb = to_v_framebuffer(fb);

	dbg_function_entry(fb->dev->dev, framebuffer, 0);

	return drm_gem_handle_create(file_priv,
			v_fb->planes[0].bo, handle);
}

static void v_framebuffer_destroy(struct drm_framebuffer *fb)
{
	struct v_framebuffer *v_fb = to_v_framebuffer(fb);
	int i, n = drm_format_num_planes(fb->pixel_format);

	dbg_function_entry(fb->dev->dev, framebuffer, 0);

	drm_framebuffer_cleanup(fb);

	for (i = 0; i < n; i++) {
		struct v_fb_plane *plane = &v_fb->planes[i];
		if (plane->bo)
			drm_gem_object_unreference_unlocked(plane->bo);
	}

	kfree(v_fb);
}

static int v_framebuffer_dirty(struct drm_framebuffer *fb,
		struct drm_file *file_priv, unsigned flags, unsigned color,
		struct drm_clip_rect *clips, unsigned num_clips)
{
	dbg_function_entry(fb->dev->dev, framebuffer, 0);

	return 0;
}

static const struct drm_framebuffer_funcs v_framebuffer_funcs = {
	.create_handle = v_framebuffer_create_handle,
	.destroy = v_framebuffer_destroy,
	.dirty = v_framebuffer_dirty,
};

/* pin, prepare for scanout: */
int v_framebuffer_pin(struct drm_framebuffer *fb)
{
	return 0;
}

/* unpin, no longer being scanned out: */
void v_framebuffer_unpin(struct drm_framebuffer *fb)
{
}

#ifdef CONFIG_DEBUG_FS
void v_framebuffer_describe(struct drm_framebuffer *fb, struct seq_file *m)
{
	struct v_framebuffer *v_fb = to_v_framebuffer(fb);
	int i, n = drm_format_num_planes(fb->pixel_format);

	seq_printf(m, "fb: %dx%d@%4.4s\n", fb->width, fb->height,
			(char *)&fb->pixel_format);

	for (i = 0; i < n; i++) {
		struct v_fb_plane *plane = &v_fb->planes[i];
		seq_printf(m, "   %d: offset=%d pitch=%d\n",
				i, plane->offset, plane->pitch);
	}
}
#endif

struct drm_framebuffer *v_framebuffer_init(struct drm_device *dev,
		struct drm_mode_fb_cmd2 *mode_cmd, struct drm_gem_object **bos)
{
	struct v_framebuffer *v_fb = NULL;
	struct drm_framebuffer *fb = NULL;
	int ret, i, n = drm_format_num_planes(mode_cmd->pixel_format);

	if (!vdrm_check_supported_formats(mode_cmd->pixel_format)) {
		dev_err(dev->dev, "unsupported pixel format: %4.4s\n",
				(char *)&mode_cmd->pixel_format);
		ret = -EINVAL;
		goto fail;
	}

	v_fb = kzalloc(sizeof(*v_fb), GFP_KERNEL);
	if (!v_fb) {
		ret = -ENOMEM;
		goto fail;
	}

	fb = &v_fb->base;

	for (i = 0; i < n; i++) {
		struct v_fb_plane *plane = &v_fb->planes[i];
		int pitch = mode_cmd->pitches[i];

		plane->bo     = bos[i];
		plane->offset = mode_cmd->offsets[i];
		plane->pitch  = pitch;
	}

	drm_helper_mode_fill_fb_struct(fb, mode_cmd);

	ret = drm_framebuffer_init(dev, fb, &v_framebuffer_funcs);
	if (ret) {
		dev_err(dev->dev, "framebuffer init failed: %d\n", ret);
		goto fail;
	}

	return fb;

fail:
	kfree(v_fb);

	return ERR_PTR(ret);
}
