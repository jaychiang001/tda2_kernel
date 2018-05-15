#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_plane_helper.h>

#include "vdrm.h"

struct v_plane {
	struct drm_plane base;
	uint32_t id;

	uint32_t nformats;
	uint32_t formats[V_PLANE_MAX_FORMATS];

	spinlock_t provider_lock;
	struct v_ctrl_endpoint *endpoint;
	bool new_endpoint;
};

#define to_v_plane(x) container_of(x, struct v_plane, base)

int v_plane_install_endpoint(struct drm_plane *plane, struct v_ctrl_endpoint *endpoint)
{
	struct v_plane *v_plane = to_v_plane(plane);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&v_plane->provider_lock, flags);
	if(v_plane->endpoint) {
		ret = -EBUSY;
		goto out;
	}
	v_plane->endpoint = endpoint;
	v_plane->new_endpoint = true;
out:
	spin_unlock_irqrestore(&v_plane->provider_lock, flags);

	return ret;
}

void v_plane_uninstall_endpoint(struct drm_plane *plane)
{
	struct v_plane *v_plane = to_v_plane(plane);
	unsigned long flags;

	spin_lock_irqsave(&v_plane->provider_lock, flags);
	v_plane->endpoint = NULL;
	v_plane->new_endpoint = false;
	spin_unlock_irqrestore(&v_plane->provider_lock, flags);
}

void v_plane_flush(struct drm_plane *plane, struct drm_framebuffer *fb,
		uint32_t dx, uint32_t dy, uint32_t dw, uint32_t dh,
		uint32_t sx, uint32_t sy, uint32_t sw, uint32_t sh,
		bool new_flush)
{
	struct v_plane *v_plane = to_v_plane(plane);
	unsigned long flags;

	spin_lock_irqsave(&v_plane->provider_lock, flags);
	if(!v_plane->endpoint)
		goto out;
	if(new_flush) {
		if(!v_plane->new_endpoint)
			goto out;
		else
			v_plane->new_endpoint = false;
	}

	v_controller_endpoint_flush(v_plane->endpoint, fb, dx, dy, dw, dh, sx, sy, sw, sh);

out:
	spin_unlock_irqrestore(&v_plane->provider_lock, flags);
}

static int v_plane_prepare_fb(struct drm_plane *plane, const struct drm_plane_state *new_state)
{
	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	return v_framebuffer_pin(new_state->fb);
}

static void v_plane_cleanup_fb(struct drm_plane *plane, const struct drm_plane_state *old_state)
{
	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	if (old_state->fb)
		v_framebuffer_unpin(old_state->fb);
}

static void v_plane_atomic_update(struct drm_plane *plane, struct drm_plane_state *old_state)
{
	struct drm_plane_state *new_state = plane->state;

	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	if(new_state->crtc != old_state->crtc)
		if(old_state->crtc)
			v_crtc_add_plane_update(old_state->crtc, plane, NULL, 0, 0, 0, 0, 0, 0, 0, 0);
	if(new_state->crtc)
		v_crtc_add_plane_update(new_state->crtc, plane, new_state->fb,
				new_state->crtc_x, new_state->crtc_y, new_state->crtc_w, new_state->crtc_h,
				new_state->src_x >> 16, new_state->src_y >> 16,
				new_state->src_w >> 16, new_state->src_h >> 16);
}

static void v_plane_atomic_disable(struct drm_plane *plane, struct drm_plane_state *old_state)
{
	struct drm_plane_state *new_state = plane->state;

	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	if(new_state->crtc != old_state->crtc)
		if(old_state->crtc)
			v_crtc_add_plane_update(old_state->crtc, plane, NULL, 0, 0, 0, 0, 0, 0, 0, 0);
	if(new_state->crtc)
		v_crtc_add_plane_update(new_state->crtc, plane, new_state->fb,
				new_state->crtc_x, new_state->crtc_y, new_state->crtc_w, new_state->crtc_h,
				new_state->src_x >> 16, new_state->src_y >> 16,
				new_state->src_w >> 16, new_state->src_h >> 16);
}

static int v_plane_atomic_check(struct drm_plane *plane, struct drm_plane_state *state)
{
	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	return 0;
}

static void v_plane_destroy(struct drm_plane *plane)
{
	struct v_plane *v_plane = to_v_plane(plane);

	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	drm_plane_cleanup(plane);
	kfree(v_plane);
}

static int v_plane_atomic_set_property(struct drm_plane *plane, struct drm_plane_state *state, struct drm_property *property, uint64_t val)
{
	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	return -EINVAL;
}

static int v_plane_atomic_get_property(struct drm_plane *plane, const struct drm_plane_state *state, struct drm_property *property, uint64_t *val)
{
	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	return -EINVAL;
}

static int v_plane_update_plane(struct drm_plane *plane, struct drm_crtc *crtc, struct drm_framebuffer *fb, int crtc_x, int crtc_y, unsigned int crtc_w, unsigned int crtc_h, uint32_t src_x, uint32_t src_y, uint32_t src_w, uint32_t src_h)
{
	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	return drm_atomic_helper_update_plane(plane, crtc, fb,
			crtc_x, crtc_y, crtc_w, crtc_h,
			src_x, src_y, src_w, src_h);
}

static int v_plane_disable_plane(struct drm_plane *plane)
{
	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	return drm_atomic_helper_disable_plane(plane);
}

static void v_plane_reset(struct drm_plane *plane)
{
	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	drm_atomic_helper_plane_reset(plane);
}

static int v_plane_set_property(struct drm_plane *plane, struct drm_property *property, uint64_t val)
{
	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	return drm_atomic_helper_plane_set_property(plane, property, val);
}

static struct drm_plane_state *v_plane_atomic_duplicate_state(struct drm_plane *plane)
{
	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	return drm_atomic_helper_plane_duplicate_state(plane);
}

static void v_plane_atomic_destroy_state(struct drm_plane *plane, struct drm_plane_state *state)
{
	dbg_function_entry(plane->dev->dev, plane, (to_v_plane(plane))->id);

	drm_atomic_helper_plane_destroy_state(plane, state);
}

static const struct drm_plane_funcs v_plane_funcs = {
	.update_plane = v_plane_update_plane,
	.disable_plane = v_plane_disable_plane,
	.reset = v_plane_reset,
	.destroy = v_plane_destroy,
	.set_property = v_plane_set_property,
	.atomic_duplicate_state = v_plane_atomic_duplicate_state,
	.atomic_destroy_state = v_plane_atomic_destroy_state,
	.atomic_set_property = v_plane_atomic_set_property,
	.atomic_get_property = v_plane_atomic_get_property,
};

static const struct drm_plane_helper_funcs v_plane_helper_funcs = {
	.prepare_fb = v_plane_prepare_fb,
	.cleanup_fb = v_plane_cleanup_fb,
	.atomic_check = v_plane_atomic_check,
	.atomic_update = v_plane_atomic_update,
	.atomic_disable = v_plane_atomic_disable,
};

struct drm_plane *v_plane_init(struct drm_device *dev,
		uint32_t id, uint32_t crtc_bitmask,
		enum drm_plane_type type,
		struct device_node *np)
{
	struct drm_plane *plane;
	struct v_plane *v_plane;
	int ret;

	v_plane = kzalloc(sizeof(*v_plane), GFP_KERNEL);
	if (!v_plane)
		return NULL;

	v_plane->id = id;

	ret = of_property_count_elems_of_size(np, "supported-formats", sizeof(uint32_t));
	if(ret <= 0)
		goto error;

	if(ret > ARRAY_SIZE(v_plane->formats))
		goto error;

	v_plane->nformats = ret;
	ret = of_property_read_u32_array(np, "supported-formats",
			v_plane->formats, v_plane->nformats);
	if(ret)
		goto error;

	spin_lock_init(&v_plane->provider_lock);
	v_plane->endpoint = NULL;
	v_plane->new_endpoint = false;

	plane = &v_plane->base;

	ret = drm_universal_plane_init(dev, plane, crtc_bitmask,
				       &v_plane_funcs, v_plane->formats,
				       v_plane->nformats, type);
	if (ret < 0)
		goto error;

	drm_plane_helper_add(plane, &v_plane_helper_funcs);

	return plane;

error:
	kfree(v_plane);
	return NULL;
}

void v_plane_fini(struct drm_device *dev, struct drm_plane *plane)
{
	v_plane_destroy(plane);
}
